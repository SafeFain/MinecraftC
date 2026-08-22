#pragma once

#include "core/RuntimeClock.h"
#include "entity/EntityManager.h"
#include "game/SaveStore.h"
#include "game/Weather.h"
#include "game/WorldCatalog.h"
#include "player/Player.h"
#include "renderer/ParticleSystem.h"
#include "renderer/RenderEnvironment.h"
#include "threading/ThreadPool.h"
#include "world/World.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class IGameRenderer;
class Localization;
struct ParsedCommand;

class GameSession {
public:
    enum class SleepAction : uint8_t {
        SleepUntilMorning,
        LeaveBed,
        TravelToHeaven
    };

    enum class SleepVisualState : uint8_t {
        Awake,
        Entering,
        Choosing,
        Leaving
    };

    enum class LoadingReason : uint8_t {
        World,
        EnteringHeaven,
        ReturningOverworld
    };

    struct Feedback {
        std::function<void(float)> setRainVolume;
        std::function<void(float, float)> playExplosion;
        std::function<void(float, float)> playThunder;
        std::function<void(float, uint32_t)> rumble;
        std::function<void()> playerDied;
        std::function<void()> autosaveMetadataError;
        std::function<void()> autosaveFlushError;
        std::function<void()> sleepStarted;
        std::function<void()> sleepEnded;
        std::function<void()> sleepBlocked;
        std::function<void()> dimensionLoading;
    };
    struct CommandResult {
        std::vector<std::string> messages;
        std::optional<GameMode> gameModeChanged;
    };
    struct LightningEvent {
        glm::dvec3 position{0.0};
        float seconds = 0.0f;
    };

    explicit GameSession(const std::filesystem::path& savesDirectory);

    void detachSaveStore();
    void leaveWorld();
    GameMode startWorld(const std::string& worldId, bool newWorld,
                        RuntimeClock::Tick loadingStarted);
    void safeSpawn();
    bool advanceLoading(IGameRenderer* renderer, RuntimeClock::Tick now);
    void updatePlaying(float dt, IGameRenderer* renderer,
                       const Feedback& feedback);
    bool beginSleepAtBed(const glm::ivec3& bed);
    void chooseSleepAction(SleepAction action, RuntimeClock::Tick loadingStarted,
                           const Feedback& feedback);
    void cancelSleep(const Feedback& feedback);
    bool isSleeping() const { return sleepState != SleepVisualState::Awake; }
    SleepVisualState sleepVisualState() const { return sleepState; }
    float sleepAnimationProgress() const { return sleepProgress; }
    const glm::ivec3& sleepingBed() const { return sleepBed; }
    const glm::vec3& sleepFacing() const { return sleepFacingDirection; }
    bool handleVoidFall(RuntimeClock::Tick loadingStarted, const Feedback& feedback);
    bool switchDimension(DimensionId target, RuntimeClock::Tick loadingStarted);
    DimensionId activeDimension() const { return dimension; }
    void respawn(RuntimeClock::Tick loadingStarted = 0);
    CommandResult executeCommand(const ParsedCommand& command,
                                 const Localization& localization);
    void beginAutosave(const std::function<void()>& onError);
    void processAutosave(const std::function<void()>& onError);
    void saveNow(const std::function<void()>& onError);
    void resetTransientState(bool newWorld, uint64_t worldTicks,
                             RuntimeClock::Tick loadingStarted);

    // These owners are declared in dependency order so destruction runs as
    // World -> ThreadPool -> dimension SaveStore -> metadata SaveStore.
    // World drains its streaming I/O while both stores remain alive.
    std::unique_ptr<SaveStore> saveStore;
    std::unique_ptr<SaveStore> dimensionSaveStore;
    ThreadPool threadPool;
    World world;
    Player player;
    EntityManager entities;
    DayNightCycle dayNightCycle;
    WeatherSystem weather;
    ParticleSystem particles;
    std::vector<LightningEvent> lightningEvents;

    bool terrainGenerated = false;
    bool loadingNewWorld = false;
    bool loadingGenerationComplete = false;
    RuntimeClock::Tick worldLoadingStarted = 0;
    WorldCatalog worldCatalog;
    WorldMetadata worldMetadata;
    float autosaveSeconds = 0.0f;
    bool autosavePending = false;
    bool autosaveEntityTurn = true;
    bool playerDead = false;
    uint64_t survivalTicks = 0;
    float survivalWorldTickRemainder = 0.0f;
    DimensionId dimension = DimensionId::Overworld;
    LoadingReason loadingReason = LoadingReason::World;
    SleepVisualState sleepState = SleepVisualState::Awake;
    glm::ivec3 sleepBed{0};
    float sleepProgress = 0.0f;
    glm::vec3 sleepFacingDirection{0.0f, 0.0f, -1.0f};

private:
    SaveStore* activeDataStore() const;
    void saveActiveDimensionState();
    void loadActiveDimensionState();
    std::optional<glm::ivec3> loadValidOverworldBed();
    void ensureHeavenSafePosition();
    void finishSleep(const Feedback& feedback);
    void updateSaveMetadata();
    void tickLightning(const Feedback& feedback);
    void beginPlayerDeath();
};
