#include "plugins/ContentRegistry.h"
#include "plugins/Runtime.h"
#include "app/GameSession.h"

#include "Config.h"
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
    : simulations{{std::make_unique<DimensionSimulation>(), nullptr}},
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
    return worldCatalog.create(name, seed, mode, difficulty, cheats, type);
}

bool GameSession::deleteWorld(const std::string& id) {
    return worldCatalog.deleteWorld(id);
}

void GameSession::setOverworldBedSpawn(const glm::ivec3& bed) {
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
        if (occupied) simulations[index]->daylight.update(dt, worldMetadata.dayNightDurationSeconds,
                worldMetadata.gameRules.boolean(GameRuleId::AdvanceTime));
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
    if (simulations[1]) simulations[1]->entities.shareModels(simulations[0]->entities);
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
        if (input.held(InputAction::Sneak)) replicaInput.buttons |= Lan::InputButtons::Sneak;
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
    auto& runtime = simulations.at(static_cast<size_t>(target));
    if (!runtime) {
        runtime = std::make_unique<DimensionSimulation>();
        runtime->world.setThreadPool(&threadPool);
        runtime->world.configureLod(lodSettings);
        runtime->entities.shareModels(simulations[0]->entities);
    }
    if (!runtime->initialized) {
        if (target == DimensionId::Heaven && saveStore)
            runtime->store = std::make_unique<SaveStore>(
                saveStore->worldDirectory() / "dimensions" / "heaven");
        SaveStore* store = target == DimensionId::Heaven ? runtime->store.get() : saveStore.get();
        runtime->world.resetForNewSeed(worldMetadata.seed, worldMetadata.worldType, target);
        runtime->world.setGameRules(worldMetadata.gameRules);
        runtime->world.setSaveStore(store);
        if (lanJoining && !replicaLodRoot.empty()) runtime->world.setReplicaLodCache(
            replicaLodRoot / "r5" / ("d_" + std::to_string(static_cast<int>(target))));
        runtime->entities.setSaveStore(store);
        runtime->entities.setNaturalSpawningEnabled(target == DimensionId::Overworld);
        if (target == DimensionId::Overworld) {
            runtime->entities.loadEntities(worldMetadata.entities);
            runtime->ticks = worldMetadata.worldTicks;
            runtime->daylight.setPhase(worldMetadata.overworldDayPhase);
            runtime->weather.reset(worldMetadata.seed, worldMetadata.weather);
        } else {
            runtime->ticks = worldMetadata.heaven.worldTicks;
            runtime->daylight.setPhase(worldMetadata.heaven.dayPhase);
            runtime->weather.reset(worldMetadata.seed, WeatherSaveState{});
            runtime->weather.setWeather(WeatherType::Clear);
        }
        runtime->initialized = true;
        if (hostingLan()) runtime->world.setBlockMutationCallback([this, target](int x, int z, uint64_t revision, uint32_t slot, BlockId block, bool persistent) {
            chunkJournal.record({target, x, z}, revision, {slot, static_cast<uint16_t>(block)});
            if (persistent) invalidateLanLod(target, x, z);
        });
    }
    return *runtime;
}

SaveStore* GameSession::activeDataStore() const {
    return dimension == DimensionId::Heaven ? simulation().store.get() : saveStore.get();
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
    // Drain pending generation/cache/entity I/O before destroying either store.
    for (auto& runtime : simulations) {
        if (!runtime) continue;
        runtime->world.resetForNewSeed(worldMetadata.seed, worldMetadata.worldType);
        runtime->world.setSaveStore(nullptr);
        runtime->entities.clear();
        runtime->entities.setSaveStore(nullptr);
        runtime->initialized = false;
        runtime->store.reset();
    }
    dimension = DimensionId::Overworld;
    player.bindWorld(world(), &entities());
    player.setAuthority(true);
    simulations[1].reset();
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
                travelLanPlayer(guest, DimensionId::Overworld, true);
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

    if (dimension == DimensionId::Heaven) weather().setWeather(WeatherType::Clear);
    if (dimension == DimensionId::Heaven) saveActiveDimensionState();
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
        LOG_INFO("Executed local command " << (std::array<const char*, 9>{
            "/help", "/gamemode", "/tp", "/time", "/gamerule", "/weather", "/locate biome", "/locate structure", "/give"
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
        if (dimension == DimensionId::Heaven) {
            message(localization.text("message.heaven_weather_clear"));
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
        dimension != DimensionId::Overworld) {
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
        if (dimension == DimensionId::Overworld && worldMetadata.gameRules.boolean(GameRuleId::AdvanceWeather))
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
    if (lanJoining || !saveStore || target == dimension) return false;
    if (hostingLan()) closeAuthorityWindow(player, simulation(), hostWindow, hostWindowView);
    saveActiveDimensionState();
    updateSaveMetadata();
    saveStore->saveMetadata(worldMetadata);
    ensureDimension(target);
    dimension = target;
    worldMetadata.activeDimension = target;
    loadingReason = target == DimensionId::Heaven
        ? LoadingReason::EnteringHeaven : LoadingReason::ReturningOverworld;
    player.bindWorld(world(), &entities());
    loadActiveDimensionState();
    if (target == DimensionId::Heaven &&
        !worldMetadata.heaven.hasSafePosition) {
        // Match direct world loading: choose the deterministic island spawn
        // before constructing the first streaming target. Otherwise loading
        // begins around the placeholder position and shifts near completion.
        const glm::dvec3 spawn = world().findSafeSpawn();
        player.setPosition(spawn);
        worldMetadata.heaven.playerPosition = spawn;
    }
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
    if (dimension != DimensionId::Heaven ||
        player.getPosition().y >= static_cast<double>(Config::WORLD_MIN_Y - 2))
        return false;
    glm::dvec3 heavenReturnPosition{0.0};
    if (worldMetadata.heaven.hasSafePosition) {
        const glm::ivec3& safe = worldMetadata.heaven.safePosition;
        heavenReturnPosition = {safe.x + 0.5, safe.y + 0.01, safe.z + 0.5};
    } else {
        heavenReturnPosition = world().findSafeSpawn();
    }
    if (!switchDimension(DimensionId::Overworld, loadingStarted)) return false;
    // switchDimension saves the position that triggered the void return.
    // Replace it with the last grounded Heaven location so the next visit
    // cannot resume below the world and immediately fall out again.
    worldMetadata.heaven.playerPosition = heavenReturnPosition;
    if (feedback.dimensionLoading) feedback.dimensionLoading();
    const std::optional<glm::ivec3> bed = loadValidOverworldBed();
    if (bed) {
        const float support = blockCollisionHeight(
            world().getBlock(bed->x, bed->y, bed->z));
        player.setPosition(glm::dvec3(bed->x + 0.5, bed->y + support + 0.001,
                                      bed->z + 0.5));
    } else {
        player.setPosition(world().findSafeSpawn());
    }
    world().update(player.getPosition());
    world().enqueueGeneration();
    updateSaveMetadata();
    if (saveStore) saveStore->saveMetadata(worldMetadata);
    if (feedback.sleepEnded) feedback.sleepEnded();
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
    const bool wasHeaven = dimension == DimensionId::Heaven;
    if (wasHeaven)
        (void)switchDimension(DimensionId::Overworld, loadingStarted);
    const std::optional<glm::ivec3> validBed = wasHeaven
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
        for (auto& entry : guests) if (entry.second->profile.dimension == DimensionId::Overworld)
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
        Lan::GameEvent remote; remote.kind = Lan::EventKind::Lightning; remote.dimension = DimensionId::Overworld; remote.position = strike;
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
    if (dimension == DimensionId::Overworld) {
        worldMetadata.playerPosition = player.getPosition();
        worldMetadata.worldTicks = survivalTicks();
        worldMetadata.overworldDayPhase = dayNightCycle().phase();
    } else {
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
    return {lanJoining?lanClient.peerId():0,static_cast<uint32_t>(dimension),lanJoining?MC_CLIENT:hostingLan()?MC_HOST:MC_LOCAL};
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
    world().resetForNewSeed(worldMetadata.seed,worldMetadata.worldType,dimension);
    entities().clear();fishing.cancel();detachSaveStore();terrainGenerated=false;
}
