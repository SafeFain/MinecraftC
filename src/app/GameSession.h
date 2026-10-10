#pragma once

#include "plugins/Runtime.h"

#include "core/RuntimeClock.h"
#include "app/DimensionSimulation.h"
#include "app/LanPlayerRuntime.h"
#include "network/GameplayProtocol.h"
#include "network/ChunkProtocol.h"
#include "network/StateProtocol.h"
#include "network/EntityProtocol.h"
#include "network/ChatProtocol.h"
#include "network/EventProtocol.h"
#include "core/LanDiscovery.h"
#include <array>
#include <chrono>
#include "entity/EntityManager.h"
#include "game/SaveStore.h"
#include "game/FishingSystem.h"
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
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class IGameRenderer;
class Localization;
struct ParsedCommand;

class GameSession : public IContainerAccess, public ITradeAccess, public IInventoryCommands {
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

    enum class LoadingPhase : uint8_t {
        Chunks,
        PreparingChunks,
        DistantTerrain
    };

    struct Feedback {
        std::function<void(float)> setRainVolume;
        std::function<void(float, float)> playExplosion;
        std::function<void(float, float)> playThunder;
        std::function<void(float, uint32_t)> rumble;
        std::function<void(FishingEventKind)> playFishing;
        std::function<void(bool,bool,bool)> playBlockInteraction;
        std::function<void()> playerDied;
        std::function<void()> playerDeathMessage;
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
        LoadingPhase phase = LoadingPhase::Chunks;
        float phaseFraction = 0.0f;
        bool newWorld = false;
        LoadingReason reason = LoadingReason::World;
    };

    explicit GameSession(const std::filesystem::path& savesDirectory);

    Platform::LanAdvertisement lanAdvertisement() const;
    std::string lanNickname() const;
    bool setLanNickname(const std::string& nickname);
    bool lanPvpEnabled() const { return lanPvp; }
    void setLanPvp(bool enabled) { lanPvp = enabled; }
    uint64_t lanStreamEpoch() const { return replicaEpoch; }
    bool openLanRoom(uint16_t port = Lan::DEFAULT_PORT, size_t capacity = Lan::MAX_PLAYERS,
                     bool loopbackOnly = false);
    void closeLanRoom();
    bool joinLanRoom(const std::string& address, uint16_t port, double now);
    bool joiningLan() const { return lanJoining; }
    bool lanWorldReady() const { return replicaWorldInfo && replicaOwnerState; }
    bool lanConnectionFailed() const { return lanJoining && !lanFailure.empty(); }
    const std::vector<Lan::PlayerView>& remotePlayers() const { return replicaOthers; }
    void pollLan(double now);
    bool hostingLan() const { return lanHost.port() != 0; }
    uint16_t lanPort() const { return lanHost.port(); }
    std::vector<Lan::RoomPlayer> roomRoster() const;
    size_t lanGuestCount() const { return guests.size(); }
    const std::string& lanError() const { return lanFailure; }
    static Lan::Compatibility lanCompatibility();
    bool usesInventoryCommands() const override { return hostingLan() || joiningLan(); }
    InventoryWindowView inventoryWindow() const override;
    bool inventoryWindowPending() const override { return lanJoining && !replicaActions.empty(); }
    void openInventoryWindow(InventoryWindowKind kind, glm::ivec3 position = {}) override;
    void submitInventoryAction(InventoryAction action) override;
    void closeInventoryWindow() override;
    bool sendLanChat(const std::string& text);
    std::vector<Lan::ChatMessage> takeLanChat();
    void leaveWorld();
    void abortPluginWorld();
    GameMode startWorld(const std::string& worldId, bool newWorld,
                        RuntimeClock::Tick loadingStarted);
    bool advanceLoading(IGameRenderer* renderer, RuntimeClock::Tick now);
    void updatePlaying(float dt, IGameRenderer* renderer,
                       const Feedback& feedback, bool localControl = true);
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

    const World& worldState() const { return world(); }
    const Player& playerState() const { return player; }
    const EntityManager& entityState() const { return entities(); }
    const DayNightCycle& daylightState() const { return dayNightCycle(); }
    const FishingView& fishingState() const { return lanJoining ? replicaFishing : fishing.view(); }
    const WeatherSystem& weatherState() const { return weather(); }
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
    bool isNight() const { return dayNightCycle().isNight(); }
    void updateDaylight(float dt, bool playing);
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
    void setLocalControl(bool enabled);
    void cancelBowCharge();
    void handleMouseDelta(float dx, float dy, float sensitivity, bool invertY);
    void handleMovement(const InputState& input, float dt);
    void handleMouseButton(int button, ButtonAction action, bool pluginUseApproved = false);
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
    bool pluginUse(bool after = false);
    bool pluginPlayer(MC_PlayerSnapshot&) const;
    bool pluginGetBlock(int32_t,int32_t,int32_t,uint16_t&);
    bool pluginSetBlock(int32_t,int32_t,int32_t,uint16_t);
    bool pluginGiveItem(uint16_t,uint32_t);
    std::filesystem::path pluginWorldDirectory() const;
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
    // Stores outlive world I/O, and both dimensions outlive their players.
    std::unique_ptr<SaveStore> saveStore;
    ThreadPool threadPool;
    DimensionId dimension = DimensionId::Overworld;
    std::array<std::unique_ptr<DimensionSimulation>, 2> simulations;
    Player player;
    // Declared after dimensions: all remote Players are destroyed before Worlds.
    std::map<uint64_t, std::unique_ptr<LanPlayerRuntime>> guests;
    Lan::Host lanHost;
    Lan::ChunkJournal chunkJournal;
    InventoryTransaction hostWindow;
    InventoryWindowView hostWindowView, replicaWindow;
    uint64_t hostActionSequence = 0, replicaActionSequence = 0;
    std::deque<Lan::GameAction> replicaActions;
    uint64_t replicaActionInFlight = 0;
    void queueReplicaAction(Lan::GameAction action);
    void sendReplicaAction();
    void applyLanAction(LanPlayerRuntime& guest, const Lan::GameAction& action);
    bool openAuthorityWindow(Player& owner, DimensionSimulation& runtime, InventoryTransaction& transaction,
                             InventoryWindowView& view, InventoryWindowKind kind, glm::ivec3 position);
    InventoryWindowView authorityWindow(Player& owner, DimensionSimulation& runtime, InventoryTransaction& transaction, InventoryWindowView& view);
    void closeAuthorityWindow(Player& owner, DimensionSimulation& runtime, InventoryTransaction& transaction, InventoryWindowView& view);
    void applyAuthorityInventory(Player& owner, DimensionSimulation& runtime, InventoryTransaction& transaction,
                                 InventoryWindowView& view, InventoryAction action);
    struct LodJob { uint64_t peer, epoch, revision, subscription; DimensionId dimension; LodTileKey key; std::future<Lan::Bytes> result; };
    std::vector<LodJob> lodJobs;
    uint64_t lodLastPeer = 0;
    struct ReplicaLodRevision { uint64_t received = 0, required = 1; bool pending = false; };
    std::unordered_map<LodTileKey, ReplicaLodRevision, LodTileKeyHash> replicaLodRevisions;
    void requestLanLod(LanPlayerRuntime& guest, const Lan::LodUpdate& request);
    void sendLanLod();
    void pollReplicaLod();
    void invalidateLanLod(DimensionId target, int x, int z);
    std::deque<Lan::GameEvent> lanEvents;
    size_t lanEventsSent = 0;
    std::function<void(const glm::ivec3&, BlockId)> blockBreakFeedback;
    std::function<void(float)> damageFeedback;
    std::function<void(const CombatFeedback&)> combatFeedback;
    std::function<void(const DamageOutcome&)> defenseFeedback;
    void bindLanFeedback(Player& owner, uint64_t id);
    void broadcastGameEvent(Lan::GameEvent event, bool ownerOnly = false, bool locally = false);
    void presentLanEvents(const Feedback& feedback);
    Lan::Client lanClient;
    bool lanPvp = false;
    bool replicaDeathNotified = false, replicaSleepStarted = false, replicaSleepEnded = false;
    bool hostWantsMorning = false;
    bool lanJoining = false, replicaWorldInfo = false, replicaOwnerState = false;
    uint64_t replicaEpoch = 0;
    Lan::PlayerInput replicaInput;
    double replicaLastInputSent = -1;
    std::vector<Lan::PlayerView> replicaOthers;
    std::vector<Lan::RoomPlayer> replicaRoster;
    std::deque<Lan::Message> replicaChunks;
    size_t replicaChunkBytes = 0;
    std::map<Lan::ChunkAddress, uint64_t> replicaRevisions;
    std::set<Lan::ChunkAddress> replicaRecovery;
    FishingView replicaFishing;
    void pollReplica(double now);
    void sendReplicaInput(bool force = false);
    void updateReplica(float dt, IGameRenderer* renderer, const Feedback& feedback);
    void applyReplicaState(const Lan::AuthorityState& state);
    void closeReplica();
    std::unique_ptr<Lan::ProfileStore> guestProfiles;
    Lan::Identity hostIdentity;
    std::filesystem::path dataDirectory, replicaLodRoot;
    std::string lanFailure;
    double lanNow = 0;
    std::deque<Lan::ChatMessage> lanChat;
    double hostChatLast = 0;
    float hostChatTokens = 5;
    void broadcastLanChat(const Lan::ChatMessage& message);
    void enqueueLanChat(Lan::ChatMessage message);
    void admitLanPlayer(uint64_t id);
    void removeLanPlayer(uint64_t id);
    void saveLanPlayer(LanPlayerRuntime& guest);
    void updateLanInterests();
    void sendLanChunks();
    void sendLanStates();
    void sendLanEntities();
    void updateLanPlayers(float dt);
    bool beginLanSleep(uint64_t id, glm::ivec3 bed);
    void finishLanSleep(LanPlayerRuntime& guest);
    void updateLanSleep(float dt);
    bool trySkipLanNight(DimensionId target);
    void travelLanPlayer(LanPlayerRuntime& guest, DimensionId target, bool respawn = false);
    void simulateDimension(DimensionId id, const std::vector<EntityPlayerView>& views,
                           float dt, const Feedback& feedback, size_t& fluidRemaining,
                           std::chrono::steady_clock::time_point fluidDeadline);

    DimensionSimulation& simulation() { return *simulations[static_cast<size_t>(dimension)]; }
    const DimensionSimulation& simulation() const { return *simulations[static_cast<size_t>(dimension)]; }
    World& world() { return simulation().world; }
    const World& world() const { return simulation().world; }
    EntityManager& entities() { return simulation().entities; }
    const EntityManager& entities() const { return simulation().entities; }
    DayNightCycle& dayNightCycle() { return simulation().daylight; }
    const DayNightCycle& dayNightCycle() const { return simulation().daylight; }
    WeatherSystem& weather() { return simulation().weather; }
    const WeatherSystem& weather() const { return simulation().weather; }
    uint64_t& survivalTicks() { return simulation().ticks; }
    float& survivalWorldTickRemainder() { return simulation().tickRemainder; }
    DimensionSimulation& ensureDimension(DimensionId target);
    void flushDimensions(bool synchronous);
    LodSettings lodSettings;
    ParticleSystem particles;
    FishingSystem fishing;
    int fishingSlot = -1;
    uint16_t fishingRodDamage = 0;
    std::vector<FishingEvent> fishingFeedback;
    FishingEnvironment fishingEnvironment();
    void collectFishingEvents();
    void validateFishingRod();
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
    size_t autosaveDimensionCursor = 0;
    bool playerDead = false;
    LoadingReason loadingReason = LoadingReason::World;
    SleepVisualState sleepState = SleepVisualState::Awake;
    glm::ivec3 sleepBed{0};
    float sleepProgress = 0.0f;
    glm::vec3 sleepFacingDirection{0.0f, 0.0f, -1.0f};

private:
    CommandResult executeCommandImpl(const ParsedCommand& command, const Localization& localization, RuntimeClock::Tick now);
    SaveStore* activeDataStore() const;
    void saveActiveDimensionState();
    void loadActiveDimensionState();
    std::optional<glm::ivec3> loadValidOverworldBed();
    void ensureHeavenSafePosition();
    void finishSleep(const Feedback& feedback);
    void updateSaveMetadata();
    void tickLightning(DimensionSimulation& runtime, const Feedback& feedback);
    void beginPlayerDeath();
};
