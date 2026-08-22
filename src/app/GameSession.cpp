#include "app/GameSession.h"

#include "Config.h"
#include "debug/Log.h"
#include "game/SurvivalSession.h"
#include "game/Command.h"
#include "game/Localization.h"
#include "renderer/GameRenderer.h"
#include "world/WorldGenContext.h"

#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>

GameSession::GameSession(const std::filesystem::path& savesDirectory)
    : player(world), entities(world), worldCatalog(savesDirectory) {
    world.setThreadPool(&threadPool);
    player.setEntityManager(&entities);
}

SaveStore* GameSession::activeDataStore() const {
    return dimension == DimensionId::Heaven ? dimensionSaveStore.get()
                                             : saveStore.get();
}

void GameSession::detachSaveStore() {
    world.setSaveStore(nullptr);
    entities.setSaveStore(nullptr);
    dimensionSaveStore.reset();
    saveStore.reset();
}

void GameSession::leaveWorld() {
    if (terrainGenerated) {
        saveActiveDimensionState();
        updateSaveMetadata();
        // Keep direct callers of leaveWorld() as safe as the normal flow
        // controller path: persist the active dimension's loaded entities
        // and block edits before detaching its store.
        if (saveStore) {
            entities.beginChunkEntityAutosave();
            entities.flushChunkEntities(std::numeric_limits<size_t>::max(), true);
            world.beginModifiedChunkAutosave();
            world.flushModifiedChunks();
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
    if (saveStore) detachSaveStore();
    saveStore = std::make_unique<SaveStore>(worldCatalog.open(worldId));
    worldMetadata = saveStore->loadMetadata();
    if (worldMetadata.generationVersion != WorldGenContext::GENERATION_VERSION)
        throw std::runtime_error("World generation version is incompatible");

    dimension = worldMetadata.activeDimension;
    loadingReason = dimension == DimensionId::Heaven
        ? LoadingReason::EnteringHeaven : LoadingReason::World;
    const GameMode mode = worldMetadata.gameMode;
    player.configureRules(mode, worldMetadata.difficulty);
    player.inventory() = worldMetadata.inventory;
    player.survivalStats().set(
        worldMetadata.health, worldMetadata.hunger,
        worldMetadata.saturation, worldMetadata.exhaustion);
    if (dimension == DimensionId::Heaven) {
        dimensionSaveStore = std::make_unique<SaveStore>(
            saveStore->worldDirectory() / "dimensions" / "heaven");
    }
    world.setSaveStore(activeDataStore());
    entities.setSaveStore(activeDataStore());
    entities.setNaturalSpawningEnabled(dimension == DimensionId::Overworld);
    LOG_INFO("Loading world with seed " << worldMetadata.seed);
    world.resetForNewSeed(worldMetadata.seed, worldMetadata.worldType, dimension);
    entities.clear();
    loadActiveDimensionState();
    if (dimension == DimensionId::Heaven &&
        !worldMetadata.heaven.hasSafePosition) {
        const glm::dvec3 spawn = world.findSafeSpawn();
        player.setPosition(spawn);
        worldMetadata.heaven.playerPosition = spawn;
    }
    resetTransientState(
        newWorld, survivalTicks, loadingStarted);
    if (newWorld) {
        dimension = DimensionId::Overworld;
        loadingReason = LoadingReason::World;
        worldMetadata.activeDimension = dimension;
        dimensionSaveStore.reset();
        world.setSaveStore(activeDataStore());
        entities.setSaveStore(activeDataStore());
        entities.setNaturalSpawningEnabled(true);
        world.resetForNewSeed(
            worldMetadata.seed, worldMetadata.worldType, dimension);
        loadActiveDimensionState();
        const glm::dvec3 spawn = world.findSafeSpawn();
        player.setPosition(spawn);
        worldMetadata.playerPosition = spawn;
        worldMetadata.worldSpawn = glm::ivec3(
            static_cast<int>(std::floor(spawn.x)),
            static_cast<int>(std::floor(spawn.y)),
            static_cast<int>(std::floor(spawn.z)));
    }
    if (!newWorld && dimension == DimensionId::Overworld)
        entities.loadEntities(worldMetadata.entities);
    world.update(player.getPosition());
    world.enqueueGeneration();
    return mode;
}

void GameSession::safeSpawn() {
    const int px = static_cast<int>(std::floor(player.getPosition().x));
    const int pz = static_cast<int>(std::floor(player.getPosition().z));
    for (int wy = Config::WORLD_MAX_Y - 1; wy >= Config::WORLD_MIN_Y; --wy) {
        const BlockId id = world.getBlock(px, wy, pz);
        if (!getBlockProps(id).solid) continue;
        auto position = player.getPosition();
        position.y = static_cast<float>(wy + 1) + 0.01f;
        player.setPosition(position);
        LOG_INFO("Spawn: ground at y=" << wy << ", player at y=" << position.y);
        return;
    }
    LOG_INFO("No ground found at spawn, creating platform");
    for (int y = Config::SEA_LEVEL - 4; y <= Config::SEA_LEVEL - 1; ++y)
        world.setBlock(px, y, pz, BlockId::STONE);
    world.setBlock(px, Config::SEA_LEVEL, pz, BlockId::GRASS);
    auto position = player.getPosition();
    position.y = Config::SEA_LEVEL + 1.01f;
    player.setPosition(position);
}

bool GameSession::advanceLoading(
    IGameRenderer* renderer, RuntimeClock::Tick now) {
    world.update(player.getPosition(), Config::LOADING_CHUNK_LOADS_PER_FRAME,
                 glm::dvec3(player.velocity()));
    // A validated spawn/safe-position correction can move the streaming
    // center after generation first reaches 100%. Keep feeding cache reads
    // and generation during the preparation phase so the newly exposed edge
    // of that target cannot remain permanently requested.
    world.enqueueGeneration();
    if (!loadingGenerationComplete) {
        world.processCompletedGenerations(false);
        const auto generation = world.generationProgress();
        if (world.streamingTargetReady() && generation.total > 0 &&
            generation.completed == generation.total && threadPool.idle()) {
            if (loadingNewWorld) {
                world.persistGeneratedChunks();
                safeSpawn();
                const auto position = player.getPosition();
                worldMetadata.playerPosition = position;
                worldMetadata.worldSpawn = glm::ivec3(
                    static_cast<int>(std::floor(position.x)),
                    static_cast<int>(std::floor(position.y)),
                    static_cast<int>(std::floor(position.z)));
                worldMetadata.worldTicks = survivalTicks;
                worldMetadata.weather = weather.saveState();
                saveStore->saveMetadata(worldMetadata);
            }
            if (dimension == DimensionId::Heaven) {
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
        world.processCompletedGenerations(
            true, Config::LOADING_MAIN_BUDGET_MS);
        world.enqueueMeshBuilds(Config::LOADING_MESH_TASKS_IN_FLIGHT);
        world.processCompletedMeshes(
            renderer, Config::LOADING_MESH_UPLOADS_PER_FRAME,
            Config::LOADING_MESH_UPLOAD_BYTES_PER_FRAME);
    }
    const auto progress = world.loadingProgress();
    if (!loadingGenerationComplete || !world.streamingTargetReady() ||
        progress.total == 0 || progress.completed != progress.total ||
        !threadPool.idle())
        return false;

    terrainGenerated = true;
    const float seconds = static_cast<float>(RuntimeClock::seconds(
        RuntimeClock::elapsed(worldLoadingStarted, now)));
    LOG_INFO("World render target loaded in " << seconds << "s ("
             << progress.total << " chunks)");
    LOG_INFO("WASD=move | Mouse=look | Space=jump | Ctrl=sprint");
    LOG_INFO("Left-click=break | Right-click=place | ESC=pause");
    return true;
}

void GameSession::updatePlaying(
    float dt, IGameRenderer* renderer, const Feedback& feedback) {
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
        !world.validBedFoot(sleepBed))
        cancelSleep(feedback);
    const glm::dvec3 playerEye = player.getEyePosition();
    const int rainX = static_cast<int>(std::floor(playerEye.x));
    const int rainY = static_cast<int>(std::floor(playerEye.y));
    const int rainZ = static_cast<int>(std::floor(playerEye.z));
    const bool rainExposure = weather.raining() &&
        world.precipitationAt(rainX, rainY, rainZ) == PrecipitationType::Rain &&
        world.hasSkyAccess(rainX, rainY, rainZ);
    player.setRainExposure(rainExposure);
    if (feedback.setRainVolume)
        feedback.setRainVolume(
            weather.rainGradient() * (rainExposure ? 0.72f : 0.06f));
    if (!playerDead) player.update(dt);
    particles.update(world, player.getPosition(), dt, weather.rainGradient(),
                     worldMetadata.seed ^ survivalTicks);
    const bool peaceful = player.difficulty() == Difficulty::Peaceful;
    entities.update(player, dt, dayNightCycle.isDay(), peaceful,
                    player.isSurvival(), !player.isSpectator(),
                    dimension == DimensionId::Overworld && weather.thundering(),
                    dimension == DimensionId::Overworld && weather.raining());
    for (const glm::dvec3& explosion : entities.takeExplosionEvents()) {
        particles.emitExplosion(explosion);
        const glm::dvec3 delta = explosion - player.getPosition();
        const float distance = static_cast<float>(glm::length(delta));
        if (feedback.playExplosion)
            feedback.playExplosion(
                std::clamp(static_cast<float>(delta.x) / 24.0f, -1.0f, 1.0f),
                std::clamp(1.0f - distance / 96.0f, .16f, 1.0f));
        if (feedback.rumble)
            feedback.rumble(
                std::clamp(1.0f - distance / 20.0f, .15f, 1.0f), 260);
    }
    if (player.isSurvival() && !playerDead && player.survivalStats().dead()) {
        beginPlayerDeath();
        if (feedback.playerDied) feedback.playerDied();
    }

    survivalWorldTickRemainder += dt * 20.0f;
    size_t fluidUpdatesRemaining = Config::FLUID_UPDATES_PER_FRAME;
    while (survivalWorldTickRemainder >= 1.0f) {
        ++survivalTicks;
        survivalWorldTickRemainder -= 1.0f;
        if (dimension == DimensionId::Overworld) weather.tick();
        if (dimension == DimensionId::Overworld) tickLightning(feedback);
        world.tickBlockEntities();
        const size_t fluidBudget = std::min(
            Config::FLUID_UPDATES_PER_TICK, fluidUpdatesRemaining);
        // Consume the frame allowance before dispatch.  The scheduler may
        // encounter stale de-duplicated queue entries and return fewer live
        // updates, but examining those entries is work that must still be
        // bounded during a catch-up loop.
        fluidUpdatesRemaining -= fluidBudget;
        world.tickFluids(survivalTicks, fluidBudget);
        if ((survivalTicks % 20) == 0) {
            world.tickSurvival(
                player.getPosition(), survivalTicks, weather.raining());
            if (dimension == DimensionId::Overworld)
                world.tickWeather(weather, dayNightCycle.isDay(), survivalTicks);
        }
        for (const glm::ivec3& position : world.takeTntIgnitions())
            entities.primeTnt(position, 4.0f, false);
    }
    for (auto& lightning : lightningEvents) lightning.seconds -= dt;
    lightningEvents.erase(std::remove_if(
        lightningEvents.begin(), lightningEvents.end(),
        [](const LightningEvent& event) { return event.seconds <= 0.0f; }),
        lightningEvents.end());

    world.update(player.getPosition(), 0, glm::dvec3(player.velocity()));
    world.enqueueGeneration();
    world.processCompletedGenerations();
    entities.syncChunks();
    world.enqueueMeshBuilds();
    world.processCompletedMeshes(renderer, Config::MESH_UPLOADS_PER_FRAME);

    if (dimension == DimensionId::Heaven) weather.setWeather(WeatherType::Clear);
    if (dimension == DimensionId::Heaven) saveActiveDimensionState();
    autosaveSeconds += dt;
    if (autosaveSeconds >= 30.0f) {
        beginAutosave(feedback.autosaveMetadataError);
        autosaveSeconds = 0.0f;
    }
    processAutosave(feedback.autosaveFlushError);
}

GameSession::CommandResult GameSession::executeCommand(
    const ParsedCommand& command, const Localization& localization) {
    CommandResult result;
    auto message = [&](std::string value) {
        result.messages.push_back(std::move(value));
    };
    if (command.type == CommandType::Help) {
        message(localization.text("message.help_header"));
        message("/help");
        message("/gamemode 0|1|3");
        message("/tp <x> <y> <z>");
        message("/time set day|night");
        message("/weather clear|rain|thunder");
        message("/locate biome <biome>");
        message(localization.text("message.help_biomes"));
        return result;
    }
    if (!worldMetadata.cheatsEnabled) {
        message(localization.text("message.cheats_disabled"));
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
        world.update(player.getPosition());
        world.enqueueGeneration();
        message(localization.format("message.teleported", {
            std::to_string(target.x), std::to_string(target.y),
            std::to_string(target.z)}));
        return result;
    }
    if (command.type == CommandType::Time) {
        if (command.time == TimePreset::Day) {
            dayNightCycle.setDay();
            message(localization.text("message.time_day"));
        } else {
            dayNightCycle.setNight();
            message(localization.text("message.time_night"));
        }
        return result;
    }
    if (command.type == CommandType::Weather) {
        if (dimension == DimensionId::Heaven) {
            message(localization.text("message.heaven_weather_clear"));
            return result;
        }
        weather.setWeather(command.weather);
        message(localization.text(
            command.weather == WeatherType::Clear ? "message.weather_clear" :
            command.weather == WeatherType::Rain ? "message.weather_rain" :
                                                   "message.weather_thunder"));
        return result;
    }
    if (command.type == CommandType::LocateBiome) {
        const glm::dvec3 position = player.getPosition();
        const auto location = world.locateBiome(
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
    }
    return result;
}

bool GameSession::beginSleepAtBed(const glm::ivec3& bed) {
    if (sleepState != SleepVisualState::Awake || playerDead) return false;
    const auto foot = world.validBedFoot(bed);
    if (!foot || !dayNightCycle.isNight()) return false;
    if (entities.hasHostileNear(glm::vec3(*foot), 8.0f))
        return false;

    sleepBed = *foot;
    BedPart part = BedPart::Foot;
    BedDirection direction = BedDirection::North;
    decodeBed(world.getBlock(foot->x, foot->y, foot->z), part, direction);
    sleepFacingDirection = glm::vec3(bedDirectionOffset(direction));
    const float bedHeight = blockCollisionHeight(
        world.getBlock(foot->x, foot->y, foot->z));
    player.setPosition(glm::dvec3(
        foot->x + 0.5, foot->y + bedHeight + 0.01, foot->z + 0.5));
    sleepState = SleepVisualState::Entering;
    sleepProgress = 0.0f;
    player.setSleepingVisual(true, 0.0f);
    player.cancelBowCharge();
    return true;
}

void GameSession::finishSleep(const Feedback& /*feedback*/) {
    if (sleepState == SleepVisualState::Awake) return;
    sleepState = SleepVisualState::Leaving;
    sleepProgress = 0.0f;
    player.setSleepingVisual(true, 1.0f);
    player.cancelBowCharge();
}

void GameSession::chooseSleepAction(
    SleepAction action, RuntimeClock::Tick loadingStarted,
    const Feedback& feedback) {
    if (sleepState != SleepVisualState::Choosing &&
        sleepState != SleepVisualState::Entering)
        return;
    if (action == SleepAction::TravelToHeaven &&
        dimension != DimensionId::Overworld) {
        finishSleep(feedback);
        return;
    }
    if (action == SleepAction::SleepUntilMorning) {
        dayNightCycle.resetMorning();
        if (dimension == DimensionId::Overworld)
            weather.setWeather(WeatherType::Clear);
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
    if (sleepState == SleepVisualState::Awake) return;
    finishSleep(feedback);
}

bool GameSession::switchDimension(
    DimensionId target, RuntimeClock::Tick loadingStarted) {
    if (!saveStore || target == dimension) return false;
    saveActiveDimensionState();
    updateSaveMetadata();
    // Flush the active dimension while its SaveStore is still attached to the
    // streaming and entity pipelines.  resetForNewSeed then drains the same
    // queues before the target store is installed.
    entities.beginChunkEntityAutosave();
    entities.flushChunkEntities(std::numeric_limits<size_t>::max(), true);
    world.beginModifiedChunkAutosave();
    world.flushModifiedChunks();
    saveStore->saveMetadata(worldMetadata);

    world.resetForNewSeed(worldMetadata.seed, worldMetadata.worldType, target);
    dimension = target;
    worldMetadata.activeDimension = target;
    loadingReason = target == DimensionId::Heaven
        ? LoadingReason::EnteringHeaven : LoadingReason::ReturningOverworld;
    if (target == DimensionId::Heaven) {
        dimensionSaveStore = std::make_unique<SaveStore>(
            saveStore->worldDirectory() / "dimensions" / "heaven");
    } else {
        dimensionSaveStore.reset();
    }
    world.setSaveStore(activeDataStore());
    entities.setSaveStore(activeDataStore());
    entities.setNaturalSpawningEnabled(target == DimensionId::Overworld);
    entities.clear();
    if (target == DimensionId::Overworld)
        entities.loadEntities(worldMetadata.entities);
    loadActiveDimensionState();
    if (target == DimensionId::Heaven &&
        !worldMetadata.heaven.hasSafePosition) {
        // Match direct world loading: choose the deterministic island spawn
        // before constructing the first streaming target. Otherwise loading
        // begins around the placeholder position and shifts near completion.
        const glm::dvec3 spawn = world.findSafeSpawn();
        player.setPosition(spawn);
        worldMetadata.heaven.playerPosition = spawn;
    }
    resetTransientState(false, survivalTicks, loadingStarted);
    sleepState = SleepVisualState::Awake;
    player.setSleepingVisual(false, 0.0f);
    world.update(player.getPosition());
    world.enqueueGeneration();
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
    if (!switchDimension(DimensionId::Overworld, loadingStarted)) return false;
    if (feedback.dimensionLoading) feedback.dimensionLoading();
    const std::optional<glm::ivec3> bed = loadValidOverworldBed();
    if (bed) {
        const float support = blockCollisionHeight(
            world.getBlock(bed->x, bed->y, bed->z));
        player.setPosition(glm::dvec3(bed->x + 0.5, bed->y + support + 0.001,
                                      bed->z + 0.5));
    } else {
        player.setPosition(world.findSafeSpawn());
    }
    world.update(player.getPosition());
    world.enqueueGeneration();
    updateSaveMetadata();
    if (saveStore) saveStore->saveMetadata(worldMetadata);
    if (feedback.sleepEnded) feedback.sleepEnded();
    return true;
}

void GameSession::beginPlayerDeath() {
    playerDead = true;
    const glm::vec3 deathPosition = glm::vec3(
        player.getPosition() + glm::dvec3(0.0, 0.5, 0.0));
    for (const auto& stack : takeDeathDrops(player.inventory()))
        entities.spawnItem(deathPosition, stack);
}

void GameSession::respawn(RuntimeClock::Tick loadingStarted) {
    const bool wasHeaven = dimension == DimensionId::Heaven;
    if (wasHeaven)
        (void)switchDimension(DimensionId::Overworld, loadingStarted);
    const std::optional<glm::ivec3> validBed = wasHeaven
        ? loadValidOverworldBed()
        : (worldMetadata.bedSpawn
            ? world.validBedFoot(*worldMetadata.bedSpawn) : std::nullopt);
    const bool bedValid = validBed.has_value();
    const glm::ivec3 spawn = chooseRespawnPosition(
        worldMetadata.worldSpawn, validBed, bedValid);
    const float spawnHeight = bedValid
        ? blockCollisionHeight(world.getBlock(spawn.x, spawn.y, spawn.z)) + 0.001f
        : 1.01f;
    player.setPosition(glm::vec3(spawn) + glm::vec3(0.5f, spawnHeight, 0.5f));
    player.survivalStats().resetAfterRespawn();
    player.extinguish();
    player.resetDamageImmunity();
    world.update(player.getPosition());
    world.enqueueGeneration();
    world.waitForInitialGeneration(150);
    world.processCompletedGenerations();
    playerDead = false;
    player.setSleepingVisual(false, 0.0f);
}

void GameSession::tickLightning(const Feedback& feedback) {
    if (!weather.thundering()) return;
    auto hash = [](uint64_t value) {
        value ^= value >> 30;
        value *= 0xbf58476d1ce4e5b9ULL;
        value ^= value >> 27;
        value *= 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    };
    for (const Chunk* chunk : world.getActiveChunks()) {
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
        lightningEvents.push_back({glm::dvec3(strike), 0.5f});
        particles.appendLightning(glm::dvec3(strike));
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
    survivalTicks = worldTicks;
    survivalWorldTickRemainder = 0.0f;
    lightningEvents.clear();
    particles.clear();
}

void GameSession::saveActiveDimensionState() {
    if (dimension == DimensionId::Overworld) {
        worldMetadata.playerPosition = player.getPosition();
        worldMetadata.worldTicks = survivalTicks;
        worldMetadata.overworldDayPhase = dayNightCycle.phase();
    } else {
        worldMetadata.heaven.playerPosition = player.getPosition();
        worldMetadata.heaven.worldTicks = survivalTicks;
        worldMetadata.heaven.dayPhase = dayNightCycle.phase();
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
        survivalTicks = worldMetadata.worldTicks;
        dayNightCycle.setPhase(worldMetadata.overworldDayPhase);
        weather.reset(worldMetadata.seed, worldMetadata.weather);
    } else {
        player.setPosition(worldMetadata.heaven.playerPosition);
        survivalTicks = worldMetadata.heaven.worldTicks;
        dayNightCycle.setPhase(worldMetadata.heaven.dayPhase);
        weather.reset(worldMetadata.seed, WeatherSaveState{});
        weather.setWeather(WeatherType::Clear);
    }
}

std::optional<glm::ivec3> GameSession::loadValidOverworldBed() {
    if (dimension != DimensionId::Overworld || !worldMetadata.bedSpawn)
        return std::nullopt;
    const glm::ivec3 requested = *worldMetadata.bedSpawn;
    // A dimension switch starts the normal target stream around the saved
    // position, not necessarily around the bed. Ensure the bed's chunk and
    // its persisted two-block state are available before validating it.
    world.update(glm::dvec3(requested), Config::LOADING_CHUNK_LOADS_PER_FRAME);
    world.enqueueGeneration();
    // Cache hits become generated only when their main-thread completion is
    // consumed, so give both the I/O lane and the generation lane a few
    // bounded chances before falling back to the world spawn.
    for (int attempt = 0; attempt < 5; ++attempt) {
        world.waitForInitialGeneration(250);
        world.processCompletedGenerations(false);
        if (const auto foot = world.validBedFoot(requested)) return foot;
        world.enqueueGeneration();
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
    if (!safe(world, candidate)) candidate = player.getPosition();
    if (!safe(world, candidate)) candidate = world.findSafeSpawn();
    if (!safe(world, candidate)) {
        // Deterministic generation normally always provides a candidate, but
        // a heavily edited save can remove every nearby island.  A tiny
        // platform is a recoverable player edit and prevents a permanent
        // void loop.
        const int x = 0;
        const int z = 0;
        const int y = 128;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                world.setBlock(x + dx, y - 1, z + dz, BlockId::STONE);
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
    if (dimension == DimensionId::Overworld)
        worldMetadata.weather = weather.saveState();
    worldMetadata.entities.clear();
}

void GameSession::beginAutosave(const std::function<void()>& onError) {
    if (!saveStore || !terrainGenerated || autosavePending) return;
    try {
        updateSaveMetadata();
        saveStore->saveMetadata(worldMetadata);
        entities.beginChunkEntityAutosave();
        world.beginModifiedChunkAutosave();
        autosavePending = world.hasPendingModifiedChunkSaves() ||
                          entities.hasPendingChunkEntitySaves();
        autosaveEntityTurn = true;
    } catch (const std::exception& error) {
        LOG_ERROR("Autosave metadata failed: " << error.what());
        if (onError) onError();
    }
}

void GameSession::processAutosave(const std::function<void()>& onError) {
    if (!autosavePending) return;
    try {
        if (autosaveEntityTurn && entities.hasPendingChunkEntitySaves())
            entities.flushChunkEntities(1);
        else if (world.hasPendingModifiedChunkSaves())
            world.flushModifiedChunks(1);
        else if (entities.hasPendingChunkEntitySaves())
            entities.flushChunkEntities(1);
        autosaveEntityTurn = !autosaveEntityTurn;
        autosavePending = world.hasPendingModifiedChunkSaves() ||
                          entities.hasPendingChunkEntitySaves();
    } catch (const std::exception& error) {
        autosavePending = false;
        LOG_ERROR("Autosave chunk flush failed: " << error.what());
        if (onError) onError();
    }
}

void GameSession::saveNow(const std::function<void()>& onError) {
    if (!saveStore || !terrainGenerated) return;
    try {
        updateSaveMetadata();
        entities.beginChunkEntityAutosave();
        world.beginModifiedChunkAutosave();
        entities.flushChunkEntities(std::numeric_limits<size_t>::max(), true);
        world.flushModifiedChunks();
        saveStore->saveMetadata(worldMetadata);
        autosavePending = false;
    } catch (const std::exception& error) {
        LOG_ERROR("Could not save world: " << error.what());
        if (onError) onError();
    }
}
