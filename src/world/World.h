#pragma once

#include <unordered_map>
#include <memory>
#include <vector>
#include <shared_mutex>
#include <optional>
#include <functional>
#include <unordered_set>
#include <atomic>
#include <queue>
#include <limits>

#include <glm/glm.hpp>

#include "Config.h"
#include "world/Chunk.h"
#include "world/ChunkMeshPipeline.h"
#include "world/ChunkStore.h"
#include "world/ChunkStreamer.h"
#include "world/FluidScheduler.h"
#include "world/WorldGenerator.h"
#include "world/WorldLighting.h"
#include "world/WorldPersistence.h"
#include "world/WorldSimulation.h"
#include "world/LodTerrainSystem.h"
#include "world/RegionGenerationData.h"
#include "world/BlockEntity.h"
#include "world/BlockLightLogic.h"
#include "game/Weather.h"

class IGameRenderer;
class ThreadPool;
class SaveStore;

class World {
public:
    World();
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // ── Thread pool ──────────────────────────────────────────────────
    void setThreadPool(ThreadPool* pool) {
        m_threadPool = pool;
        m_meshes.setThreadPool(pool);
        m_streamer.setThreadPool(pool);
        m_lod.setThreadPool(pool);
    }
    void setSaveStore(SaveStore* store) {
        m_saveStore = store;
        m_chunks.setSaveStore(store);
        m_persistence.setSaveStore(store);
        m_streamer.setSaveStore(store);
        m_lod.setSaveStore(store);
    }
    bool flushModifiedChunks(size_t maxFiles = std::numeric_limits<size_t>::max()) {
        return m_persistence.flushModifiedChunks(maxFiles);
    }
    void beginModifiedChunkAutosave() { m_persistence.beginModifiedChunkAutosave(); }
    bool hasModifiedChunks() const { return m_persistence.hasModifiedChunks(); }
    bool hasPendingModifiedChunkSaves() const {
        return m_persistence.hasPendingModifiedChunkSaves();
    }
    void tickSurvival(const glm::dvec3& playerPosition, uint64_t tick,
                      bool raining = false) {
        m_simulation.tickSurvival(playerPosition, tick, raining);
    }
    void tickWeather(const WeatherSystem& weather, bool daytime, uint64_t tick) {
        m_simulation.tickWeather(weather, daytime, tick);
    }
    void tickBlockEntities() { m_persistence.tickBlockEntities(); }
    FluidTickStats tickFluids(uint64_t tick,
                              const FluidTickBudget& budget) {
        m_currentFluidTick = tick;
        return m_fluids.tick(tick, budget);
    }
    const LightingBatchStats& fluidLightingStatistics() const {
        return m_lighting.statistics();
    }
    void resetFluidLightingStatistics() { m_lighting.resetStatistics(); }
    size_t tickFluids(
        uint64_t tick,
        size_t maximumUpdates = Config::FLUID_UPDATES_PER_TICK) {
        return tickFluids(tick, FluidTickBudget{maximumUpdates}).updated;
    }
    std::vector<glm::ivec3> takeTntIgnitions() {
        return m_simulation.takeTntIgnitions();
    }
    BlockEntity* getBlockEntity(const glm::ivec3& position) {
        return m_persistence.getBlockEntity(position);
    }
    const BlockEntity* getBlockEntity(const glm::ivec3& position) const {
        return m_persistence.getBlockEntity(position);
    }
    std::vector<ItemStack> takeBlockEntityContents(const glm::ivec3& position) {
        return m_persistence.takeBlockEntityContents(position);
    }

    // ── Block queries ────────────────────────────────────────────────
    BlockId getBlock(int worldX, int worldY, int worldZ) const;
    LightSample getLight(int worldX, int worldY, int worldZ) const;
    SmoothLightSample sampleLight(const glm::dvec3& position) const;
    uint8_t getBlockLight(int worldX, int worldY, int worldZ) const;
    uint8_t getSkyLight(int worldX, int worldY, int worldZ) const;
    int getSurfaceY(int worldX, int worldZ) const;
    bool hasSkyAccess(int worldX, int worldY, int worldZ) const;
    PrecipitationType precipitationAt(int worldX, int worldY, int worldZ) const;
    Biome biomeAt(int worldX, int worldZ) const;
    // 0..7 palette index of the exclusive Heaven biome at a column, for the
    // ambient particle tinting; returns 0 outside the Heaven dimension.
    int heavenBiomePaletteIndex(int worldX, int worldZ) const;
    std::optional<glm::ivec2> locateBiome(Biome biome, int worldX,
                                          int worldZ) const;
    std::optional<glm::ivec3> locateStructure(
        StructureType type, int worldX, int worldZ) const;
    glm::dvec3 findSafeSpawn(int maximumRadius = 512) const;
    std::vector<WorldGenerator::VillageSpawnRequest> villageSpawnsForChunk(
        int chunkX, int chunkZ) const {
        return m_generator.villageSpawnsForChunk(chunkX, chunkZ);
    }

    // Sets a block and marks affected chunks dirty
    void setBlock(int worldX, int worldY, int worldZ, BlockId id);
    bool placeBed(const glm::ivec3& foot, BedDirection direction);
    std::optional<glm::ivec3> validBedFoot(const glm::ivec3& position) const;

    // ── Chunk management ─────────────────────────────────────────────
    Chunk* getChunk(int cx, int cz);

    // Clear all chunks and recreate the generator with a new seed/type.
    // Next update() + getChunk() calls regenerate the world from scratch.
    void resetForNewSeed(uint64_t newSeed,
                         WorldType worldType = WorldType::Normal,
                         DimensionId dimension = DimensionId::Overworld);

    // Update chunk loading/unloading around player position
    void update(const glm::dvec3& playerPosition, int loadBudgetOverride = 0,
                const glm::dvec3& playerVelocity = glm::dvec3(0.0)) {
        m_streamer.update(playerPosition, loadBudgetOverride, playerVelocity);
    }

    // ── Async generation pipeline ──────────────────────────────────────
    // Enqueue terrain generation. Groups ungenerated chunks into N×N regions
    // for perfect cross-chunk continuity. Remaining singletons use the old path.
    void enqueueGeneration() { m_streamer.enqueueGeneration(); }

    // Check for newly-generated chunks and apply any pending cross-region
    // tree leaves that were waiting for those chunks to finish.
    void processCompletedGenerations(
        bool rebuildLightingNow = true,
        double mainThreadBudgetMs = Config::STREAMING_MAIN_BUDGET_MS) {
        m_streamer.processCompletedGenerations(rebuildLightingNow,
                                               mainThreadBudgetMs);
    }

    // Spin-wait for initial chunk generation (called once on first startGame)
    void waitForInitialGeneration(int maxWaitMs = 150) {
        m_streamer.waitForInitialGeneration(maxWaitMs);
    }

    using GenerationProgress = StreamingProgress;
    GenerationProgress generationProgress() const {
        return m_streamer.generationProgress();
    }
    GenerationProgress loadingProgress() const {
        return m_streamer.loadingProgress();
    }
    void persistGeneratedChunks() { m_streamer.persistGeneratedChunks(); }
    std::optional<std::vector<WorldMetadata::PersistedEntity>>
    takePrefetchedChunkEntities(int cx, int cz) {
        return m_streamer.takePrefetchedChunkEntities(cx, cz);
    }

    // Enqueue mesh builds for dirty chunks (async via thread pool)
    void enqueueMeshBuilds(
        int maxInFlight = Config::CHUNK_MESH_TASKS_IN_FLIGHT) {
        m_meshes.enqueueMeshBuilds(maxInFlight);
    }

    // Check for completed async mesh builds and upload them to GPU.
    // maxUploads caps GL uploads per frame to avoid pipeline stalls.
    void processCompletedMeshes(IGameRenderer* renderer, int maxUploads = 4,
                                size_t maxUploadBytes =
                                    Config::MESH_UPLOAD_BYTES_PER_FRAME) {
        m_meshes.processCompletedMeshes(renderer, maxUploads, maxUploadBytes);
    }

    // Synchronous build (for first frame or when thread pool unavailable)
    void buildMeshesSync(IGameRenderer* renderer, int maxCount = 16) {
        m_meshes.buildMeshesSync(renderer, maxCount);
    }
    void invalidateGpuMeshes() {
        m_meshes.invalidateGpuMeshes();
        m_lod.releaseGpuMeshes(true);
    }
    void restoreGpuMeshes() { m_meshes.restoreGpuMeshes(); }

    void configureLod(const LodSettings& settings) { m_lod.configure(settings); }
    void updateLod(const glm::dvec3& playerPosition) {
        m_lod.update(playerPosition, Config::RENDER_DISTANCE,
                     m_chunks.activeChunks());
    }
    void processCompletedLod(IGameRenderer* renderer) {
        m_lod.processCompleted(renderer);
    }
    const std::vector<LodRenderSubmission>& lodSubmissions() const {
        return m_lod.submissions();
    }
    bool lodEnabled() const { return m_lod.enabled(); }
    int lodDistanceChunks() const { return m_lod.distanceChunks(); }

    // ── Raycast ──────────────────────────────────────────────────────
    struct RaycastHit {
        glm::ivec3 blockPos;
        glm::ivec3 faceNormal;
        glm::dvec3 hitPosition{0.0};
        double distance = 0.0;
    };
    std::optional<RaycastHit> raycast(const glm::dvec3& origin,
                                      const glm::vec3& direction,
                                      float maxDistance) const;

    // ── Rendering ────────────────────────────────────────────────────
    const std::vector<Chunk*>& getActiveChunks() const {
        return m_chunks.activeChunks();
    }
    bool isGeneratedAt(int worldX, int worldZ) const {
        return generatedAt(worldX, worldZ);
    }
    bool isHeaven() const { return m_generator.isHeaven(); }
    uint64_t streamingRevision() const { return m_streamer.streamingRevision(); }
    bool streamingTargetReady() const { return m_streamer.streamingTargetReady(); }

    // ── Chunk coordinate helpers ─────────────────────────────────────
    static inline int worldToChunkX(double wx) {
        return static_cast<int>(std::floor(wx / Config::CHUNK_SIZE_X));
    }
    static inline int worldToChunkZ(double wz) {
        return static_cast<int>(std::floor(wz / Config::CHUNK_SIZE_Z));
    }

private:
    friend class ChunkMeshPipeline;
    friend class ChunkStreamer;
    friend class FluidScheduler;
    ChunkStore m_chunks;
    WorldPersistence m_persistence{m_chunks};
    FluidScheduler m_fluids{*this};
    ChunkMeshPipeline m_meshes{*this, m_chunks};
    ChunkStreamer m_streamer{*this, m_chunks};
    LodTerrainSystem m_lod;
    WorldLighting m_lighting{m_chunks};
    WorldSimulation m_simulation{*this, m_persistence, m_chunks};

    // ChunkMeshPipeline reads the streaming cursor state for near-to-far
    // mesh prioritization.
    int centerChunkX() const { return m_streamer.centerChunkX(); }
    int centerChunkZ() const { return m_streamer.centerChunkZ(); }
    int meshChunksPerFrame() const { return m_streamer.meshChunksPerFrame(); }

    // ChunkStreamer drives generation and requests lighting work.
    WorldGenerator& generator() { return m_generator; }
    bool lightDirty() const { return m_lighting.dirty(); }
    void markLightDirty() { m_lighting.markDirty(); }
    void rebuildLightingNow() { m_lighting.rebuild(); }

    WorldGenerator m_generator;
    ThreadPool* m_threadPool = nullptr;
    SaveStore* m_saveStore = nullptr;

    struct FluidMutation {
        glm::ivec3 position{0};
        BlockId previous = BlockId::AIR;
        BlockId current = BlockId::AIR;
    };
    struct FluidPositionHash {
        size_t operator()(const glm::ivec3& p) const {
            size_t h = std::hash<int>{}(p.x);
            h ^= std::hash<int>{}(p.y) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<int>{}(p.z) + 0x9e3779b9u + (h << 6) + (h >> 2);
            return h;
        }
    };
    void beginFluidBatch(uint64_t tick);
    const std::vector<FluidMutation>& endFluidBatch();
    uint64_t currentFluidTick() const { return m_currentFluidTick; }
    bool m_fluidBatchActive = false;
    uint64_t m_currentFluidTick = 0;
    std::vector<FluidMutation> m_fluidMutations;
    std::vector<FluidMutation> m_uniqueFluidMutations;
    std::vector<glm::ivec3> m_fluidLightingPositions;
    std::unordered_map<glm::ivec3, size_t, FluidPositionHash>
        m_fluidMutationIndices;

    bool generatedAt(int worldX, int worldZ) const;
    void setBlockInternal(int worldX, int worldY, int worldZ, BlockId id,
                          bool recordOverride);
    void setDerivedBlock(const glm::ivec3& position, BlockId id);
};
