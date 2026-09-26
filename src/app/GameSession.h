#pragma once

#include "core/RuntimeClock.h"
#include "entity/EntityManager.h"
#include "game/SaveStore.h"
#include "game/SessionAccess.h"
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

class GameSession : public IContainerAccess, public ITradeAccess {
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
        bool teleported = false;
    };
    struct LightningEvent {
        glm::dvec3 position{0.0};
        float seconds = 0.0f;
    };
    struct LoadingSnapshot {
        StreamingProgress progress;
        float fraction = 0.0f;
        bool preparing = false;
        bool newWorld = false;
        LoadingReason reason = LoadingReason::World;
    };

    explicit GameSession(const std::filesystem::path& savesDirectory);

    void leaveWorld();
    GameMode startWorld(const std::string& worldId, bool newWorld,
                        RuntimeClock::Tick loadingStarted);
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
                                 const Localization& localization,
                                 RuntimeClock::Tick now = 0);
    void saveNow(const std::function<void()>& onError);

    const World& worldState() const { return world; }
    const Player& playerState() const { return player; }
    const EntityManager& entityState() const { return entities; }
    const DayNightCycle& daylightState() const { return dayNightCycle; }
    const WeatherSystem& weatherState() const { return weather; }
    const ParticleSystem& particleState() const { return particles; }
    const std::vector<LightningEvent>& lightningState() const { return lightningEvents; }
    const WorldMetadata& metadata() const { return worldMetadata; }
    bool isPlayerDead() const { return playerDead; }
    bool hasWorldStore() const { return saveStore != nullptr; }
    LoadingSnapshot loadingSnapshot() const;
    std::vector<WorldSummary> listWorlds() const;
    std::string createWorld(const std::string& name, uint64_t seed,
                            GameMode mode, Difficulty difficulty, bool cheats,
                            WorldType type = WorldType::Normal);
    bool deleteWorld(const std::string& id);
    void setOverworldBedSpawn(const glm::ivec3& bed);
    bool isNight() const { return dayNightCycle.isNight(); }
    void updateDaylight(float dt, float minutes, bool playing);
    void configureVisuals(const EnhancedVisualSettings& visuals, VisualQuality quality);
    void configureLod(const LodSettings& settings);
    void setToggleSneak(bool enabled);
    void initializeEntityModels(const std::filesystem::path& assetRoot,
                                IGameRenderer& renderer);
    void invalidateGpuMeshes();
    void restoreGpuMeshes(IGameRenderer* renderer);
    void emitBlockBreak(const glm::ivec3& position, BlockId block);
    void emitCriticalHit(const glm::dvec3& position);
    void emitSweepAttack(const glm::dvec3& position);
    void setBlockBreakCallback(std::function<void(const glm::ivec3&, BlockId)> callback);
    void setDamageCallback(std::function<void(float)> callback);
    void setCombatCallback(std::function<void(const CombatFeedback&)> callback);
    void setDefenseCallback(std::function<void(const DamageOutcome&)> callback);
    void setBedCallback(std::function<void(const glm::ivec3&)> callback);
    void cancelBowCharge();
    void handleMouseDelta(float dx, float dy, float sensitivity, bool invertY);
    void handleMovement(const InputState& input, float dt);
    void handleMouseButton(int button, ButtonAction action);
    void setSelectedSlot(int slot);
    InventoryModel& inventory() { return player.inventory(); }
    const InventoryModel& inventory() const { return player.inventory(); }
    BlockEntity* blockEntityAt(const glm::ivec3& position) override;
    const Entity* tradeEntity(uint64_t entityId) const override;
    bool tradeUsable(uint64_t entityId, const glm::dvec3& eye,
                     const glm::vec3& direction, float reach) const override;
    void executeTrade(uint64_t entityId, uint8_t offerIndex,
                      InventoryModel& inventory) override;
    std::optional<uint64_t> useVillagerRay(float reach);
    void giveCreativeItem(ItemId item, int hotbarSlot);
    void dropSelectedItem(int hotbarSlot, bool entireStack);
    void dropInventoryItem(ItemStack stack);
    struct PickBlockResult { int slot = 0; bool itemChanged = false; };
    std::optional<PickBlockResult> pickBlock(int selectedSlot);
    void swapOffhand(int hotbarSlot);
    void dropContainerRemainder(ItemStack stack);

private:
    // Flow tests need to construct states that normally require a full render
    // loading gate. The bypass is unavailable to application code.
    friend struct GameSessionTestAccess;
    void detachSaveStore();
    void safeSpawn();
    void beginAutosave(const std::function<void()>& onError);
    void processAutosave(const std::function<void()>& onError);
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
