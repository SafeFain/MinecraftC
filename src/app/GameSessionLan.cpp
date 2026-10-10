#include "app/GameSession.h"
#include "debug/Log.h"
#include "game/SurvivalSession.h"
#include "plugins/ContentRegistry.h"
#include "world/WorldGenContext.h"
#include <algorithm>
#include <cmath>

namespace {
Lan::PlayerView playerView(uint64_t id, const std::string& nickname, const Player& player, const FishingView& fishing, glm::vec3 sleepFacing = {}) {
        Lan::PlayerView result;
        result.id = id; result.nickname = nickname; result.position = player.getPosition();
        result.yaw = std::remainder(player.getYaw(), 360.0f); result.pitch = player.getPitch();
        result.selected = static_cast<uint8_t>(player.selectedSlot());
        result.mode = player.gameMode(); result.flying = player.isFlying(); result.visual = player.visualState();
        result.mainhand = player.activeItem(); result.offhand = player.inventory().offhand(); result.fishing = fishing;
        result.sleepFacing = sleepFacing;
        return result;
}
}

Lan::Compatibility GameSession::lanCompatibility() {
    const auto& description=Plugins::content().networkDescription;
    return {Config::GAME_VERSION, WorldGenContext::GENERATION_VERSION, Lan::contentSignature(description), description};
}

bool GameSession::openLanRoom(uint16_t port, size_t capacity, bool loopbackOnly) {
    if (hostingLan()) return true;
    lanFailure.clear();
    if (!saveStore || !terrainGenerated) {
        lanFailure = "World is not ready";
        return false;
    }
    try {
        hostIdentity = Lan::ProfileStore::localIdentity(dataDirectory);
        guestProfiles = std::make_unique<Lan::ProfileStore>(saveStore->worldDirectory() / "players");
        lanHost.admit = [this](const Lan::Identity& identity) {
            return identity.id != hostIdentity.id && guestProfiles && guestProfiles->accepts(identity);
        };
        if (!lanHost.open(port, lanCompatibility(), capacity, loopbackOnly)) {
            lanFailure = lanHost.error();
            guestProfiles.reset();
            return false;
        }
        for (size_t index = 0; index < simulations.size(); ++index) {
            if (!simulations[index]) continue;
            simulations[index]->world.setBlockMutationCallback([this, index](int x, int z, uint64_t revision, uint32_t slot, BlockId block, bool persistent) {
                chunkJournal.record({static_cast<DimensionId>(index), x, z}, revision, {slot, static_cast<uint16_t>(block)});
            if (persistent) invalidateLanLod(static_cast<DimensionId>(index), x, z);
            });
        }
        return true;
    } catch (const std::exception& error) {
        lanFailure = error.what();
        lanHost.close();
        guestProfiles.reset();
        return false;
    }
}

void GameSession::admitLanPlayer(uint64_t id) {
    const auto peer = lanHost.peers().find(id);
    if (peer == lanHost.peers().end() || !guestProfiles) return;
    auto saved = guestProfiles->load(peer->second.identity);
    Lan::PlayerProfile profile;
    if (saved) profile = std::move(*saved);
    else {
        profile.identity = peer->second.identity;
        profile.mode = worldMetadata.gameMode;
        profile.positions[0] = glm::dvec3(worldMetadata.worldSpawn) + glm::dvec3(.5, 1.01, .5);
        profile.positioned[0] = true;
    }
    profile.identity.nickname = peer->second.identity.nickname;
    auto& runtime = ensureDimension(profile.dimension);
    auto guest = std::make_unique<LanPlayerRuntime>(runtime.world, runtime.entities, std::move(profile));
    guest->player.configureRules(guest->profile.mode, worldMetadata.difficulty);
    guest->player.setRoomPlayerId(id);
    guest->player.setBedCallback([this, id](const glm::ivec3& bed) { (void)beginLanSleep(id, bed); });
    guest->dead = guest->player.isSurvival() && guest->player.survivalStats().dead();
    guest->lastInput = lanNow;
    guestProfiles->save(guest->profile);
    guests.emplace(id, std::move(guest));
    {auto e=guests.at(id)->player.pluginEvent(MC_PLAYER_JOIN);e.role=MC_HOST;Plugins::dispatch(e);}
    bindLanFeedback(guests.at(id)->player, id);
    Lan::WorldInfo info{worldMetadata.displayName, worldMetadata.seed, worldMetadata.generationVersion,
                        worldMetadata.worldType, worldMetadata.difficulty, worldMetadata.gameRules};
    if (!lanHost.send(id, {Lan::MessageType::Environment, 0, Lan::encodeWorldInfo(info)}))
        lanHost.disconnect(id);
    else broadcastLanChat({Lan::ChatKind::Joined, peer->second.identity.nickname, {}});
}

void GameSession::saveLanPlayer(LanPlayerRuntime& guest) {
    if(!Plugins::content().runtimeFault.empty())return;
    auto& profile = guest.profile;
    profile.positions[static_cast<size_t>(profile.dimension)] = guest.player.getPosition();
    profile.positioned[static_cast<size_t>(profile.dimension)] = true;
    profile.mode = guest.player.gameMode();
    profile.inventory = guest.player.inventory();
    profile.cursor = guest.window.cursor(); profile.crafting = guest.window.crafting();
    const auto& stats = guest.player.survivalStats();
    profile.health = stats.health(); profile.hunger = stats.hunger();
    profile.saturation = stats.saturation(); profile.exhaustion = stats.exhaustion();
    profile.foodTickTimer = stats.foodTickTimer();
    if (guestProfiles) guestProfiles->save(profile);
}

void GameSession::removeLanPlayer(uint64_t id) {
    const auto found = guests.find(id);
    if (found == guests.end()) return;
    auto& guest = *found->second;
    {auto e=guest.player.pluginEvent(MC_PLAYER_LEAVE);e.role=MC_HOST;Plugins::dispatch(e);}
    guest.fishing.cancel();
    auto& runtime = *simulations[static_cast<size_t>(guest.profile.dimension)];
    for (const auto& stack : guest.window.close(guest.player.inventory()))
        runtime.entities.spawnItem(guest.player.getPosition() + glm::dvec3(0, .5, 0), stack);
    try { saveLanPlayer(guest); }
    catch (const std::exception& error) {
        lanFailure = error.what();
        LOG_ERROR("Could not save LAN player: " << error.what());
    }
    const auto nickname = guest.profile.identity.nickname;
    guests.erase(found);
    if (hostingLan()) broadcastLanChat({Lan::ChatKind::Left, nickname, {}});
}

void GameSession::closeLanRoom() {
    if (hostingLan()) closeAuthorityWindow(player, simulation(), hostWindow, hostWindowView);
    lodJobs.clear();
    lanHost.close();
    for (auto& runtime : simulations) if (runtime) runtime->world.setBlockMutationCallback({});
    chunkJournal.clear();
    while (!guests.empty()) removeLanPlayer(guests.begin()->first);
    guestProfiles.reset();
    lanEvents.clear();
    lanChat.clear(); hostChatTokens = 5; hostChatLast = lanNow;
    updateLanInterests();
}

void GameSession::pollLan(double now) {
    if (lanJoining) { pollReplica(now); return; }
    if (!hostingLan()) return;
    lanNow = now;
    auto events = lanHost.poll(now);
    for (uint64_t id : events.left) removeLanPlayer(id);
    for (uint64_t id : events.joined) {
        try { admitLanPlayer(id); }
        catch (const std::exception& error) {
            lanFailure = error.what();
            LOG_ERROR("LAN admission failed: " << error.what());
            lanHost.disconnect(id);
        }
    }
    for (const auto& received : events.messages) {
        const auto found = guests.find(received.peer);
        if (found == guests.end()) continue;
        auto& guest = *found->second;
        try {
            if (received.chunks) throw Lan::ProtocolError("Unexpected client bulk message");
            if (received.message.type == Lan::MessageType::Leave && received.message.payload.empty()) {
                lanHost.disconnect(received.peer);
                removeLanPlayer(received.peer);
                continue;
            }
            if (received.message.type == Lan::MessageType::ChunkRequest) {
                Lan::Reader reader(received.message.payload);
                if (reader.u8() != 1) throw Lan::ProtocolError("Unknown chunk request schema");
                const auto requestedDimension = static_cast<DimensionId>(reader.u8());
                const uint64_t epoch = reader.u64();
                const int x = static_cast<int32_t>(reader.u32()), z = static_cast<int32_t>(reader.u32());
                reader.finish();
                const auto position = guest.player.getPosition();
                const int64_t dx = static_cast<int64_t>(x) - World::worldToChunkX(position.x);
                const int64_t dz = static_cast<int64_t>(z) - World::worldToChunkZ(position.z);
                if (requestedDimension == guest.profile.dimension && epoch == guest.chunkEpoch &&
                    std::abs(dx) <= guest.radius && std::abs(dz) <= guest.radius && dx * dx + dz * dz <= guest.radius * guest.radius)
                    guest.sentChunkRevisions.erase({x, z});
                continue;
            }
            if (received.message.type == Lan::MessageType::LodRequest) {
                requestLanLod(guest, Lan::decodeLodUpdate(received.message.payload, false)); continue;
            }
            if (received.message.type == Lan::MessageType::Chat) {
                auto message = Lan::decodeChat(received.message.payload);
                if (message.kind != Lan::ChatKind::Message || !message.nickname.empty()) throw Lan::ProtocolError("Invalid guest chat sender");
                guest.chatTokens = std::min(5.0f, guest.chatTokens + static_cast<float>(std::max(0.0, now - guest.chatLast)));
                guest.chatLast = now;
                if (guest.chatTokens >= 1) {
                    guest.chatTokens -= 1;
                    message.nickname = guest.profile.identity.nickname;
                    broadcastLanChat(message);
                }
                continue;
            }
            if (received.message.type == Lan::MessageType::Action) {
                applyLanAction(guest, Lan::decodeGameAction(received.message.payload));
                continue;
            }
            if (received.message.type != Lan::MessageType::Input)
                throw Lan::ProtocolError("Unexpected client message");
            const auto input = Lan::decodePlayerInput(received.message.payload);
            if (input.dimension != guest.profile.dimension) continue;
            guest.input.beginFrame();
            guest.input.clearVirtual();
            guest.input.setVirtual(InputAction::MoveForward, std::max(0.0f, input.forward));
            guest.input.setVirtual(InputAction::MoveBackward, std::max(0.0f, -input.forward));
            guest.input.setVirtual(InputAction::MoveRight, std::max(0.0f, input.strafe));
            guest.input.setVirtual(InputAction::MoveLeft, std::max(0.0f, -input.strafe));
            guest.input.setVirtual(InputAction::Jump, (input.buttons & Lan::InputButtons::Jump) ? 1 : 0);
            guest.input.setVirtual(InputAction::Sneak, (input.buttons & Lan::InputButtons::Sneak) ? 1 : 0);
            guest.input.setVirtual(InputAction::Sprint, (input.buttons & Lan::InputButtons::Sprint) ? 1 : 0);
            guest.player.setOrientation(input.yaw, input.pitch);
            guest.player.setSelectedSlot(input.selected);
            guest.radius = input.radius; guest.lodRadius = input.lodRadius;
            // Actions are applied on the simulation tick after terrain readiness
            // validation. TCP packet count never controls elapsed game time.
            guest.buttons = input.buttons;
            guest.lastInput = now;
            guest.inputSequence = received.message.sequence;
        } catch (const Lan::ProtocolError& error) {
            LOG_WARN("Rejected LAN gameplay packet: " << error.what());
            lanHost.disconnect(received.peer);
            removeLanPlayer(received.peer);
        }
    }
    updateLanInterests();
    sendLanStates();
    sendLanEntities();
    sendLanChunks();
    sendLanLod();
}

void GameSession::updateLanInterests() {
    if (!lanJoining) {
        replicaOthers.clear();
        for (const auto& entry : guests) if (entry.second->profile.dimension == dimension)
            replicaOthers.push_back(playerView(entry.first, entry.second->profile.identity.nickname, entry.second->player,
                entry.second->fishing.view(), entry.second->sleepFacing));
    }
    for (size_t index = 0; index < simulations.size(); ++index) {
        if (!simulations[index]) continue;
        std::vector<StreamingInterest> interests;
        std::vector<EntityPlayerView> pvpPlayers;
        if (index == static_cast<size_t>(dimension)) pvpPlayers.push_back({0, &player, !playerDead, !playerDead, terrainGenerated});
        for (const auto& entry : guests) {
            const auto& guest = *entry.second;
            if (static_cast<size_t>(guest.profile.dimension) != index) continue;
            pvpPlayers.push_back({entry.first, &entry.second->player, !guest.dead, !guest.dead, !guest.loading});
            const auto position = guest.player.getPosition();
            interests.push_back({World::worldToChunkX(position.x), World::worldToChunkZ(position.z), guest.radius});
        }
        simulations[index]->entities.setPvpPlayers(hostingLan() && lanPvp, std::move(pvpPlayers));
        simulations[index]->world.setLocalRenderEnabled(index == static_cast<size_t>(dimension));
        simulations[index]->world.setAdditionalStreamingInterests(std::move(interests));
    }
}

void GameSession::updateLanPlayers(float dt) {
    updateLanSleep(dt);
    static const std::array<InputBinding, INPUT_ACTION_COUNT> noPhysicalBindings{};
    for (auto& entry : guests) {
        auto& guest = *entry.second;
        const Plugins::ActorScope actor({entry.first,static_cast<uint32_t>(guest.profile.dimension),MC_HOST});
        auto& runtime = *simulations[static_cast<size_t>(guest.profile.dimension)];
        const auto position = guest.player.getPosition();
        const int cx = World::worldToChunkX(position.x), cz = World::worldToChunkZ(position.z);
        bool ready = true;
        for (int dx = -1; dx <= 1; ++dx) for (int dz = -1; dz <= 1; ++dz)
            ready = ready && runtime.world.isGeneratedAt((cx + dx) * 16, (cz + dz) * 16);
        guest.loading = !ready;
        if (ready && guest.pendingRespawn) {
            guest.pendingRespawn = false;
            const auto bed = guest.profile.bedSpawn ? runtime.world.validBedFoot(*guest.profile.bedSpawn) : std::nullopt;
            const auto spawn = bed ? *bed : worldMetadata.worldSpawn;
            const double height = bed ? blockCollisionHeight(runtime.world.getBlock(spawn.x, spawn.y, spawn.z)) + .001 : 1.01;
            guest.player.setPosition(glm::dvec3(spawn) + glm::dvec3(.5, height, .5));
            if (!bed) guest.profile.bedSpawn.reset();
            guest.loading = !runtime.world.isGeneratedAt(spawn.x, spawn.z);
        }
        if (guest.profile.dimension == DimensionId::Heaven && guest.player.getPosition().y < Config::WORLD_MIN_Y - 32) {
            travelLanPlayer(guest, DimensionId::Overworld); continue;
        }
        if (guest.dead || guest.loading || guest.windowView.kind != InventoryWindowKind::Closed || guest.sleep != 0 || lanNow - guest.lastInput > 1) {
            guest.input.clearVirtual();
            guest.buttons = 0;
        }
        guest.input.update(noPhysicalBindings);
        if (guest.dead || guest.loading) guest.player.cancelBowCharge();
        for (const auto& binding : std::array<std::pair<uint8_t, int>, 2>{{
                {Lan::InputButtons::Attack, MouseButton::Left}, {Lan::InputButtons::Use, MouseButton::Right}}}) {
            if (!(guest.buttons & binding.first) && (guest.appliedButtons & binding.first))
                guest.player.handleMouseButton(binding.second, ButtonAction::Release);
        }
        guest.player.setMouseLocked(!guest.dead && !guest.loading && guest.sleep == 0 && guest.windowView.kind == InventoryWindowKind::Closed);
        if (!guest.dead && !guest.loading) {
            guest.player.handleMovement(guest.input, dt);
            FishingEnvironment environment{
                [&runtime](glm::ivec3 p) { return runtime.world.getLoadedBlock(p.x, p.y, p.z); },
                [&runtime](glm::ivec3 p) { return runtime.world.hasSkyAccess(p.x, p.y, p.z); },
                [&runtime](glm::ivec3 p) { return runtime.weather.raining() &&
                    runtime.world.precipitationAt(p.x, p.y, p.z) == PrecipitationType::Rain; }
            };
            const auto item = guest.player.activeItem();
            if (guest.fishing.view().active() && (guest.player.selectedSlot() != guest.fishingSlot ||
                item.id != ItemId::FISHING_ROD || item.damage != guest.fishingRodDamage)) guest.fishing.cancel();
            for (const auto& binding : std::array<std::pair<uint8_t, int>, 2>{{
                    {Lan::InputButtons::Attack, MouseButton::Left}, {Lan::InputButtons::Use, MouseButton::Right}}}) {
                if (!(guest.buttons & binding.first) || (guest.appliedButtons & binding.first)) continue;
                auto useEvent=guest.player.pluginEvent(MC_USE_PRE);
                const bool use=binding.second==MouseButton::Right&&!guest.player.isSpectator();
                if(use) {
                    useEvent.item=static_cast<uint16_t>(guest.player.activeItem().id);
                    const auto hit=runtime.world.raycast(guest.player.getEyePosition(),guest.player.getForward(),Config::REACH_DISTANCE);
                    if(hit){useEvent.x=hit->blockPos.x;useEvent.y=hit->blockPos.y;useEvent.z=hit->blockPos.z;useEvent.block=static_cast<uint16_t>(runtime.world.getBlock(useEvent.x,useEvent.y,useEvent.z));}
                    if(!Plugins::dispatch(useEvent))continue;
                }
                if (binding.second == MouseButton::Right && item.id == ItemId::FISHING_ROD) {
                    guest.fishing.update(0, guest.player.getEyePosition(), environment);
                    guest.fishingSlot = guest.player.selectedSlot();
                    guest.fishingRodDamage = item.damage;
                    guest.fishing.use(guest.player.getEyePosition(), guest.player.getForward(), guest.player.velocity(), environment);
                    guest.player.animateItemUse();
                } else guest.player.handleMouseButton(binding.second, ButtonAction::Press);
                if(use){useEvent.kind=MC_USE_POST;useEvent.cancelled=0;Plugins::dispatch(useEvent);}
            }
            const auto eye = guest.player.getEyePosition();
            const glm::ivec3 eyeBlock(glm::floor(eye));
            guest.player.setRainExposure(runtime.weather.raining() &&
                runtime.world.precipitationAt(eyeBlock.x, eyeBlock.y, eyeBlock.z) == PrecipitationType::Rain &&
                runtime.world.hasSkyAccess(eyeBlock.x, eyeBlock.y, eyeBlock.z));
            guest.player.update(dt);
            guest.fishing.update(dt, guest.player.getEyePosition(), environment);
            for (const auto& event : guest.fishing.takeEvents()) {
                Lan::GameEvent remote; remote.kind = Lan::EventKind::Fishing; remote.owner = entry.first; remote.dimension = guest.profile.dimension; remote.position = event.position; remote.flags = static_cast<uint8_t>(event.kind);
                broadcastGameEvent(remote, false, true);
                if (!event.catchItem.empty()) {
                    const auto delta = guest.player.getPosition() + glm::dvec3(0, .7, 0) - event.position;
                    const float duration = std::clamp(static_cast<float>(glm::length(delta)) / 12, .2f, 1.2f);
                    runtime.entities.spawnItem(event.position, event.catchItem,
                        glm::vec3(delta / static_cast<double>(duration)) + glm::vec3(0, 10 * duration, 0), .15f);
                }
                if (event.wear && guest.player.isSurvival() && guest.fishingSlot >= 0) {
                    auto& rod = guest.player.inventory().slot(static_cast<size_t>(guest.fishingSlot));
                    if (rod.id == ItemId::FISHING_ROD) {
                        rod.damage = static_cast<uint16_t>(rod.damage + event.wear);
                        if (rod.damage >= getItemProps(rod.id).maxDurability) rod.clear();
                        guest.fishingRodDamage = rod.damage;
                    }
                }
            }
        }
        guest.appliedButtons = guest.buttons;
        guest.appliedInputSequence = guest.inputSequence;
        guest.input.beginFrame();
    }
}

void GameSession::simulateDimension(DimensionId id, const std::vector<EntityPlayerView>& views,
                                    float dt, const Feedback& feedback, size_t& fluidRemaining,
                                    std::chrono::steady_clock::time_point fluidDeadline) {
    auto& runtime = *simulations[static_cast<size_t>(id)];
    const bool overworld = id == DimensionId::Overworld;
    runtime.entities.update(views, dt, runtime.daylight.isDay(), worldMetadata.difficulty == Difficulty::Peaceful,
                            overworld && runtime.weather.thundering(), overworld && runtime.weather.raining(), runtime.ticks);
    std::vector<glm::dvec3> positions;
    for (const auto& view : views) positions.push_back(view.player->getPosition());
    runtime.tickRemainder += dt * 20;
    while (runtime.tickRemainder >= 1) {
        ++runtime.ticks;
        runtime.tickRemainder -= 1;
        if (overworld) {
            runtime.weather.tick(worldMetadata.gameRules.boolean(GameRuleId::AdvanceWeather));
            tickLightning(runtime, feedback);
        }
        auto& world = runtime.world;
        world.tickBlockEntities();
        world.tickInteractiveBlocks([&](const glm::ivec3& p) { return runtime.entities.arrowTouchesButton(p); });
        for (const auto& drop : world.takeSupportDrops())
            runtime.entities.spawnItem(glm::dvec3(drop.first) + glm::dvec3(.5), drop.second);
        const size_t budget = std::min(Config::FLUID_UPDATES_PER_TICK, fluidRemaining);
        fluidRemaining -= budget;
        world.tickFluids(runtime.ticks, FluidTickBudget{budget, fluidDeadline});
        world.tickSurvival(positions, runtime.ticks, runtime.weather.raining());
        if (overworld && runtime.ticks % 20 == 0)
            world.tickWeather(runtime.weather, runtime.daylight.isDay(), runtime.ticks);
        for (const auto& position : world.takeTntIgnitions())
            runtime.entities.primeTnt(position, 4, false);
    }
}


void GameSession::sendLanChunks() {
    for (auto& entry : guests) {
        auto& guest = *entry.second;
        const auto peer = lanHost.peers().find(entry.first);
        if (peer == lanHost.peers().end() || !peer->second.chunks) continue;
        auto& runtime = *simulations[static_cast<size_t>(guest.profile.dimension)];
        const auto position = guest.player.getPosition();
        const int cx = World::worldToChunkX(position.x), cz = World::worldToChunkZ(position.z);
        auto inRange = [&](int x, int z) {
            const int64_t dx = static_cast<int64_t>(x) - cx, dz = static_cast<int64_t>(z) - cz;
            return std::abs(dx) <= guest.radius && std::abs(dz) <= guest.radius && dx * dx + dz * dz <= guest.radius * guest.radius;
        };
        for (auto it = guest.sentChunkRevisions.begin(); it != guest.sentChunkRevisions.end();) {
            if (!inRange(it->first.first, it->first.second)) it = guest.sentChunkRevisions.erase(it);
            else ++it;
        }
        std::vector<const Chunk*> available;
        for (const auto* chunk : runtime.world.getSimulationChunks())
            if (chunk->generated.load() && inRange(chunk->cx, chunk->cz)) available.push_back(chunk);
        std::sort(available.begin(), available.end(), [&](const Chunk* a, const Chunk* b) {
            const int64_t ax = static_cast<int64_t>(a->cx) - cx, az = static_cast<int64_t>(a->cz) - cz;
            const int64_t bx = static_cast<int64_t>(b->cx) - cx, bz = static_cast<int64_t>(b->cz) - cz;
            return ax * ax + az * az != bx * bx + bz * bz ? ax * ax + az * az < bx * bx + bz * bz :
                std::tie(a->cx, a->cz) < std::tie(b->cx, b->cz);
        });
        size_t sent = 0;
        for (const auto* chunk : available) {
            if (sent >= 2 || peer->second.chunks->queuedBytes() > Lan::MAX_PAYLOAD) break;
            const auto key = std::make_pair(chunk->cx, chunk->cz);
            const auto previous = guest.sentChunkRevisions.find(key);
            const uint64_t revision = chunk->blockRevision();
            if (previous != guest.sentChunkRevisions.end() && previous->second == revision) continue;
            const Lan::ChunkAddress address{guest.profile.dimension, chunk->cx, chunk->cz};
            Lan::Message message;
            const auto delta = previous == guest.sentChunkRevisions.end() ? std::nullopt :
                chunkJournal.since(address, previous->second, revision);
            if (delta && !delta->empty()) {
                message.type = Lan::MessageType::ChunkDelta;
                message.payload = Lan::encodeChunkDelta({address, guest.chunkEpoch, previous->second, revision, *delta});
            } else {
                message.type = Lan::MessageType::ChunkSnapshot;
                Lan::ChunkSnapshot snapshot{address, guest.chunkEpoch, revision, {}};
                chunk->copyRawBlocks(snapshot.blocks);
                message.payload = Lan::encodeChunkSnapshot(snapshot);
            }
            if (!lanHost.send(entry.first, std::move(message), true)) break;
            guest.sentChunkRevisions[key] = revision;
            ++sent;
        }
    }
}


void GameSession::sendLanStates() {
    for (auto& entry : guests) {
        auto& guest = *entry.second;
        if (lanNow - guest.lastStateSent < .05) continue;
        const auto& runtime = *simulations[static_cast<size_t>(guest.profile.dimension)];
        Lan::AuthorityState state;
        state.dimension = guest.profile.dimension; state.epoch = guest.chunkEpoch; state.tick = runtime.ticks;
        state.inputAcknowledged = guest.appliedInputSequence; state.phase = runtime.daylight.phase();
        state.rules = worldMetadata.gameRules; state.difficulty = worldMetadata.difficulty;
        state.dayDuration = worldMetadata.dayNightDurationSeconds; state.weather = runtime.weather.saveState();
        state.dead = guest.dead; state.loading = guest.loading;
        state.sleepState = guest.sleep; state.bed = guest.bed; state.sleepFacing = guest.sleepFacing;
        state.self = playerView(entry.first, guest.profile.identity.nickname, guest.player, guest.fishing.view(), guest.sleepFacing);
        if (dimension == guest.profile.dimension) state.others.push_back(playerView(0, hostIdentity.nickname, player, fishing.view(), sleepFacingDirection));
        for (const auto& other : guests) {
            if (other.first == entry.first || other.second->profile.dimension != guest.profile.dimension) continue;
            state.others.push_back(playerView(other.first, other.second->profile.identity.nickname, other.second->player, other.second->fishing.view(), other.second->sleepFacing));
        }
        state.miningProgress = guest.player.getMiningProgress();
        if (state.miningProgress > 0) state.miningTarget = guest.player.getHighlightedBlock();
        state.roster = roomRoster();
        state.window = authorityWindow(guest.player, *simulations[static_cast<size_t>(guest.profile.dimension)], guest.window, guest.windowView);
        state.inventory = guest.player.inventory();
        const auto& stats = guest.player.survivalStats();
        state.health = stats.health(); state.hunger = stats.hunger(); state.saturation = stats.saturation();
        state.exhaustion = stats.exhaustion(); state.foodTimer = stats.foodTickTimer();
        state.actionAcknowledged = guest.actionAcknowledged;
        state.inventoryRevision = state.window.revision; state.cursor = guest.window.cursor(); state.crafting = guest.window.crafting();
        if (lanHost.send(entry.first, {Lan::MessageType::PlayerState, 0, Lan::encodeAuthorityState(state)})) guest.lastStateSent = lanNow;
    }
}

std::string GameSession::lanNickname() const {
    try { return Lan::ProfileStore::localIdentity(dataDirectory).nickname; }
    catch (const std::exception& error) { LOG_ERROR(error.what()); return "Player"; }
}
bool GameSession::setLanNickname(const std::string& nickname) {
    try {
        if (!Lan::ProfileStore::setLocalNickname(dataDirectory, nickname)) return false;
        if (hostingLan()) hostIdentity.nickname = nickname;
        return true;
    } catch (const std::exception& error) { lanFailure = error.what(); LOG_ERROR(error.what()); return false; }
}

void GameSession::sendLanEntities() {
    for (auto& entry : guests) {
        auto& guest = *entry.second;
        if (lanNow - guest.lastEntitiesSent < .1) continue;
        const auto& runtime = *simulations[static_cast<size_t>(guest.profile.dimension)];
        const auto position = guest.player.getPosition();
        const int64_t cx = World::worldToChunkX(position.x), cz = World::worldToChunkZ(position.z);
        const auto inRange = [&](glm::dvec3 p) {
            const int64_t dx = World::worldToChunkX(p.x) - cx, dz = World::worldToChunkZ(p.z) - cz;
            return std::abs(dx) <= guest.radius && std::abs(dz) <= guest.radius && dx * dx + dz * dz <= guest.radius * guest.radius;
        };
        std::vector<const Entity*> visible;
        for (const auto& entity : runtime.entities.entities()) if (entity.health > 0 && inRange(entity.position)) visible.push_back(&entity);
        std::sort(visible.begin(), visible.end(), [&](const Entity* a, const Entity* b) {
            const auto da = glm::distance(a->position, position), db = glm::distance(b->position, position);
            return da == db ? a->id < b->id : da < db;
        });
        if (visible.size() > Lan::MAX_VISIBLE_ENTITIES) visible.resize(Lan::MAX_VISIBLE_ENTITIES);
        Lan::EntityBatch batch; batch.dimension = guest.profile.dimension; batch.epoch = guest.chunkEpoch; batch.tick = runtime.ticks;
        for (const auto* entity : visible) {
            EntitySnapshot snapshot;
            snapshot.id = entity->id; snapshot.type = entity->type; snapshot.position = entity->position;
            snapshot.velocity = entity->velocity; snapshot.locomotion = entity->locomotionVelocity; snapshot.facing = entity->facing;
            snapshot.health = entity->health; snapshot.age = entity->ageSeconds; snapshot.hurt = entity->hurtFlashSeconds; snapshot.burning = entity->burningSeconds;
            snapshot.item = entity->item; snapshot.seed = entity->behaviorSeed; snapshot.inGround = entity->inGround;
            snapshot.playerOwned = entity->playerOwned; snapshot.sleeping = entity->sleeping; snapshot.attacking = entity->attackPending;
            snapshot.villager = entity->villager; batch.entities.push_back(std::move(snapshot));
        }
        for (const auto& dead : runtime.entities.deadEntityRenders()) if (inRange(dead.position) && batch.deaths.size() < Lan::MAX_VISIBLE_DEATHS)
            batch.deaths.push_back({dead.id, dead.type, dead.position, dead.velocity, dead.facing, dead.behaviorSeed, dead.elapsed});
        if (lanHost.send(entry.first, {Lan::MessageType::EntityState, 0, Lan::encodeEntityBatch(batch)})) guest.lastEntitiesSent = lanNow;
    }
}

void GameSession::enqueueLanChat(Lan::ChatMessage message) {
    lanChat.push_back(std::move(message));
    while (lanChat.size() > 100) lanChat.pop_front();
}
void GameSession::broadcastLanChat(const Lan::ChatMessage& message) {
    const auto payload = Lan::encodeChat(message);
    enqueueLanChat(message);
    for (const auto& guest : guests) (void)lanHost.send(guest.first, {Lan::MessageType::Chat, 0, payload});
}
std::vector<Lan::ChatMessage> GameSession::takeLanChat() {
    std::vector<Lan::ChatMessage> messages(lanChat.begin(), lanChat.end());
    lanChat.clear(); return messages;
}
bool GameSession::sendLanChat(const std::string& text) {
    if (!hostingLan() && !lanWorldReady()) return false;
    hostChatTokens = std::min(5.0f, hostChatTokens + static_cast<float>(std::max(0.0, lanNow - hostChatLast)));
    hostChatLast = lanNow;
    if (hostChatTokens < 1) return false;
    try {
        Lan::ChatMessage message{Lan::ChatKind::Message, {}, text};
        const auto payload = Lan::encodeChat(message);
        if (lanJoining) {
            if (!lanClient.send({Lan::MessageType::Chat, 0, payload})) return false;
        } else {
            message.nickname = hostIdentity.nickname; broadcastLanChat(message);
        }
        hostChatTokens -= 1; return true;
    } catch (const Lan::ProtocolError&) { return false; }
}

Platform::LanAdvertisement GameSession::lanAdvertisement() const {
    return {hostIdentity.id, worldMetadata.displayName, Config::GAME_VERSION, lanPort(), Lan::PROTOCOL_VERSION,
        worldMetadata.generationVersion, static_cast<uint8_t>(guests.size() + 1), static_cast<uint8_t>(lanHost.capacity()), lanPvp, std::to_string(lanCompatibility().contentSignature)};
}

std::vector<Lan::RoomPlayer> GameSession::roomRoster() const {
    if (lanJoining) return replicaRoster;
    if (!hostingLan()) return {};
    std::vector<Lan::RoomPlayer> result{{0, hostIdentity.nickname, dimension}};
    for (const auto& guest : guests) result.push_back({guest.first, guest.second->profile.identity.nickname, guest.second->profile.dimension});
    return result;
}
