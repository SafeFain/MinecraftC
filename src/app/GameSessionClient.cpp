#include "app/GameSession.h"
#include "network/EntityProtocol.h"
#include "debug/Log.h"
#include "plugins/ContentRegistry.h"
#include "world/WorldGenContext.h"
#include <algorithm>
#include <cmath>

bool GameSession::joinLanRoom(const std::string& address, uint16_t port, double now) {
    lanFailure.clear();
    if (saveStore || hostingLan()) { lanFailure = "Leave the current world before joining"; return false; }
    closeReplica();
    try {
        const auto identity = Lan::ProfileStore::localIdentity(dataDirectory);
        if (!lanClient.join(address, port, lanCompatibility(), identity, now)) {
            lanFailure = lanClient.error(); return false;
        }
        replicaLodRoot = dataDirectory / "lan-cache" / Lan::randomToken();
        lanJoining = true; lanNow = now;
        loadingNewWorld = false; terrainGenerated = false; loadingGenerationComplete = false;
        worldLoadingStarted = RuntimeClock{}.now();
        return true;
    } catch (const std::exception& error) { lanFailure = error.what(); return false; }
}

void GameSession::closeReplica() {
    if (!replicaLodRoot.empty()) {
        threadPool.waitIdle();
        for (auto& runtime : simulations) if (runtime) runtime->world.setReplicaLodCache({});
        std::error_code error;
        std::filesystem::remove_all(replicaLodRoot, error);
        if (error) LOG_WARN("Could not remove transient LAN LOD cache: " << error.message());
        replicaLodRoot.clear();
    }
    lanClient.close(); lanJoining = false; replicaWorldInfo = replicaOwnerState = false;
    replicaLodRevisions.clear();
    for (auto& runtime : simulations) if (runtime) runtime->world.clearReplicaLod();
    replicaEpoch = 0; replicaInput = {}; replicaLastInputSent = -1;
    replicaChunks.clear(); replicaChunkBytes = 0; replicaRevisions.clear(); replicaRecovery.clear();
    lanEvents.clear();
    replicaRoster.clear(); replicaOthers.clear(); replicaFishing = {}; lanChat.clear(); hostChatTokens = 5; hostChatLast = lanNow;
    replicaDeathNotified = replicaSleepStarted = replicaSleepEnded = false; replicaWindow = {}; replicaActions.clear(); replicaActionInFlight = replicaActionSequence = 0;
}

void GameSession::sendReplicaInput(bool force) {
    if (!lanJoining || !lanWorldReady() || lanClient.state() != Lan::Client::State::Connected ||
        (!force && lanNow - replicaLastInputSent < 1.0 / 30.0)) return;
    replicaInput.dimension = dimension;
    replicaInput.yaw = std::remainder(player.getYaw(), 360.0f); replicaInput.pitch = player.getPitch();
    replicaInput.selected = static_cast<uint8_t>(player.selectedSlot());
    replicaInput.lodRadius = static_cast<uint16_t>(lodSettings.enabled ? lodSettings.distanceChunks : 0);
    replicaInput.radius = static_cast<uint8_t>(std::clamp(Config::RENDER_DISTANCE, 2, 16));
    if (!player.isMouseLocked() || playerDead || isSleeping()) {
        replicaInput.forward = replicaInput.strafe = 0; replicaInput.buttons = 0;
    }
    if (lanClient.send({Lan::MessageType::Input, 0, Lan::encodePlayerInput(replicaInput)})) replicaLastInputSent = lanNow;
}

void GameSession::applyReplicaState(const Lan::AuthorityState& state) {
    if (!replicaWorldInfo || state.self.id != lanClient.peerId()) throw Lan::ProtocolError("Invalid authority recipient");
    const bool initial = !replicaOwnerState || state.epoch != replicaEpoch;
    if (!initial && state.dimension != dimension) throw Lan::ProtocolError("Dimension changed without stream epoch");
    if (initial) {
        dimension = state.dimension;
        ensureDimension(dimension);
        world().setReplicaMode(true); entities().setNaturalSpawningEnabled(false);
        player.bindWorld(world(), &entities()); player.setAuthority(false);
        updateLanInterests();
        replicaEpoch = state.epoch; replicaRevisions.clear(); replicaRecovery.clear();
        replicaLodRevisions.clear(); world().clearReplicaLod();
        resetTransientState(false, state.tick, RuntimeClock{}.now());
        loadingReason = dimension == DimensionId::Heaven ? LoadingReason::EnteringHeaven : LoadingReason::World;
        player.setOrientation(state.self.yaw, state.self.pitch);
    }
    if (state.tick < simulation().ticks) throw Lan::ProtocolError("Authority clock moved backwards");
    simulation().ticks = state.tick; simulation().daylight.setPhase(state.phase);
    simulation().weather.reset(worldMetadata.seed, state.weather);
    worldMetadata.dayNightDurationSeconds = state.dayDuration; worldMetadata.activeDimension = dimension;
    worldMetadata.gameRules = state.rules; worldMetadata.difficulty = state.difficulty;
    for (auto& runtime : simulations) if (runtime) runtime->world.setGameRules(state.rules);
    if (player.gameMode() != state.self.mode || player.difficulty() != state.difficulty || initial) player.configureRules(state.self.mode, worldMetadata.difficulty);
    player.reconcileReplica(state.self.position, state.self.visual.velocity, state.self.visual.grounded,
                            state.self.flying, state.self.visual.pose, initial);
    player.inventory() = state.inventory;
    player.applyReplicaVisualState(state.self.visual);
    player.setReplicaMining(state.miningProgress, state.miningTarget);
    player.survivalStats().set(state.health, state.hunger, state.saturation, state.exhaustion, state.foodTimer);
    playerDead = state.dead;
    if (sleepState == SleepVisualState::Awake && state.sleepState != 0) replicaSleepStarted = true;
    if (sleepState != SleepVisualState::Awake && state.sleepState == 0) replicaSleepEnded = true;
    sleepState = static_cast<SleepVisualState>(state.sleepState); sleepBed = state.bed;
    sleepProgress = state.self.visual.sleepProgress; sleepFacingDirection = state.sleepFacing;
    player.setSleepingVisual(state.self.visual.sleeping, state.self.visual.sleepProgress);
    replicaOthers = state.others; replicaRoster = state.roster; replicaFishing = state.self.fishing;
    replicaWindow = state.window; replicaWindow.revision = state.inventoryRevision;
    replicaWindow.cursor = state.cursor; replicaWindow.crafting = state.crafting;
    if (replicaActionInFlight && state.actionAcknowledged >= replicaActionInFlight) {
        if (!replicaActions.empty()) replicaActions.pop_front();
        replicaActionInFlight = 0;
        player.setSelectedSlot(state.self.selected);
    }
    replicaOwnerState = true;
    sendReplicaAction();
}

void GameSession::pollReplica(double now) {
    lanNow = now;
    try {
        for (auto& received : lanClient.poll(now)) {
            auto& message = received.message;
            if (received.chunks) {
                if (message.type != Lan::MessageType::ChunkSnapshot && message.type != Lan::MessageType::ChunkDelta && message.type != Lan::MessageType::LodColumns)
                    throw Lan::ProtocolError("Unexpected host bulk message");
                if (replicaChunkBytes + message.payload.size() > Lan::MAX_QUEUED_BYTES || replicaChunks.size() >= Lan::MAX_MESSAGES_PER_POLL)
                    throw Lan::ProtocolError("Replica terrain queue is full");
                replicaChunkBytes += message.payload.size(); replicaChunks.push_back(std::move(message));
            } else if (message.type == Lan::MessageType::Environment) {
                if (replicaWorldInfo) throw Lan::ProtocolError("Duplicate world description");
                const auto info = Lan::decodeWorldInfo(message.payload);
                if (info.generationVersion != WorldGenContext::GENERATION_VERSION) throw Lan::ProtocolError("Incompatible world generation");
                worldMetadata = WorldMetadata{}; worldMetadata.displayName = info.name; worldMetadata.seed = info.seed;
                worldMetadata.generationVersion = info.generationVersion; worldMetadata.worldType = info.type;
                worldMetadata.difficulty = info.difficulty; worldMetadata.gameRules = info.rules;
                replicaWorldInfo = true;
            } else if (message.type == Lan::MessageType::PlayerState) applyReplicaState(Lan::decodeAuthorityState(message.payload));
            else if (message.type == Lan::MessageType::Chat) enqueueLanChat(Lan::decodeChat(message.payload));
            else if (message.type == Lan::MessageType::Event) {
                auto event = Lan::decodeGameEvent(message.payload);
                if (event.dimension == dimension && event.epoch == replicaEpoch && lanEvents.size() < 256) lanEvents.push_back(std::move(event));
            }
            else if (message.type == Lan::MessageType::LodInvalidate) {
                const auto update = Lan::decodeLodUpdate(message.payload, false);
                if (update.epoch == replicaEpoch && update.dimension == dimension) {
                    auto found = replicaLodRevisions.find(update.key);
                    if (found != replicaLodRevisions.end()) { found->second.required = std::max(found->second.required, update.revision); found->second.pending = false; }
                }
            }
            else if (message.type == Lan::MessageType::EntityState) {
                const auto batch = Lan::decodeEntityBatch(message.payload);
                if (!lanWorldReady()) throw Lan::ProtocolError("Entity state before bootstrap");
                if (batch.epoch == replicaEpoch && batch.dimension == dimension)
                    entities().applyReplicaSnapshots(batch.entities, batch.deaths);
            } else throw Lan::ProtocolError("Unexpected host control message");
        }
        if (lanClient.state() == Lan::Client::State::Failed) { lanFailure = lanClient.error(); return; }
        if (!lanWorldReady()) return;
        world().update(player.getPosition());
        const int cx = World::worldToChunkX(player.getPosition().x), cz = World::worldToChunkZ(player.getPosition().z);
        const int radius = std::clamp(Config::RENDER_DISTANCE, 2, 16);
        auto inRange = [&](const Lan::ChunkAddress& address) {
            const int64_t dx = static_cast<int64_t>(address.x) - cx, dz = static_cast<int64_t>(address.z) - cz;
            return address.dimension == dimension && std::abs(dx) <= radius && std::abs(dz) <= radius && dx * dx + dz * dz <= radius * radius;
        };
        for (size_t count = 0; count < 2 && !replicaChunks.empty(); ++count) {
            auto message = std::move(replicaChunks.front()); replicaChunks.pop_front(); replicaChunkBytes -= message.payload.size();
            if (message.type == Lan::MessageType::LodColumns) {
                const auto update = Lan::decodeLodUpdate(message.payload, true);
                if (update.epoch != replicaEpoch || update.dimension != dimension) continue;
                auto found = replicaLodRevisions.find(update.key);
                if (found == replicaLodRevisions.end()) continue;
                auto& revision = found->second; revision.pending = false;
                if (update.revision < revision.required || update.revision <= revision.received) continue;
                world().applyReplicaLod(update.key, update.revision, update.columns); revision.received = update.revision;
            } else if (message.type == Lan::MessageType::ChunkSnapshot) {
                const auto snapshot = Lan::decodeChunkSnapshot(message.payload);
                if (snapshot.epoch != replicaEpoch || !inRange(snapshot.address)) continue;
                const auto previous = replicaRevisions.find(snapshot.address);
                if (previous != replicaRevisions.end() && previous->second >= snapshot.revision) continue;
                if (world().installReplicaSnapshot(snapshot.address.x, snapshot.address.z, snapshot.blocks)) {
                    replicaRevisions[snapshot.address] = snapshot.revision; replicaRecovery.erase(snapshot.address);
                }
            } else {
                const auto delta = Lan::decodeChunkDelta(message.payload);
                if (delta.epoch != replicaEpoch || !inRange(delta.address)) continue;
                const auto previous = replicaRevisions.find(delta.address);
                if (previous != replicaRevisions.end() && previous->second == delta.base) {
                    std::vector<std::pair<uint32_t, BlockId>> edits;
                    for (const auto& edit : delta.edits) edits.push_back({edit.index, static_cast<BlockId>(edit.block)});
                    if (world().installReplicaEdits(delta.address.x, delta.address.z, edits)) {
                        previous->second = delta.revision;
                        continue;
                    }
                }
                replicaRevisions.erase(delta.address); replicaRecovery.erase(delta.address);
            }
        }
        for (auto it = replicaRevisions.begin(); it != replicaRevisions.end();) {
            if (!inRange(it->first)) it = replicaRevisions.erase(it); else ++it;
        }
        for (auto it = replicaRecovery.begin(); it != replicaRecovery.end();) {
            if (!inRange(*it)) it = replicaRecovery.erase(it); else ++it;
        }
        size_t requested = 0;
        for (const auto* chunk : world().getActiveChunks()) {
            Lan::ChunkAddress address{dimension, chunk->cx, chunk->cz};
            if (!inRange(address) || replicaRevisions.count(address) || replicaRecovery.count(address)) continue;
            if (requested++ >= 2) break;
            Lan::Writer writer; writer.u8(1); writer.u8(static_cast<uint8_t>(dimension)); writer.u64(replicaEpoch);
            writer.u32(static_cast<uint32_t>(chunk->cx)); writer.u32(static_cast<uint32_t>(chunk->cz));
            if (lanClient.send({Lan::MessageType::ChunkRequest, 0, std::move(writer.bytes)})) replicaRecovery.insert(address);
        }
        sendReplicaInput();
        pollReplicaLod();
    } catch (const std::exception& error) {
        lanFailure = error.what(); lanClient.close();
        LOG_ERROR("LAN replica disconnected: " << error.what());
    }
}

void GameSession::updateReplica(float dt, IGameRenderer* renderer, const Feedback& feedback) {
    if (!lanWorldReady() || lanConnectionFailed()) return;
    presentLanEvents(feedback);
    for (auto& event : lightningEvents) event.seconds -= dt;
    lightningEvents.erase(std::remove_if(lightningEvents.begin(), lightningEvents.end(), [](const LightningEvent& event) { return event.seconds <= 0; }), lightningEvents.end());
    if (feedback.setRainVolume) feedback.setRainVolume(weather().rainGradient() * .72f);
    if (replicaSleepStarted) { replicaSleepStarted = false; if (feedback.sleepStarted) feedback.sleepStarted(); }
    if (replicaSleepEnded) { replicaSleepEnded = false; if (feedback.sleepEnded) feedback.sleepEnded(); }
    if (playerDead && !replicaDeathNotified) {
        replicaDeathNotified = true;
        if (feedback.playerDied) feedback.playerDied();
        if (feedback.playerDeathMessage) feedback.playerDeathMessage();
    }
    if (!playerDead) { replicaDeathNotified = false; player.update(dt); }
    world().update(player.getPosition(), 0, glm::dvec3(player.velocity()));
    world().processCompletedGenerations(); world().enqueueMeshBuilds();
    world().processCompletedMeshes(renderer, Config::MESH_UPLOADS_PER_FRAME);
    world().updateLod(player.getPosition()); world().processCompletedLod(renderer);
    entities().advanceReplicaPresentation(dt);
    particles.update(world(), player.getPosition(), dt, weather().rainGradient(), worldMetadata.seed ^ survivalTicks(),
                     dimension, dayNightCycle().evaluate().daylight);
}
