#include "plugins/ContentRegistry.h"
#include "plugins/Runtime.h"
#include "app/GameSession.h"

#include "Config.h"
#include "player/PluginLanding.h"
#include "debug/Log.h"
#include "game/SurvivalSession.h"
#include "game/InventoryInteraction.h"
#include "game/Item.h"
#include "game/Command.h"
#include "game/Localization.h"
#include "renderer/GameRenderer.h"
#include "world/WorldGenContext.h"

#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <chrono>

GameSession::GameSession(const std::filesystem::path& savesDirectory)
    : simulations([]{std::vector<std::unique_ptr<DimensionSimulation>> v(2);v[0]=std::make_unique<DimensionSimulation>();return v;}()),
      player(world()), dataDirectory(savesDirectory.parent_path()), worldCatalog(savesDirectory) {
    world().setThreadPool(&threadPool);
    player.setEntityManager(&entities());
    bindLanFeedback(player, 0);
}

GameSession::LoadingSnapshot GameSession::loadingSnapshot() const {
    const StreamingProgress progress = loadingGenerationComplete
        ? world().loadingProgress() : world().generationProgress();
    const float chunkFraction = progress.total == 0 ? 0.0f :
        static_cast<float>(progress.completed) /
        static_cast<float>(progress.total);
    const float lodFraction = world().lodCoverageFraction();
    LoadingPhase phase = loadingGenerationComplete
        ? LoadingPhase::PreparingChunks : LoadingPhase::Chunks;
    if (loadingGenerationComplete && progress.total > 0 &&
        progress.completed == progress.total && world().lodEnabled())
        phase = LoadingPhase::DistantTerrain;
    return {progress,
        loadingGenerationComplete
            ? 0.75f + chunkFraction * 0.15f + lodFraction * 0.10f
            : chunkFraction * 0.75f,
        phase, phase == LoadingPhase::DistantTerrain
            ? lodFraction : chunkFraction,
        loadingNewWorld, loadingReason};
}

std::vector<WorldSummary> GameSession::listWorlds() const {
    return worldCatalog.list();
}

std::string GameSession::createWorld(const std::string& name, uint64_t seed,
                                     GameMode mode, Difficulty difficulty,
                                     bool cheats, WorldType type) {
    if(!Plugins::content().runtimeFault.empty())throw std::runtime_error(Plugins::content().runtimeFault);
    return worldCatalog.create(name, seed, mode, difficulty, cheats, type,creationGeneration);
}

bool GameSession::deleteWorld(const std::string& id) {
    return worldCatalog.deleteWorld(id);
}

void GameSession::setDimensionBedSpawn(const glm::ivec3& bed) {
    worldMetadata.dimensions[static_cast<uint32_t>(dimension)].bedSpawn=bed;
    if (dimension == DimensionId::Overworld) worldMetadata.bedSpawn = bed;
}

void GameSession::updateDaylight(float dt, bool playing) {
    if (lanJoining) return;
    if (!playing) return;
    for (size_t index = 0; index < simulations.size(); ++index) {
        if (!simulations[index]) continue;
        const bool occupied = index == static_cast<size_t>(dimension) || std::any_of(
            guests.begin(), guests.end(), [index](const auto& entry) {
                return static_cast<size_t>(entry.second->profile.dimension) == index;
            });
        const auto policy=worldMetadata.timePolicies.find(static_cast<uint32_t>(index));
        const uint32_t cycle=policy==worldMetadata.timePolicies.end()||!policy->second.first?worldMetadata.dayNightDurationSeconds:policy->second.first;
        const uint32_t advancing=policy==worldMetadata.timePolicies.end()?0:policy->second.second;
        if (occupied) simulations[index]->daylight.update(dt,cycle,advancing==2||(advancing==0&&worldMetadata.gameRules.boolean(GameRuleId::AdvanceTime)));
    }
}

void GameSession::configureVisuals(
    const EnhancedVisualSettings& visuals, VisualQuality quality) {
    particles.setEnhancedVisuals(visuals, quality);
}

void GameSession::configureLod(const LodSettings& settings) {
    lodSettings = settings;
    for (auto& runtime : simulations)
        if (runtime) runtime->world.configureLod(settings);
}

void GameSession::setToggleSneak(bool enabled) { player.setToggleSneak(enabled); }

void GameSession::initializeEntityModels(
    const std::filesystem::path& assetRoot, IGameRenderer& renderer) {
    simulations[0]->entities.initializeModels(assetRoot, renderer);
    for(size_t id=1;id<simulations.size();++id)if(simulations[id])simulations[id]->entities.shareModels(simulations[0]->entities);
}

void GameSession::invalidateGpuMeshes() {
    for (auto& runtime : simulations)
        if (runtime) runtime->world.invalidateGpuMeshes();
}
void GameSession::restoreGpuMeshes(IGameRenderer* renderer) {
    world().restoreGpuMeshes(renderer);
}
void GameSession::emitBlockBreak(const glm::ivec3& position, BlockId block) {
    particles.emitBlockBreak(position, block);
}
void GameSession::emitCriticalHit(const glm::dvec3& position) {
    particles.emitCriticalHit(position);
}
void GameSession::emitSweepAttack(const glm::dvec3& position) {
    particles.emitSweepAttack(position);
}
void GameSession::setBlockBreakCallback(
    std::function<void(const glm::ivec3&, BlockId)> callback) {
    blockBreakFeedback = std::move(callback);
}
void GameSession::setDamageCallback(std::function<void(float)> callback) {
    damageFeedback = std::move(callback);
}
void GameSession::setCombatCallback(
    std::function<void(const CombatFeedback&)> callback) {
    combatFeedback = std::move(callback);
}
void GameSession::setDefenseCallback(
    std::function<void(const DamageOutcome&)> callback) {
    defenseFeedback = std::move(callback);
}
void GameSession::setBedCallback(
    std::function<void(const glm::ivec3&)> callback) {
    player.setBedCallback(std::move(callback));
}
void GameSession::cancelBowCharge() {
    player.cancelBowCharge();
    if (lanJoining && (replicaInput.buttons & (Lan::InputButtons::Attack | Lan::InputButtons::Use))) {
        replicaInput.buttons &= ~(Lan::InputButtons::Attack | Lan::InputButtons::Use);
        sendReplicaInput(true);
    }
}
void GameSession::handleMouseDelta(
    float dx, float dy, float sensitivity, bool invertY) {
    player.handleMouseDelta(dx, dy, sensitivity, invertY);
}
void GameSession::setLocalControl(bool enabled) {
    player.setMouseLocked(enabled);
    if (!enabled && lanJoining) { replicaInput.forward = replicaInput.strafe = 0; replicaInput.buttons = 0; }
}
void GameSession::handleMovement(const InputState& input, float dt) {
    if (!playerDead) player.handleMovement(input, dt);
    if (lanJoining) {
        replicaInput.forward = input.value(InputAction::MoveForward) - input.value(InputAction::MoveBackward);
        replicaInput.strafe = input.value(InputAction::MoveRight) - input.value(InputAction::MoveLeft);
        replicaInput.buttons &= Lan::InputButtons::Attack | Lan::InputButtons::Use;
        if (input.held(InputAction::Jump)) replicaInput.buttons |= Lan::InputButtons::Jump;
        if (player.sneakInput()) replicaInput.buttons |= Lan::InputButtons::Sneak;
        if (input.held(InputAction::Sprint)) replicaInput.buttons |= Lan::InputButtons::Sprint;
    }
}
FishingEnvironment GameSession::fishingEnvironment() {
    return {
        [this](glm::ivec3 p) -> std::optional<BlockId> {
            return world().getLoadedBlock(p.x,p.y,p.z);
        },
        [this](glm::ivec3 p) { return world().hasSkyAccess(p.x,p.y,p.z); },
        [this](glm::ivec3 p) {
            return weather().raining() &&
                world().precipitationAt(p.x,p.y,p.z) == PrecipitationType::Rain;
        }
    };
}
void GameSession::validateFishingRod() {
    if (!fishing.view().active()) return;
    const auto item = player.activeItem();
    if (playerDead || isSleeping() || player.isSpectator() ||
        player.selectedSlot() != fishingSlot || item.empty() ||
        item.id != ItemId::FISHING_ROD || item.damage != fishingRodDamage)
        fishing.cancel();
}
void GameSession::collectFishingEvents() {
    for (const auto& event : fishing.takeEvents()) {
        if (!event.catchItem.empty()) {
            const glm::dvec3 delta = player.getPosition() + glm::dvec3(0,.7,0) - event.position;
            const float distance = static_cast<float>(glm::length(delta));
            const float duration = std::clamp(distance / 12.0f, .2f, 1.2f);
            const glm::vec3 velocity = glm::vec3(delta / static_cast<double>(duration)) +
                glm::vec3(0, .5f * 20.0f * duration, 0);
            entities().spawnItem(event.position, event.catchItem, velocity, .15f);
        }
        if (event.wear && player.isSurvival() && fishingSlot >= 0) {
            auto& rod = player.inventory().slot(static_cast<size_t>(fishingSlot));
            if (rod.id == ItemId::FISHING_ROD) {
                rod.damage = static_cast<uint16_t>(rod.damage + event.wear);
                if (rod.damage >= getItemProps(rod.id).maxDurability) rod.clear();
            }
        }
        fishingFeedback.push_back(event);
    }
}
void GameSession::handleMouseButton(int button, ButtonAction action, bool pluginUseApproved) {
    if (lanJoining) {
        const uint8_t flag = button == MouseButton::Left ? Lan::InputButtons::Attack :
            button == MouseButton::Right ? Lan::InputButtons::Use : 0;
        if (action == ButtonAction::Press) replicaInput.buttons |= flag;
        else if (action == ButtonAction::Release) replicaInput.buttons &= ~flag;
        sendReplicaInput(true);
        return;
    }
    const bool use=button==MouseButton::Right&&action==ButtonAction::Press&&!player.isSpectator();
    if(use&&!pluginUseApproved&&!pluginUse())return;
    struct UseEnd { GameSession& session; bool use; ~UseEnd(){if(use)session.pluginUse(true);} } useEnd{*this,use};
    if(use && !playerDead && !isSleeping() && player.tryUseInteractiveBlock())return;
    validateFishingRod();
    if (button == MouseButton::Right && player.activeItem().id == ItemId::FISHING_ROD) {
        if (action == ButtonAction::Press && player.isMouseLocked() &&
            !playerDead && !isSleeping() && !player.isSpectator()) {
            fishing.update(0,player.getEyePosition(),fishingEnvironment());
            fishingSlot = player.selectedSlot();
            fishingRodDamage = player.activeItem().damage;
            fishing.use(player.getEyePosition(),player.getForward(),player.velocity(),fishingEnvironment());
            player.animateItemUse();
            collectFishingEvents();
        }
        return;
    }
    player.handleMouseButton(button, action);
}
void GameSession::setSelectedSlot(int slot) {
    if (slot >= 0 && slot < 9 && slot != player.selectedSlot()) fishing.cancel();
    player.setSelectedSlot(slot);
}
std::optional<uint64_t> GameSession::useVillagerRay(float reach) {
    return entities().useRay(player.getEyePosition(), player.getForward(), reach);
}
BlockEntity* GameSession::blockEntityAt(const glm::ivec3& position) {
    if (lanJoining) return replicaWindow.container && replicaWindow.position == position ? &*replicaWindow.container : nullptr;
    return world().getBlockEntity(position);
}
const Entity* GameSession::tradeEntity(uint64_t entityId) const {
    return entities().entityById(entityId);
}
bool GameSession::tradeUsable(uint64_t entityId, const glm::dvec3& eye,
                              const glm::vec3& direction, float reach) const {
    return entities().villagerUsable(entityId, eye, direction, reach);
}
void GameSession::executeTrade(uint64_t entityId, uint8_t offerIndex,
                               InventoryModel& inventory) {
    if (player.isSpectator() || player.survivalStats().dead() || offerIndex >= 5 || &inventory != &player.inventory()) return;
    if (!tradeUsable(entityId, player.getEyePosition(), player.getForward(), 3.0f)) return;
    if (lanJoining) {
        const auto* entity = tradeEntity(entityId);
        if (!entity) return;
        Lan::GameAction action; action.kind = Lan::ActionKind::Trade; action.target = entityId;
        action.inventory.argument = offerIndex;
        action.inventory.containerRevision = villagerQuoteRevision(entity->villager);
        queueReplicaAction(std::move(action));
    } else (void)entities().tradeWith(entityId, offerIndex, inventory);
}
void GameSession::giveCreativeItem(ItemId item, int hotbarSlot) {
    if (usesInventoryCommands()) {
        if (hotbarSlot < 0 || hotbarSlot >= 9) return;
        InventoryAction action; action.operation = InventoryOperation::CreativeGrant; action.slot.index = static_cast<uint8_t>(hotbarSlot);
        action.argument = static_cast<uint16_t>(item); submitInventoryAction(action); return;
    }
    if (hotbarSlot < 0 ||
        hotbarSlot >= static_cast<int>(InventoryModel::HOTBAR_SIZE)) return;
    if (hotbarSlot == fishingSlot) fishing.cancel();
    InventoryInteraction::setCreativeItem(
        player.inventory().slot(static_cast<size_t>(hotbarSlot)), item);
}

void GameSession::dropSelectedItem(int hotbarSlot, bool entireStack) {
    if (usesInventoryCommands()) {
        if (hotbarSlot < 0 || hotbarSlot >= 9) return;
        InventoryAction action; action.operation = InventoryOperation::Drop; action.slot.index = static_cast<uint8_t>(hotbarSlot);
        action.alternate = entireStack; submitInventoryAction(action); return;
    }
    if (player.isSpectator() || hotbarSlot < 0 ||
        hotbarSlot >= static_cast<int>(InventoryModel::HOTBAR_SIZE)) return;
    auto& slot = player.inventory().slot(static_cast<size_t>(hotbarSlot));
    ItemStack dropped = slot;
    if (!entireStack) dropped = InventoryInteraction::takeOne(slot);
    else slot.clear();
    validateFishingRod();
    dropInventoryItem(dropped);
}

void GameSession::dropInventoryItem(ItemStack stack) {
    if (lanJoining || stack.empty()) return;
    const glm::vec3 forward = glm::normalize(player.getForward());
    entities().spawnItem(
        player.getEyePosition() + glm::dvec3(forward) * 0.65,
        stack, forward * 4.5f + glm::vec3(0.0f, 1.5f, 0.0f), 0.8f);
}

std::optional<GameSession::PickBlockResult> GameSession::pickBlock(
    int selectedSlot) {
    if (lanJoining) { Lan::GameAction action; action.kind = Lan::ActionKind::PickBlock; queueReplicaAction(action); return std::nullopt; }
    if (player.isSpectator() || selectedSlot < 0 ||
        selectedSlot >= static_cast<int>(InventoryModel::HOTBAR_SIZE))
        return std::nullopt;
    const auto hit = world().raycast(
        player.getEyePosition(), player.getForward(), Config::REACH_DISTANCE);
    if (!hit) return std::nullopt;
    const ItemId item = itemForBlock(world().getBlock(
        hit->blockPos.x, hit->blockPos.y, hit->blockPos.z));
    if (item == ItemId::EMPTY) return std::nullopt;

    auto& items = player.inventory();
    int source = -1;
    for (size_t i = 0; i < InventoryModel::STORAGE_SIZE; ++i) {
        if (items.slot(i).id == item) { source = static_cast<int>(i); break; }
    }
    if (source >= 0 && source < static_cast<int>(InventoryModel::HOTBAR_SIZE)) {
        player.setSelectedSlot(source);
        return PickBlockResult{source, false};
    }
    if (player.gameMode() == GameMode::Creative) {
        InventoryInteraction::setCreativeItem(
            items.slot(static_cast<size_t>(selectedSlot)), item);
    } else if (source >= 0) {
        std::swap(items.slot(static_cast<size_t>(selectedSlot)),
                  items.slot(static_cast<size_t>(source)));
    }
    player.setSelectedSlot(selectedSlot);
    return PickBlockResult{selectedSlot, true};
}

void GameSession::swapOffhand(int hotbarSlot) {
    if (usesInventoryCommands()) {
        if (hotbarSlot < 0 || hotbarSlot >= 9) return;
        InventoryAction action; action.operation = InventoryOperation::SwapOffhand; action.slot.index = static_cast<uint8_t>(hotbarSlot);
        submitInventoryAction(action); return;
    }
    if (player.isSpectator() || hotbarSlot < 0 ||
        hotbarSlot >= static_cast<int>(InventoryModel::HOTBAR_SIZE)) return;
    auto& items = player.inventory();
    if (hotbarSlot == fishingSlot) fishing.cancel();
    std::swap(items.slot(static_cast<size_t>(hotbarSlot)), items.offhand());
    player.cancelBowCharge();
}

void GameSession::dropContainerRemainder(ItemStack stack) {
    if (!lanJoining && !stack.empty()) entities().spawnItem(
        player.getPosition() + glm::dvec3(0.0, 0.5, 0.0), stack);
}

DimensionSimulation& GameSession::ensureDimension(DimensionId target) {
    if(!Plugins::validDimension(target))throw std::runtime_error("Unknown dimension");
    const auto index=static_cast<size_t>(target);if(simulations.size()<=index)simulations.resize(index+1);
    auto& runtime = simulations.at(index);
    if (!runtime) {
        runtime = std::make_unique<DimensionSimulation>();
        runtime->world.setThreadPool(&threadPool);
        runtime->world.configureLod(lodSettings);
        runtime->entities.shareModels(simulations[0]->entities);
    }
    if (!runtime->initialized) {
        if (target != DimensionId::Overworld && saveStore)
            runtime->store = std::make_unique<SaveStore>(
                saveStore->worldDirectory() / "dimensions" / Plugins::dimensionDirectory(target));
        SaveStore* store = target != DimensionId::Overworld ? runtime->store.get() : saveStore.get();
        runtime->world.resetForNewSeed(worldMetadata.seed, worldMetadata.worldType, target,worldMetadata.generation[static_cast<size_t>(target)]);
        runtime->world.setGameRules(worldMetadata.gameRules);
        runtime->world.setSaveStore(store);
        if (lanJoining && !replicaLodRoot.empty()) runtime->world.setReplicaLodCache(
            replicaLodRoot / "r5" / ("d_" + std::to_string(static_cast<int>(target))));
        runtime->entities.setSaveStore(store);
        runtime->entities.setNaturalSpawningEnabled(Plugins::dimensionContent(target).natural);
        if (target == DimensionId::Overworld) {
            runtime->entities.loadEntities(worldMetadata.entities);
            runtime->ticks = worldMetadata.worldTicks;
            runtime->daylight.setPhase(worldMetadata.overworldDayPhase);
            runtime->weather.reset(worldMetadata.seed, worldMetadata.weather);
        } else {
            const auto& state=worldMetadata.dimensions[static_cast<uint32_t>(target)];
            runtime->ticks=state.worldTicks;runtime->daylight.setPhase(state.dayPhase);runtime->weather.reset(worldMetadata.seed,state.weather);
        }
        const auto policy=Plugins::dimensionContent(target).weather;
        if(policy!=MC_WEATHER_NORMAL)runtime->weather.setWeather(static_cast<WeatherType>(policy-1));
        runtime->initialized = true;
        if (hostingLan()) runtime->world.setBlockMutationCallback([this, target](int x, int z, uint64_t revision, uint32_t slot, BlockId block, bool persistent) {
            chunkJournal.record({target, x, z}, revision, {slot, static_cast<uint16_t>(block)});
            if (persistent) invalidateLanLod(target, x, z);
        });
    }
    return *runtime;
}

SaveStore* GameSession::activeDataStore() const {
    return dimension != DimensionId::Overworld ? simulation().store.get() : saveStore.get();
}

void GameSession::flushDimensions(bool synchronous) {
    for (auto& runtime : simulations) {
        if (!runtime || !runtime->initialized) continue;
        runtime->entities.beginChunkEntityAutosave();
        runtime->world.beginModifiedChunkAutosave();
        if (synchronous) {
            runtime->entities.flushChunkEntities(std::numeric_limits<size_t>::max(), true);
            runtime->world.flushModifiedChunks();
        }
    }
}

void GameSession::detachSaveStore() {
    closeLanRoom();
    closeReplica();
    player.clearPluginCooldowns();
    // Drain pending generation/cache/entity I/O before destroying either store.
    for (auto& runtime : simulations) {
        if (!runtime) continue;
        runtime->world.resetForNewSeed(worldMetadata.seed, worldMetadata.worldType,DimensionId::Overworld,worldMetadata.generation[0]);
        runtime->world.setSaveStore(nullptr);
        runtime->entities.clear();
        runtime->entities.setSaveStore(nullptr);
        runtime->initialized = false;
        runtime->store.reset();
    }
    dimension = DimensionId::Overworld;
    player.bindWorld(world(), &entities());
    player.setAuthority(true);
    simulations.resize(1);simulations.resize(2);
    saveStore.reset();
    updateLanInterests();
}

void GameSession::leaveWorld() {
    closeLanRoom();
    if (terrainGenerated) { const Plugins::ActorScope scope(pluginContext());auto e=Plugins::event(MC_WORLD_CLOSE);Plugins::dispatch(e); }
    if (!Plugins::content().runtimeFault.empty()) { abortPluginWorld(); return; }
    fishing.cancel(); fishingFeedback.clear();
    if (terrainGenerated || saveStore) {
        saveActiveDimensionState();
        updateSaveMetadata();
        // Keep direct callers of leaveWorld() as safe as the normal flow
        // controller path: persist the active dimension's loaded entities
        // and block edits before detaching its store.
        if (saveStore) {
            flushDimensions(true);
            saveStore->saveMetadata(worldMetadata);
        }
    }
    detachSaveStore();
    terrainGenerated = false;
    dimension = DimensionId::Overworld;
    loadingReason = LoadingReason::World;
    sleepState = SleepVisualState::Awake;
    player.setSleepingVisual(false, 0.0f);
}

GameMode GameSession::startWorld(
    const std::string& worldId, bool newWorld,
    RuntimeClock::Tick loadingStarted) {
    if(!Plugins::content().runtimeFault.empty())throw std::runtime_error(Plugins::content().runtimeFault);
    if (saveStore) detachSaveStore();
    player.clearPluginCooldowns();
    auto selectedStore = std::make_unique<SaveStore>(worldCatalog.open(worldId));
    auto selectedMetadata = selectedStore->loadMetadata();
    selectedStore->validatePluginFiles();
    if (!WorldGenContext::canLoadGeneration(selectedMetadata.generationVersion))
        throw std::runtime_error("World generation version is incompatible");
    saveStore = std::move(selectedStore);
    worldMetadata = std::move(selectedMetadata);
    dimension = newWorld ? DimensionId::Overworld : worldMetadata.activeDimension;
    ensureDimension(dimension);
    player.bindWorld(world(), &entities());
    loadingReason = dimension == DimensionId::Heaven
        ? LoadingReason::EnteringHeaven : LoadingReason::World;
    const GameMode mode = worldMetadata.gameMode;
    player.configureRules(mode, worldMetadata.difficulty);
    player.inventory() = worldMetadata.inventory;
    player.survivalStats().set(
        worldMetadata.health, worldMetadata.hunger,
        worldMetadata.saturation, worldMetadata.exhaustion,
        worldMetadata.foodTickTimer);
    LOG_INFO("Loading world with seed " << worldMetadata.seed);
    loadActiveDimensionState();
    if(static_cast<uint32_t>(dimension)>=2){const auto landing=dimensionLanding(dimension,player.getPosition());if(!landing)throw std::runtime_error("Dimension has no safe landing");player.setPosition(*landing);}
    if (dimension == DimensionId::Heaven && !worldMetadata.heaven.hasSafePosition) {
        const glm::dvec3 spawn = world().findSafeSpawn();
        player.setPosition(spawn);
        worldMetadata.heaven.playerPosition = spawn;
    }
    resetTransientState(
        newWorld, survivalTicks(), loadingStarted);
    if (newWorld) {
        worldMetadata.activeDimension = dimension;
        const glm::dvec3 spawn = world().findSafeSpawn();
        player.setPosition(spawn);
        worldMetadata.playerPosition = spawn;
        worldMetadata.worldSpawn = glm::ivec3(
            static_cast<int>(std::floor(spawn.x)),
            static_cast<int>(std::floor(spawn.y)),
            static_cast<int>(std::floor(spawn.z)));
    }
    updateLanInterests();
    world().update(player.getPosition());
    world().enqueueGeneration();
    return mode;
}

void GameSession::safeSpawn() {
    const int px = static_cast<int>(std::floor(player.getPosition().x));
    const int pz = static_cast<int>(std::floor(player.getPosition().z));
    for (int wy = Config::WORLD_MAX_Y - 1; wy >= Config::WORLD_MIN_Y; --wy) {
        const BlockId id = world().getBlock(px, wy, pz);
        if (!getBlockProps(id).solid) continue;
        auto position = player.getPosition();
        position.y = static_cast<float>(wy + 1) + 0.01f;
        player.setPosition(position);
        LOG_INFO("Spawn: ground at y=" << wy << ", player at y=" << position.y);
        return;
    }
    LOG_INFO("No ground found at spawn, creating platform");
    for (int y = Config::SEA_LEVEL - 4; y <= Config::SEA_LEVEL - 1; ++y)
        world().setBlock(px, y, pz, BlockId::STONE);
    world().setBlock(px, Config::SEA_LEVEL, pz, BlockId::GRASS);
    auto position = player.getPosition();
    position.y = Config::SEA_LEVEL + 1.01f;
    player.setPosition(position);
}

bool GameSession::advanceLoading(
    IGameRenderer* renderer, RuntimeClock::Tick now) {
    if (lanJoining && !lanWorldReady()) return false;
    if (lanJoining) {
        for (const auto* chunk : world().getActiveChunks())
            if (!replicaRevisions.count({dimension, chunk->cx, chunk->cz})) return false;
    }
    world().update(player.getPosition(), Config::LOADING_CHUNK_LOADS_PER_FRAME,
                 glm::dvec3(player.velocity()));
    // A validated spawn/safe-position correction can move the streaming
    // center after generation first reaches 100%. Keep feeding cache reads
    // and generation during the preparation phase so the newly exposed edge
    // of that target cannot remain permanently requested.
    world().enqueueGeneration();
    if (!loadingGenerationComplete) {
        world().processCompletedGenerations(false);
        const auto generation = world().generationProgress();
        if (world().streamingTargetReady() && generation.total > 0 &&
            generation.completed == generation.total && (hostingLan() || threadPool.idle())) {
            if (loadingNewWorld) {
                world().persistGeneratedChunks();
                safeSpawn();
                const auto position = player.getPosition();
                worldMetadata.playerPosition = position;
                worldMetadata.worldSpawn = glm::ivec3(
                    static_cast<int>(std::floor(position.x)),
                    static_cast<int>(std::floor(position.y)),
                    static_cast<int>(std::floor(position.z)));
                worldMetadata.worldTicks = survivalTicks();
                worldMetadata.weather = weather().saveState();
                saveStore->saveMetadata(worldMetadata);
            }
            if (dimension == DimensionId::Heaven && !lanJoining) {
                ensureHeavenSafePosition();
                updateSaveMetadata();
                if (saveStore) saveStore->saveMetadata(worldMetadata);
            }
            loadingGenerationComplete = true;
        }
    }
    if (loadingGenerationComplete) {
        // The final generation completions can arrive between the unbounded
        // poll above and its completion check. Their lighting handoff is
        // budgeted and may span multiple frames, so keep consuming the queue
        // throughout the preparation phase instead of assuming one pass was
        // sufficient.
        world().processCompletedGenerations(
            true, Config::LOADING_MAIN_BUDGET_MS);
        world().enqueueMeshBuilds(Config::LOADING_MESH_TASKS_IN_FLIGHT);
        world().processCompletedMeshes(
            renderer, Config::LOADING_MESH_UPLOADS_PER_FRAME,
            Config::LOADING_MESH_UPLOAD_BYTES_PER_FRAME);
        // Coverage opens the loading gate. Exact-cache extraction and stale
        // mesh refinements resume in updatePlaying; continuously feeding them
        // here keeps the shared worker pool busy even after coverage is 100%.
        world().updateLod(player.getPosition(), false);
        world().processCompletedLod(renderer, false);
    }
    const auto progress = world().loadingProgress();
    if (!loadingGenerationComplete || !world().streamingTargetReady() ||
        progress.total == 0 || progress.completed != progress.total ||
        (!hostingLan() && !threadPool.idle()) || !world().lodCoverageReady())
        return false;

    terrainGenerated = true;
    { const Plugins::ActorScope scope(pluginContext());auto e=Plugins::event(MC_WORLD_READY);Plugins::dispatch(e); }
    const float seconds = static_cast<float>(RuntimeClock::seconds(
        RuntimeClock::elapsed(worldLoadingStarted, now)));
    LOG_INFO("World render target loaded in " << seconds << "s ("
             << progress.total << " chunks)");
    LOG_INFO("WASD=move | Mouse=look | Space=jump | Ctrl=sprint");
    LOG_INFO("Left-click=break | Right-click=place | ESC=pause");
    return true;
}

void GameSession::updatePlaying(
    float dt, IGameRenderer* renderer, const Feedback& feedback, bool localControl) {
    if (!localControl) setLocalControl(false);
    if (lanJoining) { updateReplica(dt, renderer, feedback); return; }
    const Plugins::ActorScope actorScope(pluginContext());
    auto pluginUpdate=Plugins::event(MC_UPDATE_PRE);pluginUpdate.dt=dt;
    const auto pluginPosition=player.getPosition();for(int i=0;i<3;++i)pluginUpdate.player[i]=pluginPosition[i];
    Plugins::dispatch(pluginUpdate);
    if (!Plugins::content().runtimeFault.empty()) return;
    if (sleepState == SleepVisualState::Entering) {
        sleepProgress = std::min(1.0f, sleepProgress + dt / 0.6f);
        player.setSleepingVisual(true, sleepProgress);
        if (sleepProgress >= 1.0f) sleepState = SleepVisualState::Choosing;
    } else if (sleepState == SleepVisualState::Leaving) {
        sleepProgress = std::min(1.0f, sleepProgress + dt / 0.35f);
        player.setSleepingVisual(true, 1.0f - sleepProgress);
        if (sleepProgress >= 1.0f) {
            sleepState = SleepVisualState::Awake;
            player.setSleepingVisual(false, 0.0f);
            sleepProgress = 0.0f;
            if (feedback.sleepEnded) feedback.sleepEnded();
        }
    }
    if ((sleepState == SleepVisualState::Entering ||
         sleepState == SleepVisualState::Choosing) &&
        !world().validBedFoot(sleepBed))
        cancelSleep(feedback);
    const glm::dvec3 playerEye = player.getEyePosition();
    const int rainX = static_cast<int>(std::floor(playerEye.x));
    const int rainY = static_cast<int>(std::floor(playerEye.y));
    const int rainZ = static_cast<int>(std::floor(playerEye.z));
    const bool rainExposure = weather().raining() &&
        world().precipitationAt(rainX, rainY, rainZ) == PrecipitationType::Rain &&
        world().hasSkyAccess(rainX, rainY, rainZ);
    player.setRainExposure(rainExposure);
    if (feedback.setRainVolume)
        feedback.setRainVolume(
            weather().rainGradient() * (rainExposure ? 0.72f : 0.06f));
    if (!playerDead && (terrainGenerated || !hostingLan())) player.update(dt);
    if (!Plugins::content().runtimeFault.empty()) return;
    validateFishingRod();
    fishing.update(dt,player.getEyePosition(),fishingEnvironment());
    collectFishingEvents();
    for (const auto& event : fishingFeedback) {
        Lan::GameEvent remote; remote.kind = Lan::EventKind::Fishing; remote.dimension = dimension; remote.position = event.position; remote.flags = static_cast<uint8_t>(event.kind);
        broadcastGameEvent(remote);
        if (feedback.playFishing && event.kind != FishingEventKind::Approach)
            feedback.playFishing(event.kind);
        if (event.kind == FishingEventKind::Splash || event.kind == FishingEventKind::Bite ||
            event.kind == FishingEventKind::Approach) particles.emitFishingSplash(event.position,
                event.kind == FishingEventKind::Bite);
        if (event.kind == FishingEventKind::Bite && feedback.rumble) feedback.rumble(.4f,160);
    }
    fishingFeedback.clear();
    particles.update(world(), player.getPosition(), dt, weather().rainGradient(),
                     worldMetadata.seed ^ survivalTicks(), dimension,
                     dayNightCycle().evaluate().daylight);
    updateLanPlayers(dt);
    updateLanInterests();
    size_t fluidUpdatesRemaining = Config::FLUID_UPDATES_PER_FRAME;
    const auto fluidDeadline = std::chrono::steady_clock::now() +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double, std::milli>(Config::FLUID_MAIN_THREAD_BUDGET_MS));
    for (size_t index = 0; index < simulations.size(); ++index) {
        if (!simulations[index] || !simulations[index]->initialized) continue;
        std::vector<EntityPlayerView> views;
        if (index == static_cast<size_t>(dimension))
            views.push_back({0, &player, player.isSurvival() && !playerDead && (terrainGenerated || !hostingLan()), !player.isSpectator() && !playerDead && (terrainGenerated || !hostingLan()), terrainGenerated || !hostingLan()});
        for (auto& entry : guests) {
            auto& guest = *entry.second;
            if (static_cast<size_t>(guest.profile.dimension) == index && !guest.loading)
                views.push_back({entry.first, &guest.player, guest.player.isSurvival() && !guest.dead,
                                 !guest.player.isSpectator() && !guest.dead});
        }
        // Loading guests still need terrain supplied around their spawn.
        auto& runtime = *simulations[index];
        glm::dvec3 center = player.getPosition();
        if (index != static_cast<size_t>(dimension)) {
            const auto found = std::find_if(guests.begin(), guests.end(), [index](const auto& entry) {
                return static_cast<size_t>(entry.second->profile.dimension) == index;
            });
            if (found == guests.end()) continue;
            center = found->second->player.getPosition();
        }
        const bool mayGenerate = index != static_cast<size_t>(dimension) || !player.isSpectator() ||
            worldMetadata.gameRules.boolean(GameRuleId::SpectatorsGenerateChunks) ||
            std::any_of(guests.begin(), guests.end(), [index](const auto& entry) {
                return static_cast<size_t>(entry.second->profile.dimension) == index;
            });
        if (mayGenerate) {
            runtime.world.update(center, 0, index == static_cast<size_t>(dimension) ? glm::dvec3(player.velocity()) : glm::dvec3(0));
            runtime.world.enqueueGeneration();
        }
        runtime.world.processCompletedGenerations();
        runtime.entities.syncChunks();
        if (!views.empty()) simulateDimension(static_cast<DimensionId>(index), views, dt,
            index == static_cast<size_t>(dimension) ? feedback : Feedback{}, fluidUpdatesRemaining, fluidDeadline);
    }
    for (auto& entry : guests) {
        auto& guest = *entry.second;
        if (!guest.dead && guest.player.isSurvival() && guest.player.survivalStats().dead()) {
            guest.dead = true;
            guest.sleep = 0; guest.wantsMorning = false; guest.player.setSleepingVisual(false, 0);
            guest.fishing.cancel();
            auto& runtime = *simulations[static_cast<size_t>(guest.profile.dimension)];
            closeAuthorityWindow(guest.player, runtime, guest.window, guest.windowView);
            if (!worldMetadata.gameRules.boolean(GameRuleId::KeepInventory))
                for (const auto& stack : takeDeathDrops(guest.player.inventory()))
                    runtime.entities.spawnItem(guest.player.getPosition() + glm::dvec3(0, .5, 0), stack);
            if (worldMetadata.gameRules.boolean(GameRuleId::ImmediateRespawn))
                travelLanPlayer(guest, deathDimension(guest.profile.dimension), true);
        }
    }
    for (size_t index = 0; index < simulations.size(); ++index) if (simulations[index]) {
        auto& runtime = *simulations[index];
        for (const auto& position : runtime.entities.takeExplosionEvents()) {
            Lan::GameEvent event; event.kind = Lan::EventKind::Explosion; event.dimension = static_cast<DimensionId>(index); event.position = position;
            if (hostingLan()) broadcastGameEvent(event, false, true);
            else if (index == static_cast<size_t>(dimension) && lanEvents.size() < 256) lanEvents.push_back(event);
        }
        for (const auto& sound : runtime.world.takeInteractionSounds()) {
            Lan::GameEvent event; event.kind = Lan::EventKind::Interaction; event.dimension = static_cast<DimensionId>(index); event.position = sound.position;
            event.flags = (sound.metal ? 1 : 0) | (sound.opening ? 2 : 0) | (sound.button ? 4 : 0);
            if (hostingLan()) broadcastGameEvent(event, false, true);
            else if (index == static_cast<size_t>(dimension) && lanEvents.size() < 256) lanEvents.push_back(event);
        }
    }
    presentLanEvents(feedback); lanEventsSent = 0;
    if (player.isSurvival() && !playerDead && player.survivalStats().dead()) {
        beginPlayerDeath();
        if (worldMetadata.gameRules.boolean(GameRuleId::ShowDeathMessages) && feedback.playerDeathMessage)
            feedback.playerDeathMessage();
        if (worldMetadata.gameRules.boolean(GameRuleId::ImmediateRespawn)) {
            respawn(RuntimeClock{}.now());
            if(worldLoadingPending()&&feedback.dimensionLoading)feedback.dimensionLoading();
        } else if (feedback.playerDied) feedback.playerDied();
    }

    for (auto& lightning : lightningEvents) lightning.seconds -= dt;
    lightningEvents.erase(std::remove_if(
        lightningEvents.begin(), lightningEvents.end(),
        [](const LightningEvent& event) { return event.seconds <= 0.0f; }),
        lightningEvents.end());

    if (!Plugins::content().runtimeFault.empty()) return;
    world().enqueueMeshBuilds();
    world().processCompletedMeshes(renderer, Config::MESH_UPLOADS_PER_FRAME);
    world().updateLod(player.getPosition());
    world().processCompletedLod(renderer);

    saveActiveDimensionState();
    pluginUpdate.kind=MC_UPDATE_POST;Plugins::dispatch(pluginUpdate);
    if (!Plugins::content().runtimeFault.empty()) return;
    autosaveSeconds += dt;
    if (autosaveSeconds >= 30.0f) {
        beginAutosave(feedback.autosaveMetadataError);
        autosaveSeconds = 0.0f;
    }
    processAutosave(feedback.autosaveFlushError);
}

GameSession::CommandResult GameSession::executeCommand(
    const ParsedCommand& command, const Localization& localization,
    RuntimeClock::Tick now) {
    const bool feedbackEnabled = worldMetadata.gameRules.boolean(GameRuleId::SendCommandFeedback);
    CommandResult result = executeCommandImpl(command, localization, now);
    const bool query = command.type == CommandType::GameRule && !command.gameRuleValue;
    const bool feedbackToggle = command.type == CommandType::GameRule &&
        command.gameRule.id == GameRuleId::SendCommandFeedback;
    const bool supportWarning = command.type == CommandType::GameRule &&
        gameRuleDefinition(command.gameRule.id).support != GameRuleSupport::Implemented;
    if (worldMetadata.cheatsEnabled && command.type != CommandType::Help && !query &&
        worldMetadata.gameRules.boolean(GameRuleId::LogAdminCommands))
        LOG_INFO("Executed local command " << (std::array<const char*, 10>{
            "/help", "/gamemode", "/tp", "/time", "/gamerule", "/weather", "/locate biome", "/locate structure", "/give", "/dimension"
        }.at(static_cast<size_t>(command.type))));
    if (worldMetadata.cheatsEnabled && !feedbackEnabled && !query &&
        command.type != CommandType::Help && !feedbackToggle) {
        if (supportWarning && result.messages.size() > 1) result.messages.erase(result.messages.begin());
        else result.messages.clear();
    }
    return result;
}

GameSession::CommandResult GameSession::executeCommandImpl(
    const ParsedCommand& command, const Localization& localization,
    RuntimeClock::Tick now) {
    CommandResult result;
    if (lanJoining) { result.messages.push_back(localization.text("lan.host_commands")); return result; }
    auto message = [&](std::string value) {
        result.messages.push_back(std::move(value));
    };
    if (command.type == CommandType::Help && command.gameRuleHelp) {
        for (const auto& rule : GAME_RULES) {
            if (command.gameRuleHelpSingle && rule.id != command.gameRule.id) continue;
            message(localization.format("message.gamerule_help", {
                std::string(rule.name), gameRuleValueText({rule.type, rule.defaultValue}),
                gameRuleRange({rule.id}), std::string(rule.legacyName)}));
            const std::string support = localization.text(rule.support == GameRuleSupport::Unavailable
                ? "message.gamerule_unavailable" : rule.support == GameRuleSupport::Partial
                ? "message.gamerule_partial" : "message.gamerule_implemented");
            if (command.gameRuleHelpSingle) {
                message(localization.text("gamerule.description." + std::string(rule.name)));
                message(support);
            } else result.messages.back() += " — " + support;
        }
        return result;
    }
    if (command.type == CommandType::Help) {
        message(localization.text("message.help_header"));
        message("/help");
        message("/gamemode 0|1|3");
        message("/give <item> [1..64]");
        message("/tp <x> <y> <z>");
        message("/time set day|night");
        message("/gamerule <rule> [<value>]");
        message("/help gamerule [<rule>]");
        message("/weather clear|rain|thunder");
        message("/dimension list|<dimension>");
        message("/locate biome <biome>");
        message(localization.text("message.help_biomes"));
        message("/locate structure <structure>");
        message(localization.text("message.help_structures"));
        return result;
    }
    if (!worldMetadata.cheatsEnabled) {
        message(localization.text("message.cheats_disabled"));
        return result;
    }
    if(command.type==CommandType::Dimension) {
        if(command.dimensionKey=="list"){for(const auto& entry:Plugins::content().extensions.dimensions)message(entry.second.key+" · "+entry.second.name);return result;}
        try{const auto target=static_cast<DimensionId>(Plugins::resolveDimension(command.dimensionKey));
            if(switchDimension(target,now)){result.teleported=true;message(localization.format("message.dimension_entered",{command.dimensionKey}));}
            else message(localization.text("message.dimension_travel_failed"));
        }catch(const std::exception&){message(localization.text("message.dimension_travel_failed"));}
        return result;
    }
    if (command.type == CommandType::Give) {
        if (!isValidItemId(command.item) || command.item == ItemId::EMPTY ||
            command.itemCount < 1 || command.itemCount > 64) return result;
        const uint32_t remaining = player.inventory().add({command.item, command.itemCount, 0});
        message(localization.format("message.given", {
            localization.itemName(command.item),
            std::to_string(command.itemCount - remaining)}));
        return result;
    }
    if (command.type == CommandType::Gamemode) {
        player.configureRules(command.gameMode, worldMetadata.difficulty);
        worldMetadata.gameMode = command.gameMode;
        result.gameModeChanged = command.gameMode;
        const std::string name = localization.text(
            command.gameMode == GameMode::Survival ? "common.survival" :
            command.gameMode == GameMode::Creative ? "common.creative" :
                                                     "common.spectator");
        message(localization.format("message.mode_changed", {name}));
        return result;
    }
    if (command.type == CommandType::Teleport) {
        const auto& target = command.teleport;
        player.teleport({target.x, target.y, target.z});
        world().update(player.getPosition());
        world().enqueueGeneration();
        worldLoadingStarted = now;
        result.teleported = true;
        message(localization.format("message.teleported", {
            std::to_string(target.x), std::to_string(target.y),
            std::to_string(target.z)}));
        return result;
    }
    if (command.type == CommandType::GameRule) {
        const auto ref = command.gameRule;
        const auto& rule = gameRuleDefinition(ref.id);
        if (command.gameRuleValue) {
            if (!validGameRuleValue(ref.id, *command.gameRuleValue)) {
                message(localization.text("message.gamerule_invalid"));
                return result;
            }
            if (ref.id == GameRuleId::DayNightDuration)
                worldMetadata.dayNightDurationSeconds = static_cast<uint32_t>(command.gameRuleValue->number);
            else {
                worldMetadata.gameRules.set(ref.id, *command.gameRuleValue);
                for (auto& runtime : simulations)
                    if (runtime) runtime->world.setGameRules(worldMetadata.gameRules);
            }
        }
        const auto value = ref.id == GameRuleId::DayNightDuration
            ? GameRuleValue::integer(worldMetadata.dayNightDurationSeconds)
            : worldMetadata.gameRules.get(ref.id);
        message(localization.format(command.gameRuleValue ? "message.gamerule_set" : "message.gamerule_query", {
            std::string(ref.adapter == GameRuleAdapter::FireEnabled ? "doFireTick" :
                ref.adapter == GameRuleAdapter::FireAway ? "allowFireTicksAwayFromPlayer" :
                ref.adapter == GameRuleAdapter::Inverted ? rule.legacyName : rule.name),
            gameRuleValueText(gameRuleQueryValue(ref, value))}));
        if (rule.support != GameRuleSupport::Implemented)
            message(localization.text(rule.support == GameRuleSupport::Partial
                ? "message.gamerule_partial" : "message.gamerule_unavailable"));
        return result;
    }
    if (command.type == CommandType::Time) {
        if (command.time == TimePreset::Day) {
            dayNightCycle().setDay();
            message(localization.text("message.time_day"));
        } else {
            dayNightCycle().setNight();
            message(localization.text("message.time_night"));
        }
        return result;
    }
    if (command.type == CommandType::Weather) {
        if (Plugins::dimensionContent(dimension).weather!=MC_WEATHER_NORMAL && command.weather!=static_cast<WeatherType>(Plugins::dimensionContent(dimension).weather-1)) {
            message(localization.text("message.dimension_weather_fixed"));
            return result;
        }
        weather().setWeather(command.weather);
        message(localization.text(
            command.weather == WeatherType::Clear ? "message.weather_clear" :
            command.weather == WeatherType::Rain ? "message.weather_rain" :
                                                   "message.weather_thunder"));
        return result;
    }
    if (command.type == CommandType::LocateBiome) {
        const glm::dvec3 position = player.getPosition();
        const auto location = world().locateBiome(
            command.biome, static_cast<int>(std::floor(position.x)),
            static_cast<int>(std::floor(position.z)));
        if (!location) {
            message(localization.text("message.locate_not_found"));
            return result;
        }
        const double dx = static_cast<double>(location->x) - position.x;
        const double dz = static_cast<double>(location->y) - position.z;
        message(localization.format("message.locate_found", {
            localization.text("biome." +
                std::string(biomeCommandName(command.biome))),
            std::to_string(location->x), std::to_string(location->y),
            std::to_string(static_cast<int>(
                std::round(std::sqrt(dx * dx + dz * dz))))}));
        return result;
    }
    if (command.type == CommandType::LocateStructure) {
        const glm::dvec3 position = player.getPosition();
        if(!command.pluginStructure.empty()){const auto found=world().generator().locatePluginStructure(command.pluginStructure,static_cast<int>(std::floor(position.x)),static_cast<int>(std::floor(position.z)));if(!found){message(localization.text("message.locate_structure_not_found"));return result;}message(localization.format("message.locate_found",{command.pluginStructure,std::to_string(found->anchor[0]),std::to_string(found->anchor[2]),std::to_string(int(glm::distance(glm::dvec2(position.x,position.z),glm::dvec2(found->anchor[0],found->anchor[2]))))}));return result;}
        const auto location = world().locateStructure(
            command.structure, static_cast<int>(std::floor(position.x)),
            static_cast<int>(std::floor(position.z)));
        if (!location) {
            message(localization.text("message.locate_structure_not_found"));
            return result;
        }
        const double dx = static_cast<double>(location->x) - position.x;
        const double dz = static_cast<double>(location->z) - position.z;
        message(localization.format("message.locate_found", {
            std::string(structureCommandName(command.structure)),
            std::to_string(location->x), std::to_string(location->z),
            std::to_string(static_cast<int>(
                std::floor(std::sqrt(dx * dx + dz * dz))))}));
    }
    return result;
}

bool GameSession::beginSleepAtBed(const glm::ivec3& bed) {
    if (lanJoining || sleepState != SleepVisualState::Awake || playerDead) return false;
    const auto foot = world().validBedFoot(bed);
    if (!foot || !dayNightCycle().isNight()) return false;
    if (entities().hasHostileNear(glm::vec3(*foot), 8.0f))
        return false;

    if (hostingLan()) for (const auto& entry : guests)
        if (entry.second->profile.dimension == dimension && entry.second->sleep != 0 && entry.second->bed == *foot) return false;
    hostWantsMorning = false;
    fishing.cancel();
    sleepBed = *foot;
    BedPart part = BedPart::Foot;
    BedDirection direction = BedDirection::North;
    decodeBed(world().getBlock(foot->x, foot->y, foot->z), part, direction);
    sleepFacingDirection = glm::vec3(bedDirectionOffset(direction));
    const float bedHeight = blockCollisionHeight(
        world().getBlock(foot->x, foot->y, foot->z));
    player.setPosition(glm::dvec3(
        foot->x + 0.5, foot->y + bedHeight + 0.01, foot->z + 0.5));
    sleepState = SleepVisualState::Entering;
    sleepProgress = 0.0f;
    player.setSleepingVisual(true, 0.0f);
    player.cancelBowCharge();
    return true;
}

void GameSession::finishSleep(const Feedback& /*feedback*/) {
    hostWantsMorning = false;
    if (sleepState == SleepVisualState::Awake) return;
    sleepState = SleepVisualState::Leaving;
    sleepProgress = 0.0f;
    player.setSleepingVisual(true, 1.0f);
    player.cancelBowCharge();
}

void GameSession::chooseSleepAction(
    SleepAction action, RuntimeClock::Tick loadingStarted,
    const Feedback& feedback) {
    if (lanJoining) {
        Lan::GameAction command; command.kind = Lan::ActionKind::SleepChoice;
        command.inventory.argument = static_cast<uint16_t>(action); queueReplicaAction(command); return;
    }
    if (sleepState != SleepVisualState::Choosing &&
        sleepState != SleepVisualState::Entering)
        return;
    if (action == SleepAction::TravelToHeaven &&
        (dimension != DimensionId::Overworld||!Plugins::validDimension(DimensionId::Heaven))) {
        finishSleep(feedback);
        return;
    }
    if (action == SleepAction::SleepUntilMorning) {
        if (hostingLan()) { hostWantsMorning = true; (void)trySkipLanNight(dimension); return; }
        if (worldMetadata.gameRules.integer(GameRuleId::PlayersSleepingPercentage) > 100) {
            finishSleep(feedback);
            return;
        }
        if (worldMetadata.gameRules.boolean(GameRuleId::AdvanceTime)) dayNightCycle().resetMorning();
        if (Plugins::dimensionContent(dimension).weather==MC_WEATHER_NORMAL && worldMetadata.gameRules.boolean(GameRuleId::AdvanceWeather))
            weather().setWeather(WeatherType::Clear);
    }
    if (action == SleepAction::TravelToHeaven) {
        finishSleep(feedback);
        if (switchDimension(DimensionId::Heaven, loadingStarted) &&
            feedback.dimensionLoading)
            feedback.dimensionLoading();
        return;
    }
    finishSleep(feedback);
}

void GameSession::cancelSleep(const Feedback& feedback) {
    if (lanJoining) { chooseSleepAction(SleepAction::LeaveBed, 0, feedback); return; }
    if (sleepState == SleepVisualState::Awake) return;
    finishSleep(feedback);
}

bool GameSession::switchDimension(
    DimensionId target, RuntimeClock::Tick loadingStarted) {
    if (lanJoining || !saveStore || target == dimension || !Plugins::validDimension(target)) return false;
    const auto id=static_cast<uint32_t>(target);auto& state=worldMetadata.dimensions[id];
    std::optional<glm::dvec3> preferred;
    if(state.positioned)preferred=state.playerPosition;
    else if(target==DimensionId::Overworld)preferred=worldMetadata.playerPosition;
    else if(target==DimensionId::Heaven&&worldMetadata.heaven.hasSafePosition)preferred=worldMetadata.heaven.playerPosition;
    const auto landing=dimensionLanding(target,preferred);if(!landing)return false;
    return enterDimension(target,loadingStarted,*landing);
}

bool GameSession::enterDimension(DimensionId target,RuntimeClock::Tick loadingStarted,const glm::dvec3& validatedLanding) {
    if(lanJoining||!saveStore||target==dimension||!Plugins::validDimension(target))return false;
    if (hostingLan()) closeAuthorityWindow(player, simulation(), hostWindow, hostWindowView);
    saveActiveDimensionState();
    updateSaveMetadata();
    saveStore->saveMetadata(worldMetadata);
    ensureDimension(target);
    dimension = target;
    worldMetadata.activeDimension = target;
    loadingReason = target == DimensionId::Heaven
        ? LoadingReason::EnteringHeaven : (target==DimensionId::Overworld?LoadingReason::ReturningOverworld:LoadingReason::World);
    player.bindWorld(world(), &entities());
    loadActiveDimensionState();
    player.setPosition(validatedLanding);
    resetTransientState(false, survivalTicks(), loadingStarted);
    sleepState = SleepVisualState::Awake;
    player.setSleepingVisual(false, 0.0f);
    updateLanInterests();
    world().update(player.getPosition());
    world().enqueueGeneration();
    saveActiveDimensionState();
    updateSaveMetadata();
    saveStore->saveMetadata(worldMetadata);
    return true;
}

bool GameSession::handleVoidFall(
    RuntimeClock::Tick loadingStarted, const Feedback& feedback) {
    const auto source=dimension;const auto& definition=Plugins::dimensionContent(source);
    if(definition.voidTarget.empty()||player.getPosition().y>=Config::WORLD_MIN_Y-2)return false;
    const auto target=static_cast<DimensionId>(Plugins::resolveDimension(definition.voidTarget));
    auto& sourceState=worldMetadata.dimensions[static_cast<uint32_t>(source)];
    if(source==DimensionId::Heaven&&worldMetadata.heaven.hasSafePosition&&sourceState.safePosition!=worldMetadata.heaven.safePosition){sourceState.safePosition=worldMetadata.heaven.safePosition;sourceState.safePlayerPosition=glm::dvec3(sourceState.safePosition)+glm::dvec3(.5,.01,.5);sourceState.hasSafePosition=true;}
    auto& targetState=worldMetadata.dimensions[static_cast<uint32_t>(target)];
    std::optional<glm::dvec3> preferred;
    if(targetState.hasSafePosition)preferred=targetState.safePlayerPosition;
    const auto landing=dimensionLanding(target,preferred);if(!landing)return false;
    if(target!=source&&!enterDimension(target,loadingStarted,*landing))return false;
    player.setPosition(*landing);
    if(sourceState.hasSafePosition)sourceState.playerPosition=sourceState.safePlayerPosition;
    if(source==DimensionId::Heaven)worldMetadata.heaven.playerPosition=sourceState.playerPosition;
    if(target==source)resetTransientState(false,survivalTicks(),loadingStarted);
    world().update(player.getPosition());world().enqueueGeneration();updateSaveMetadata();
    if(saveStore)saveStore->saveMetadata(worldMetadata);
    if(feedback.dimensionLoading)feedback.dimensionLoading();
    if(feedback.sleepEnded)feedback.sleepEnded();
    return true;
}

void GameSession::beginPlayerDeath() {
    if (hostingLan()) closeAuthorityWindow(player, simulation(), hostWindow, hostWindowView);
    fishing.cancel();
    playerDead = true;
    const glm::vec3 deathPosition = glm::vec3(
        player.getPosition() + glm::dvec3(0.0, 0.5, 0.0));
    if (!worldMetadata.gameRules.boolean(GameRuleId::KeepInventory))
        for (const auto& stack : takeDeathDrops(player.inventory()))
            entities().spawnItem(deathPosition, stack);
}

void GameSession::respawn(RuntimeClock::Tick loadingStarted) {
    if (lanJoining) { Lan::GameAction action; action.kind = Lan::ActionKind::Respawn; queueReplicaAction(action); return; }
    const auto source=dimension;const auto target=deathDimension(source);
    if(target!=DimensionId::Overworld) {
        const auto bed=worldMetadata.dimensions[static_cast<uint32_t>(target)].bedSpawn;
        const auto landing=dimensionLanding(target,{},bed);if(!landing)return;
        if(target!=dimension&&!enterDimension(target,loadingStarted,*landing))return;
        player.setPosition(*landing);player.survivalStats().resetAfterRespawn();player.extinguish();player.resetDamageImmunity();
        resetTransientState(false,survivalTicks(),loadingStarted);world().update(*landing);world().enqueueGeneration();updateSaveMetadata();return;
    }
    const bool returning = source!=DimensionId::Overworld;
    if (returning&&!switchDimension(target, loadingStarted))return;
    const std::optional<glm::ivec3> validBed = returning
        ? loadValidOverworldBed()
        : (worldMetadata.bedSpawn
            ? world().validBedFoot(*worldMetadata.bedSpawn) : std::nullopt);
    const bool bedValid = validBed.has_value();
    glm::ivec3 spawn = chooseRespawnPosition(worldMetadata.worldSpawn, validBed, bedValid);
    if (!bedValid) {
        const int64_t radius = worldMetadata.gameRules.integer(GameRuleId::RespawnRadius);
        const uint64_t width = static_cast<uint64_t>(radius * 2 + 1);
        for (int attempt = 0; radius > 0 && attempt < 16; ++attempt) {
            const uint64_t hash = WorldGenContext::hashPosition(worldMetadata.seed ^ survivalTicks(),
                worldMetadata.worldSpawn.x, attempt, worldMetadata.worldSpawn.z);
            const int64_t x = static_cast<int64_t>(spawn.x) + static_cast<int64_t>(hash % width) - radius;
            const int64_t z = static_cast<int64_t>(spawn.z) + static_cast<int64_t>((hash >> 32) % width) - radius;
            if (x < INT32_MIN || x > INT32_MAX || z < INT32_MIN || z > INT32_MAX) continue;
            if (!world().getLoadedBlock(static_cast<int>(x), spawn.y, static_cast<int>(z))) continue;
            const int y = world().getSurfaceY(static_cast<int>(x), static_cast<int>(z));
            if (!Config::isValidWorldY(y + 2)) continue;
            const BlockId ground = world().getBlock(static_cast<int>(x), y, static_cast<int>(z));
            if (!isFullCollisionBlock(ground) || isFluid(ground) || ground == BlockId::FIRE ||
                world().getBlock(static_cast<int>(x), y + 1, static_cast<int>(z)) != BlockId::AIR ||
                world().getBlock(static_cast<int>(x), y + 2, static_cast<int>(z)) != BlockId::AIR) continue;
            spawn = {static_cast<int>(x), y, static_cast<int>(z)};
            break;
        }
    }
    const float spawnHeight = bedValid
        ? blockCollisionHeight(world().getBlock(spawn.x, spawn.y, spawn.z)) + 0.001f
        : 1.01f;
    player.setPosition(glm::vec3(spawn) + glm::vec3(0.5f, spawnHeight, 0.5f));
    player.survivalStats().resetAfterRespawn();
    player.extinguish();
    player.resetDamageImmunity();
    world().update(player.getPosition());
    world().enqueueGeneration();
    world().waitForInitialGeneration(150);
    world().processCompletedGenerations();
    playerDead = false;
    player.setSleepingVisual(false, 0.0f);
}

void GameSession::tickLightning(DimensionSimulation& runtime, const Feedback& feedback) {
    auto& weather = runtime.weather;
    auto& world = runtime.world;
    const auto survivalTicks = runtime.ticks;
    if (!weather.thundering()) return;
    auto hash = [](uint64_t value) {
        value ^= value >> 30;
        value *= 0xbf58476d1ce4e5b9ULL;
        value ^= value >> 27;
        value *= 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    };
    for (const Chunk* chunk : world.getSimulationChunks()) {
        if (!chunk->generated.load()) continue;
        uint64_t random = worldMetadata.seed ^ survivalTicks * 131ULL;
        random ^= static_cast<uint64_t>(static_cast<uint32_t>(chunk->cx));
        random ^= static_cast<uint64_t>(static_cast<uint32_t>(chunk->cz)) << 32;
        random = hash(random);
        if (random % 100000 != 0) continue;
        const int x = chunk->worldX() + static_cast<int>((random >> 17) % 16);
        const int z = chunk->worldZ() + static_cast<int>((random >> 25) % 16);
        const int strikeY = world.getSurfaceY(x, z) + 1;
        const glm::ivec3 strike(x, strikeY, z);
        std::vector<EntityPlayerView> affected;
        if (&runtime == &simulation()) affected.push_back({0, &player, !playerDead, !playerDead, terrainGenerated});
        for (auto& entry : guests) if (entry.second->profile.dimension == runtime.world.dimension())
            affected.push_back({entry.first, &entry.second->player, !entry.second->dead, !entry.second->dead, !entry.second->loading});
        runtime.entities.strikeLightning(affected, strike);
        const glm::dvec3 delta = glm::dvec3(strike) - player.getPosition();
        const float distance = static_cast<float>(glm::length(delta));
        if (feedback.playThunder)
            feedback.playThunder(
                std::clamp(static_cast<float>(delta.x) / 32.0f, -1.0f, 1.0f),
                std::clamp(1.0f - distance / 160.0f, 0.18f, 1.0f));
        if (feedback.rumble)
            feedback.rumble(
                std::clamp(1.0f - distance / 48.0f, .12f, .8f), 220);
        if (world.getBlock(x, strikeY, z) == BlockId::AIR ||
            world.getBlock(x, strikeY, z) == BlockId::SNOW_LAYER)
            world.setBlock(x, strikeY, z, BlockId::FIRE);
        Lan::GameEvent remote; remote.kind = Lan::EventKind::Lightning; remote.dimension = runtime.world.dimension(); remote.position = strike;
        broadcastGameEvent(remote);
        if (&runtime == &simulation()) {
            lightningEvents.push_back({glm::dvec3(strike), 0.5f});
            particles.appendLightning(glm::dvec3(strike));
        }
    }
}

void GameSession::resetTransientState(
    bool newWorld, uint64_t worldTicks,
    RuntimeClock::Tick loadingStarted) {
    terrainGenerated = false;
    loadingNewWorld = newWorld;
    loadingGenerationComplete = false;
    worldLoadingStarted = loadingStarted;
    autosaveSeconds = 0.0f;
    autosavePending = false;
    autosaveEntityTurn = true;
    playerDead = false;
    survivalTicks() = worldTicks;
    if (newWorld) survivalWorldTickRemainder() = 0.0f;
    lightningEvents.clear();
    particles.clear();
    fishing.reset(worldMetadata.seed ^ worldTicks ^ 0xf1571a9ULL);
    fishingFeedback.clear();
    fishingSlot = -1;
}

void GameSession::saveActiveDimensionState() {
    auto& state=worldMetadata.dimensions[static_cast<uint32_t>(dimension)];
    state.playerPosition=player.getPosition();state.positioned=true;state.worldTicks=survivalTicks();state.dayPhase=dayNightCycle().phase();state.weather=weather().saveState();
    if(player.onGround()&&player.getPosition().y>=Config::WORLD_MIN_Y){state.safePlayerPosition=player.getPosition();state.safePosition=glm::ivec3(glm::floor(player.getPosition()));state.hasSafePosition=true;}
    if (dimension == DimensionId::Overworld) {
        worldMetadata.playerPosition = player.getPosition();
        worldMetadata.worldTicks = survivalTicks();
        worldMetadata.overworldDayPhase = dayNightCycle().phase();
    } else if(dimension==DimensionId::Heaven) {
        worldMetadata.heaven.playerPosition = player.getPosition();
        worldMetadata.heaven.worldTicks = survivalTicks();
        worldMetadata.heaven.dayPhase = dayNightCycle().phase();
        if (player.onGround()) {
            worldMetadata.heaven.safePosition = glm::ivec3(
                static_cast<int>(std::floor(player.getPosition().x)),
                static_cast<int>(std::floor(player.getPosition().y)),
                static_cast<int>(std::floor(player.getPosition().z)));
            worldMetadata.heaven.hasSafePosition = true;
        }
    }
    worldMetadata.activeDimension = dimension;
}

void GameSession::loadActiveDimensionState() {
    if(static_cast<uint32_t>(dimension)>=2) {
        const auto& state=worldMetadata.dimensions[static_cast<uint32_t>(dimension)];
        player.setPosition(state.playerPosition);return;
    }
    if (dimension == DimensionId::Overworld) {
        player.setPosition(worldMetadata.playerPosition);
    } else {
        glm::dvec3 position = worldMetadata.heaven.playerPosition;
        if (position.y < static_cast<double>(Config::WORLD_MIN_Y)) {
            if (worldMetadata.heaven.hasSafePosition) {
                const glm::ivec3& safe = worldMetadata.heaven.safePosition;
                position = {safe.x + 0.5, safe.y + 0.01, safe.z + 0.5};
            } else {
                position = world().findSafeSpawn();
            }
            worldMetadata.heaven.playerPosition = position;
        }
        player.setPosition(position);
    }
}

std::optional<glm::ivec3> GameSession::loadValidOverworldBed() {
    if (dimension != DimensionId::Overworld || !worldMetadata.bedSpawn)
        return std::nullopt;
    const glm::ivec3 requested = *worldMetadata.bedSpawn;
    // A dimension switch starts the normal target stream around the saved
    // position, not necessarily around the bed. Ensure the bed's chunk and
    // its persisted two-block state are available before validating it.
    world().update(glm::dvec3(requested), Config::LOADING_CHUNK_LOADS_PER_FRAME);
    world().enqueueGeneration();
    // Cache hits become generated only when their main-thread completion is
    // consumed, so give both the I/O lane and the generation lane a few
    // bounded chances before falling back to the world spawn.
    for (int attempt = 0; attempt < 5; ++attempt) {
        world().waitForInitialGeneration(250);
        world().processCompletedGenerations(false);
        if (const auto foot = world().validBedFoot(requested)) return foot;
        world().enqueueGeneration();
    }
    return std::nullopt;
}

void GameSession::ensureHeavenSafePosition() {
    auto safe = [](const World& target, const glm::dvec3& position) {
        const int x = static_cast<int>(std::floor(position.x));
        const int y = static_cast<int>(std::floor(position.y));
        const int z = static_cast<int>(std::floor(position.z));
        return Config::isValidWorldY(y) &&
            isFullCollisionBlock(target.getBlock(x, y - 1, z)) &&
            !isFullCollisionBlock(target.getBlock(x, y, z)) &&
            !isFullCollisionBlock(target.getBlock(x, y + 1, z));
    };

    glm::dvec3 candidate = player.getPosition();
    if (worldMetadata.heaven.hasSafePosition) {
        const glm::ivec3& saved = worldMetadata.heaven.safePosition;
        candidate = glm::dvec3(saved.x + 0.5, saved.y + 0.01,
                               saved.z + 0.5);
    }
    if (!safe(world(), candidate)) candidate = player.getPosition();
    if (!safe(world(), candidate)) candidate = world().findSafeSpawn();
    if (!safe(world(), candidate)) {
        // Deterministic generation normally always provides a candidate, but
        // a heavily edited save can remove every nearby island.  A tiny
        // platform is a recoverable player edit and prevents a permanent
        // void loop.
        const int x = 0;
        const int z = 0;
        const int y = 128;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                world().setBlock(x + dx, y - 1, z + dz, BlockId::STONE);
        candidate = {x + 0.5, y + 0.01, z + 0.5};
    }
    player.setPosition(candidate);
    worldMetadata.heaven.playerPosition = candidate;
    worldMetadata.heaven.safePosition = glm::ivec3(
        static_cast<int>(std::floor(candidate.x)),
        static_cast<int>(std::floor(candidate.y)),
        static_cast<int>(std::floor(candidate.z)));
    worldMetadata.heaven.hasSafePosition = true;
    auto& state=worldMetadata.dimensions[1];state.safePlayerPosition=candidate;state.safePosition=worldMetadata.heaven.safePosition;state.hasSafePosition=true;
}

void GameSession::updateSaveMetadata() {
    saveActiveDimensionState();
    worldMetadata.inventory = player.inventory();
    worldMetadata.health = player.survivalStats().health();
    worldMetadata.hunger = player.survivalStats().hunger();
    worldMetadata.saturation = player.survivalStats().saturation();
    worldMetadata.exhaustion = player.survivalStats().exhaustion();
    worldMetadata.foodTickTimer = player.survivalStats().foodTickTimer();
    if (simulations[0]->initialized) {
        worldMetadata.worldTicks = simulations[0]->ticks;
        worldMetadata.overworldDayPhase = simulations[0]->daylight.phase();
        worldMetadata.weather = simulations[0]->weather.saveState();
    }
    if (simulations[1] && simulations[1]->initialized) {
        worldMetadata.heaven.worldTicks = simulations[1]->ticks;
        worldMetadata.heaven.dayPhase = simulations[1]->daylight.phase();
    }
    for(size_t id=0;id<simulations.size();++id)if(simulations[id]&&simulations[id]->initialized){auto& state=worldMetadata.dimensions[static_cast<uint32_t>(id)];state.worldTicks=simulations[id]->ticks;state.dayPhase=simulations[id]->daylight.phase();state.weather=simulations[id]->weather.saveState();}
    worldMetadata.entities.clear();
}

void GameSession::beginAutosave(const std::function<void()>& onError) {
    if (!saveStore || !terrainGenerated || autosavePending) return;
    try {
        updateSaveMetadata();
        for (auto& entry : guests) saveLanPlayer(*entry.second);
        saveStore->saveMetadata(worldMetadata);
        flushDimensions(false);
        autosavePending = false;
        for (const auto& runtime : simulations)
            if (runtime) autosavePending = autosavePending ||
                runtime->world.hasPendingModifiedChunkSaves() ||
                runtime->entities.hasPendingChunkEntitySaves();
        autosaveEntityTurn = true;
    } catch (const std::exception& error) {
        LOG_ERROR("Autosave metadata failed: " << error.what());
        if (onError) onError();
    }
}

void GameSession::processAutosave(const std::function<void()>& onError) {
    if (!autosavePending) return;
    try {
        // One file per call, alternating dimensions and entity/terrain queues.
        bool flushed = false;
        for (size_t offset = 0; offset < simulations.size(); ++offset) {
            const size_t index = (autosaveDimensionCursor + offset) % simulations.size();
            auto& runtime = simulations[index];
            if (!runtime) continue;
            if (autosaveEntityTurn && runtime->entities.hasPendingChunkEntitySaves()) {
                runtime->entities.flushChunkEntities(1);
                flushed = true;
            } else if (runtime->world.hasPendingModifiedChunkSaves()) {
                runtime->world.flushModifiedChunks(1);
                flushed = true;
            } else if (runtime->entities.hasPendingChunkEntitySaves()) {
                runtime->entities.flushChunkEntities(1);
                flushed = true;
            }
            if (flushed) { autosaveDimensionCursor = (index + 1) % simulations.size(); break; }
        }
        autosaveEntityTurn = !autosaveEntityTurn;
        autosavePending = false;
        for (const auto& runtime : simulations)
            if (runtime) autosavePending = autosavePending ||
                runtime->world.hasPendingModifiedChunkSaves() ||
                runtime->entities.hasPendingChunkEntitySaves();
    } catch (const std::exception& error) {
        autosavePending = false;
        LOG_ERROR("Autosave chunk flush failed: " << error.what());
        if (onError) onError();
    }
}

void GameSession::saveNow(const std::function<void()>& onError) {
    if (!Plugins::content().runtimeFault.empty()) return;
    if (!saveStore || (!terrainGenerated && !hostingLan())) return;
    try {
        updateSaveMetadata();
        for (auto& entry : guests) saveLanPlayer(*entry.second);
        flushDimensions(true);
        saveStore->saveMetadata(worldMetadata);
        autosavePending = false;
    } catch (const std::exception& error) {
        LOG_ERROR("Could not save world: " << error.what());
        if (onError) onError();
    }
}

bool GameSession::pluginUse(bool after) {
    if(lanJoining)return true;
    const Plugins::ActorScope actor(pluginContext());
    if(!terrainGenerated)return !Plugins::dispatcher();
    auto e=Plugins::event(after?MC_USE_POST:MC_USE_PRE);e.item=static_cast<uint16_t>(player.activeItem().id);
    const auto hit=world().raycast(player.getEyePosition(),player.getForward(),Config::REACH_DISTANCE);
    if(hit){e.x=hit->blockPos.x;e.y=hit->blockPos.y;e.z=hit->blockPos.z;e.block=static_cast<uint16_t>(world().getBlock(e.x,e.y,e.z));}
    return Plugins::dispatch(e);
}
Plugins::ActorContext GameSession::pluginContext() const {
    return {lanJoining?lanClient.peerId():0, static_cast<uint32_t>(dimension),
            static_cast<uint32_t>(lanJoining?MC_CLIENT:hostingLan()?MC_HOST:MC_LOCAL)};
}
std::vector<uint64_t> GameSession::pluginPlayers() const {
    if(!terrainGenerated&&!hostingLan())return {};
    std::vector<uint64_t> ids;
    if(lanJoining){for(const auto& member:replicaRoster)ids.push_back(member.id);}
    else {ids.push_back(0);for(const auto& entry:guests)ids.push_back(entry.first);}
    return ids;
}
bool GameSession::pluginPlayerById(uint64_t id,MC_PlayerSnapshot& out,uint32_t& dim) const {
    if(!terrainGenerated&&!hostingLan())return false;
    const Player* selected=nullptr;
    if((!lanJoining&&id==0)||(lanJoining&&id==lanClient.peerId())){selected=&player;dim=static_cast<uint32_t>(dimension);}
    else if(!lanJoining) {
        const auto found=guests.find(id);if(found==guests.end())return false;
        selected=&found->second->player;dim=static_cast<uint32_t>(found->second->profile.dimension);
    } else return false;
    const auto position=selected->getPosition();for(int i=0;i<3;++i)out.position[i]=position[i];
    out.health=selected->survivalStats().health();out.mode=static_cast<uint32_t>(selected->gameMode());return true;
}
bool GameSession::pluginPlayer(MC_PlayerSnapshot& out) const {
    uint32_t dim=0;const auto actor=Plugins::scopedContext()?Plugins::context():pluginContext();
    return pluginPlayerById(actor.player,out,dim);
}
bool GameSession::pluginGetBlock(int32_t x,int32_t y,int32_t z,uint16_t& id) {
    if((!terrainGenerated&&!hostingLan())||y<Config::WORLD_MIN_Y||y>=Config::WORLD_MAX_Y||std::abs(int64_t(x))>100000000||std::abs(int64_t(z))>100000000)return false;
    const auto actor=Plugins::scopedContext()?Plugins::context():pluginContext();
    if(actor.dimension>=simulations.size()||!simulations[actor.dimension])return false;
    if(!lanJoining&&actor.player&&!guests.count(actor.player))return false;
    const auto value=simulations[actor.dimension]->world.getLoadedBlock(x,y,z);if(!value)return false;
    id=static_cast<uint16_t>(*value);return true;
}
bool GameSession::pluginBlockInteraction(uint32_t dim,int32_t x,int32_t y,int32_t z,bool button,bool open) {
    if(lanJoining||dim>=simulations.size()||!simulations[dim]||!Config::isValidWorldY(y))return false;
    const auto actor=Plugins::scopedContext()?Plugins::context():pluginContext();
    if(actor.player&&(actor.dimension!=dim||!guests.count(actor.player)))return false;
    auto& terrain=simulations[dim]->world;if(!terrain.getLoadedBlock(x,y,z))return false;
    return button?terrain.activateButton({x,y,z}):terrain.setDoorOpen({x,y,z},open);
}
bool GameSession::pluginSetBlock(int32_t x,int32_t y,int32_t z,uint16_t id) {
    uint16_t old=0;if(lanJoining||!isValidBlockId(static_cast<BlockId>(id))||!pluginGetBlock(x,y,z,old))return false;
    const auto actor=Plugins::scopedContext()?Plugins::context():pluginContext();
    simulations[actor.dimension]->world.setBlock(x,y,z,static_cast<BlockId>(id));return true;
}
bool GameSession::pluginGiveItem(uint16_t raw,uint32_t count) {
    const auto id=static_cast<ItemId>(raw);if(lanJoining||(!terrainGenerated&&!hostingLan())||!isValidItemId(id)||!raw||!count||count>4096)return false;
    const auto actor=Plugins::scopedContext()?Plugins::context():pluginContext();
    Player* selected=&player;
    if(actor.player){auto found=guests.find(actor.player);if(found==guests.end())return false;selected=&found->second->player;}
    InventoryModel candidate=selected->inventory();const auto stackSize=getItemProps(id).maxStack;
    while(count){const auto amount=static_cast<uint8_t>(std::min(count,uint32_t(stackSize)));if(candidate.add({id,amount,0}))return false;count-=amount;}
    selected->inventory()=std::move(candidate);return true;
}
std::filesystem::path GameSession::pluginWorldDirectory() const {return saveStore?saveStore->worldDirectory():std::filesystem::path{};}

void GameSession::abortPluginWorld() {
    const Plugins::ActorScope actor(pluginContext());
    lanHost.close(Plugins::content().runtimeFault); guests.clear(); guestProfiles.reset();
    if(lanJoining)closeReplica();
    if(terrainGenerated){auto e=Plugins::event(MC_WORLD_CLOSE);Plugins::dispatch(e);}
    // Discard the live session, draining worker/cache work while its stores are
    // alive. No player state, edited chunks or entities are flushed here.
    world().resetForNewSeed(worldMetadata.seed,worldMetadata.worldType,dimension,worldMetadata.generation[static_cast<size_t>(dimension)]);
    entities().clear();fishing.cancel();detachSaveStore();terrainGenerated=false;
}

bool GameSession::pluginSpawnEntity(uint32_t dim,uint16_t type,const double* p) {
    if(lanJoining||!terrainGenerated||dim>=simulations.size()||!simulations[dim]||(Plugins::context().player&&!guests.count(Plugins::context().player)))return false;
    return simulations[dim]->entities.spawnMob(static_cast<EntityType>(type),{p[0],p[1],p[2]});
}
bool GameSession::pluginEntitySnapshot(uint32_t dim,uint64_t id,MC_EntitySnapshot& out,std::string& state) {
    if(!terrainGenerated||dim>=simulations.size()||!simulations[dim])return false;
    return simulations[dim]->entities.pluginSnapshot(id,out,state);
}
bool GameSession::pluginRemoveEntity(uint32_t dim,uint64_t id) {
    return !lanJoining&&terrainGenerated&&(!Plugins::context().player||guests.count(Plugins::context().player))&&dim<simulations.size()&&simulations[dim]&&simulations[dim]->entities.pluginRemove(id);
}
bool GameSession::pluginEntityIntent(uint32_t dim,uint64_t id,const MC_EntityIntent& intent) {
    return !lanJoining&&terrainGenerated&&(!Plugins::context().player||guests.count(Plugins::context().player))&&dim<simulations.size()&&simulations[dim]&&simulations[dim]->entities.pluginIntent(id,intent);
}

DimensionId GameSession::deathDimension(DimensionId source) const {
    const auto& target=Plugins::dimensionContent(source).deathTarget;
    return target.empty()?source:static_cast<DimensionId>(Plugins::resolveDimension(target));
}

std::optional<glm::dvec3> GameSession::dimensionLanding(DimensionId target,std::optional<glm::dvec3> preferred,std::optional<glm::ivec3> bed) {
    auto& runtime=ensureDimension(target);auto& terrain=runtime.world;const auto& definition=Plugins::dimensionContent(target);
    auto valid=[&](glm::dvec3 p) {
        for(int i=0;i<3;++i)if(!std::isfinite(p[i])||std::abs(p[i])>30000000)return false;
        if(p.y<Config::WORLD_MIN_Y||p.y+Config::PLAYER_HEIGHT>=Config::WORLD_MAX_Y)return false;
        terrain.update(p);terrain.enqueueGeneration();
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(1500);
        while(!terrain.isGeneratedAt(static_cast<int>(std::floor(p.x)),static_cast<int>(std::floor(p.z)))&&std::chrono::steady_clock::now()<deadline){terrain.waitForInitialGeneration(10);terrain.processCompletedGenerations(false);terrain.enqueueGeneration();}
        const glm::dvec3 lo=p-glm::dvec3(Config::PLAYER_WIDTH/2,0,Config::PLAYER_WIDTH/2),hi=p+glm::dvec3(Config::PLAYER_WIDTH/2,Config::PLAYER_HEIGHT,Config::PLAYER_WIDTH/2);
        bool support=false;
        for(int z=static_cast<int>(std::floor(lo.z));z<=static_cast<int>(std::floor(hi.z));++z)for(int x=static_cast<int>(std::floor(lo.x));x<=static_cast<int>(std::floor(hi.x));++x){
            if(!terrain.isGeneratedAt(x,z))return false;
            bool columnSupport=false;
            for(int y=static_cast<int>(std::floor(p.y-.05));y<=static_cast<int>(std::floor(hi.y));++y){
                const auto block=terrain.getBlock(x,y,z);if(isFluid(block)||block==BlockId::FIRE)return false;
                const auto boxes=blockCollisionBoxes(block);
                for(uint8_t b=0;b<boxes.count;++b){const auto min=glm::dvec3(x,y,z)+glm::dvec3(boxes.boxes[b].min),max=glm::dvec3(x,y,z)+glm::dvec3(boxes.boxes[b].max);
                    if(hi.x>min.x&&lo.x<max.x&&hi.z>min.z&&lo.z<max.z){if(hi.y>min.y&&lo.y<max.y-0.0001)return false;if(p.y>=max.y&&p.y-max.y<.05)columnSupport=true;}}
            }
            if(!columnSupport)return false;
            support=true;
        }
        return support;
    };
    if(bed){terrain.update(glm::dvec3(*bed));terrain.enqueueGeneration();terrain.waitForInitialGeneration(1500);terrain.processCompletedGenerations(false);const auto foot=terrain.validBedFoot(*bed);if(foot){const auto p=glm::dvec3(*foot)+glm::dvec3(.5,blockCollisionHeight(terrain.getBlock(foot->x,foot->y,foot->z))+.001,.5);if(valid(p))return p;}}
    if(preferred&&valid(*preferred))return preferred;
    if(definition.sampleSpawn) {
        const auto sampled=terrain.generator().sampleDimensionSpawn();
        return sampled&&valid(*sampled)?sampled:std::nullopt;
    }
    glm::dvec3 candidate=definition.spawn==MC_SPAWN_FIXED?glm::dvec3(definition.position[0],definition.position[1],definition.position[2]):
        (target==DimensionId::Overworld?glm::dvec3(worldMetadata.worldSpawn)+glm::dvec3(.5,.01,.5):terrain.findSafeSpawn());
    if(valid(candidate))return candidate;
    if(definition.spawn==MC_SPAWN_SAFE_SURFACE) {
        candidate.y=terrain.getSurfaceY(static_cast<int>(std::floor(candidate.x)),static_cast<int>(std::floor(candidate.z)))+1.01;
        if(valid(candidate))return candidate;
    }
    // Edits may invalidate the generated surface. Search a small loaded apron.
    if(definition.spawn==MC_SPAWN_SAFE_SURFACE)for(int radius=1;radius<=4;++radius)for(int dz=-radius;dz<=radius;++dz)for(int dx=-radius;dx<=radius;++dx){if(std::abs(dx)!=radius&&std::abs(dz)!=radius)continue;const int x=static_cast<int>(std::floor(candidate.x))+dx,z=static_cast<int>(std::floor(candidate.z))+dz;if(!terrain.isGeneratedAt(x,z))continue;glm::dvec3 p(x+.5,terrain.getSurfaceY(x,z)+1.01,z+.5);if(valid(p))return p;}
    return {};
}

bool GameSession::pluginTravel(uint64_t id,uint32_t target) {
    if(lanJoining||!terrainGenerated||!Plugins::validDimension(target))return false;
    if(!id)return switchDimension(static_cast<DimensionId>(target),RuntimeClock{}.now());
    auto it=guests.find(id);if(it==guests.end())return false;
    return travelLanPlayer(*it->second,static_cast<DimensionId>(target));
}

Player* GameSession::pluginActor(uint64_t id,uint32_t& dim){
    if(!terrainGenerated&&!hostingLan()&&!(lanJoining&&lanWorldReady()))return nullptr;
    if((!lanJoining&&id==0)||(lanJoining&&id==lanClient.peerId())){dim=static_cast<uint32_t>(dimension);return &player;}
    if(lanJoining)return nullptr;
    const auto found=guests.find(id);if(found==guests.end())return nullptr;dim=static_cast<uint32_t>(found->second->profile.dimension);return &found->second->player;
}
uint64_t GameSession::pluginActionState(uint64_t id){uint32_t dim=0;const auto* p=pluginActor(id,dim);if(!p)return 0;const auto item=p->activeItem();return 1+(uint64_t(dim)<<32)+(uint64_t(p->selectedSlot())<<24)+(uint64_t(item.id)<<8)+(uint64_t(item.damage)<<48)+item.count;}
bool GameSession::pluginActionCooldown(uint64_t id,const std::string& key,float& out){uint32_t dim=0;auto* p=pluginActor(id,dim);if(!p||!Plugins::content().extensions.actions.count(key))return false;out=p->pluginCooldown(key);return true;}
bool GameSession::pluginActionTarget(uint64_t id,const std::string& key,bool preview,MC_ActionTarget& out,bool* handled){
    if(handled)*handled=false;
    uint32_t dim=0;auto* p=pluginActor(id,dim);const auto found=Plugins::content().extensions.actions.find(key);
    if(!p||found==Plugins::content().extensions.actions.end()||dim>=simulations.size()||!simulations[dim]||p->isSpectator()||(p->gameMode()==GameMode::Survival&&p->survivalStats().health()<=0)||(!preview&&lanJoining))return false;
    const auto& action=found->second;out={};out.size=sizeof(out);out.dimension=dim;
    const auto* item=Plugins::itemBehavior(p->activeItem().id);
    if(item&&!item->dimension.empty()&&Plugins::resolveDimension(item->dimension)!=dim)return true;
    const bool itemBound=item&&item->action==key;
    auto& terrain=simulations[dim]->world;
    const auto hit=terrain.raycast(p->getEyePosition(),p->getForward(),itemBound?action.range:Config::REACH_DISTANCE);
    if(!itemBound){if(!hit)return true;const auto* b=Plugins::blockBehavior(terrain.getBlock(hit->blockPos.x,hit->blockPos.y,hit->blockPos.z));if(!b||b->action!=key)return true;}
    if(p->gameMode()==GameMode::Survival&&action.durability&&!getItemProps(p->activeItem().id).maxDurability)return true;
    if(action.kind==MC_ACTION_TRAVEL){if(p->pluginCooldown(key)>0)return true;out.dimension=Plugins::resolveDimension(action.dimension);out.valid=out.dimension!=dim;return true;}
    if(action.target||action.targetV2){
        struct Reader{World* world;uint32_t samples=0;} reader{&terrain};
        MC_ActionViewV2 view{};view.size=sizeof(view);auto& base=view.base;base.size=sizeof(base);base.dimension=dim;base.player_id=id;base.item=static_cast<uint16_t>(p->activeItem().id);
        const auto eye=p->getEyePosition(),position=p->getPosition();const auto direction=p->getForward();
        for(int i=0;i<3;++i){base.position[i]=position[i];base.eye[i]=eye[i];base.forward[i]=direction[i];}
        base.reader=&reader;base.block=[](void* data,int32_t x,int32_t y,int32_t z,uint16_t* block)->int32_t{
            if(!block||!Config::isValidWorldY(y)||std::abs(int64_t(x))>30000000||std::abs(int64_t(z))>30000000)return -1;
            const auto value=static_cast<Reader*>(data)->world->getLoadedBlock(x,y,z);if(!value)return -1;*block=static_cast<uint16_t>(*value);return 0;};
        view.sneaking=p->sneakInput();view.source=itemBound?0:1;view.phase=preview?0:1;
        if(hit){view.hit_valid=1;for(int i=0;i<3;++i){view.hit[i]=hit->blockPos[i];view.face[i]=hit->faceNormal[i];}view.hit_block=static_cast<uint16_t>(terrain.getBlock(hit->blockPos.x,hit->blockPos.y,hit->blockPos.z));}
        const auto& generator=terrain.generator();view.generation={sizeof(MC_GenerationContext),dim,generator.getSeed(),generator.generationConfig().parameters.c_str()};view.builtin_generation=!generator.customGenerator();
        const auto spawn=generator.isHeaven()&&!generator.customGenerator()?generator.heavenSpawnBlock():worldMetadata.worldSpawn;
        for(int i=0;i<3;++i)view.spawn[i]=spawn[i];
        view.layers=[](void* data,int32_t x,int32_t z,MC_ActionLayer* layers,uint32_t capacity,uint32_t* count)->int32_t{
            auto& r=*static_cast<Reader*>(data);try {if(!count||!layers||std::abs(int64_t(x))>30000000||std::abs(int64_t(z))>30000000||++r.samples>65536)return -1;
            const auto& g=r.world->generator();
            if(g.isHeaven()&&!g.customGenerator()){*count=5;if(capacity<5)return -1;const auto columns=g.sampleHeavenLayers(x,z);for(size_t i=0;i<5;++i)layers[i]={columns[i].bottom,columns[i].top,uint32_t(columns[i].present),uint32_t(columns[i].biome)};return 0;}
            if(g.customGenerator()){const auto spans=g.samplePluginLodColumn(x,z,1);std::vector<MC_ActionLayer> result;for(const auto& span:spans){if(!result.empty()&&result.back().top+1==span.bottom)result.back().top=span.top;else result.push_back({span.bottom,span.top,1,0});}*count=static_cast<uint32_t>(result.size());if(capacity<result.size())return -1;std::copy(result.begin(),result.end(),layers);return 0;}
            *count=1;if(!capacity)return -1;const auto column=g.sampleTerrainColumn(x,z);layers[0]={Config::WORLD_MIN_Y,column.height,1,0};return 0;
            }catch(...){return -1;}
        };
        view.block_flags=[](void*,uint16_t raw,uint32_t* flags)->int32_t{const auto block=static_cast<BlockId>(raw);if(!flags||!isValidBlockId(block))return -1;*flags=(isFullCollisionBlock(block)?1u:0u)|(blockCollisionBoxes(block).count==0?2u:0u);return 0;};
        if(action.targetV2){MC_ActionTargetV2 target{};target.base=out;if(!action.targetV2(view,target)||target.handled>1)return false;out=target.base;if(handled)*handled=target.handled!=0;}
        else if(!action.target(base,out))return false;
    }else if(hit&&hit->faceNormal.y>0){out.valid=1;out.position[0]=hit->blockPos.x+.5;out.position[1]=hit->blockPos.y+blockCollisionHeight(terrain.getBlock(hit->blockPos.x,hit->blockPos.y,hit->blockPos.z))+.001;out.position[2]=hit->blockPos.z+.5;}
    if(out.size<sizeof(out)||out.valid>1||out.dimension!=dim)return false;
    const glm::dvec3 target(out.position[0],out.position[1],out.position[2]);
    const auto origin=action.rangeFromEye?p->getEyePosition():p->getPosition();
    if(out.valid&&(!pluginSafeLanding(terrain,target)||glm::distance(origin,target)>action.range))out.valid=0;
    if(p->pluginCooldown(key)>0||(action.targetV2&&action.useCooldown>0&&p->actionUseCooldown()>0))out.valid=0;
    return true;
}
bool GameSession::pluginUseAction(uint64_t id,const std::string& key){
    MC_ActionTarget target{};bool handled=false;
    if(!pluginActionTarget(id,key,false,target,&handled))return false;
    if(target.valid)(void)pluginExecuteAction(id,key);
    return handled;
}
bool GameSession::pluginExecuteAction(uint64_t id,const std::string& key){
    if(lanJoining)return false;
    uint32_t dim=0;auto* p=pluginActor(id,dim);if(!p)return false;
    MC_ActionTarget target{};target.size=sizeof(target);if(!pluginActionTarget(id,key,false,target)||!target.valid)return false;
    const auto& action=Plugins::content().extensions.actions.at(key);
    const bool done=action.kind==MC_ACTION_TRAVEL?pluginTravel(id,target.dimension):pluginTeleport(id,target.dimension,target.position);
    if(done){p->completePluginAction(key,action.cooldown,action.durability);p->setActionUseCooldown(action.useCooldown);}
    return done;
}
bool GameSession::pluginTeleport(uint64_t id,uint32_t dim,const double* position){
    uint32_t current=0;auto* p=pluginActor(id,current);if(lanJoining||!p||current!=dim||!position||!pluginSafeLanding(simulations[dim]->world,{position[0],position[1],position[2]}))return false;p->teleport({position[0],position[1],position[2]});return true;
}
bool GameSession::pluginDimensionTime(uint32_t dim,MC_DimensionTime& out){
    if((!terrainGenerated&&!hostingLan()&&!(lanJoining&&lanWorldReady()))||dim>=simulations.size()||!simulations[dim])return false;
    out={sizeof(out),dim,0,0,simulations[dim]->daylight.phase()};const auto found=worldMetadata.timePolicies.find(dim);if(found!=worldMetadata.timePolicies.end()){out.cycle_seconds=found->second.first;out.advancing=found->second.second;}return true;
}
bool GameSession::pluginSetDimensionTime(const MC_DimensionTime& time){
    if(lanJoining||!terrainGenerated||time.dimension>=simulations.size()||!simulations[time.dimension])return false;
    worldMetadata.timePolicies[time.dimension]={time.cycle_seconds,time.advancing};simulations[time.dimension]->daylight.setPhase(time.phase);return true;
}
bool GameSession::pluginLocateStructure(uint32_t dim,const std::string& key,int32_t x,int32_t z,uint32_t range,MC_LocatedStructure& out){
    if((!terrainGenerated&&!hostingLan()&&!(lanJoining&&lanWorldReady()))||dim>=simulations.size()||!simulations[dim])return false;
    const auto found=simulations[dim]->world.generator().locatePluginStructure(key,x,z,range);if(!found)return false;out=*found;return true;
}

bool GameSession::pluginEmitParticle(const MC_ParticleEmission& e){
    if((!terrainGenerated&&!hostingLan())||!e.particle||!Plugins::content().extensions.particles.count(e.particle)||(!e.local_only&&lanJoining))return false;
    if(e.dimension==static_cast<uint32_t>(dimension)&&!particles.emitPlugin(e))return false;
    if(!e.local_only)for(const auto& entry:guests){const auto& guest=*entry.second;if(e.dimension!=static_cast<uint32_t>(guest.profile.dimension))continue;Lan::Writer writer;Lan::writeDimension(writer,guest.profile.dimension);writer.u64(guest.chunkEpoch);writer.text(e.particle);writer.u32(e.count);for(double v:e.position)writer.f64(v);lanHost.send(entry.first,{Lan::MessageType::PluginParticle,0,std::move(writer.bytes)});}
    return true;
}
