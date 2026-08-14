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
    struct Feedback {
        std::function<void(float)> setRainVolume;
        std::function<void(float, float)> playExplosion;
        std::function<void(float, float)> playThunder;
        std::function<void(float, uint32_t)> rumble;
        std::function<void()> playerDied;
        std::function<void()> autosaveMetadataError;
        std::function<void()> autosaveFlushError;
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
    void respawn();
    CommandResult executeCommand(const ParsedCommand& command,
                                 const Localization& localization);
    void beginAutosave(const std::function<void()>& onError);
    void processAutosave(const std::function<void()>& onError);
    void saveNow(const std::function<void()>& onError);
    void resetTransientState(bool newWorld, uint64_t worldTicks,
                             RuntimeClock::Tick loadingStarted);

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
    std::unique_ptr<SaveStore> saveStore;
    WorldCatalog worldCatalog;
    WorldMetadata worldMetadata;
    float autosaveSeconds = 0.0f;
    bool autosavePending = false;
    bool autosaveEntityTurn = true;
    bool playerDead = false;
    uint64_t survivalTicks = 0;
    float survivalWorldTickRemainder = 0.0f;

private:
    void updateSaveMetadata();
    void tickLightning(const Feedback& feedback);
    void beginPlayerDeath();
};
