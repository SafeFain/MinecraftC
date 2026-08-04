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

#include "world/Chunk.h"
#include "world/WorldGenerator.h"
#include "world/RegionGenerationData.h"
#include "world/BlockEntity.h"
#include "world/BlockLightLogic.h"
#include "game/Weather.h"

class Renderer;
class ThreadPool;
class SaveStore;

class World {
public:
    World();
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // ── Thread pool ──────────────────────────────────────────────────
    void setThreadPool(ThreadPool* pool) { m_threadPool = pool; }
    void setSaveStore(SaveStore* store) { m_saveStore = store; }
    bool flushModifiedChunks(size_t maxFiles = std::numeric_limits<size_t>::max());
    void beginModifiedChunkAutosave();
    bool hasModifiedChunks() const {
        return !m_dirtyOverrideChunks.empty() || !m_dirtyBlockEntityChunks.empty() ||
               !m_pendingOverrideSaves.empty() || !m_pendingBlockEntitySaves.empty();
    }
    bool hasPendingModifiedChunkSaves() const {
        return !m_pendingOverrideSaves.empty() ||
               !m_pendingBlockEntitySaves.empty();
    }
    void tickSurvival(const glm::dvec3& playerPosition, uint64_t tick,
                      bool raining = false);
    void tickWeather(const WeatherSystem& weather, bool daytime, uint64_t tick);
    void tickBlockEntities();
    void tickFluids(uint64_t tick);
    std::vector<glm::ivec3> takeTntIgnitions();
    BlockEntity* getBlockEntity(const glm::ivec3& position);
    const BlockEntity* getBlockEntity(const glm::ivec3& position) const;
    std::vector<ItemStack> takeBlockEntityContents(const glm::ivec3& position);

    // ── Block queries ────────────────────────────────────────────────
    BlockId getBlock(int worldX, int worldY, int worldZ) const;
    LightSample getLight(int worldX, int worldY, int worldZ) const;
    SmoothLightSample sampleLight(const glm::dvec3& position) const;
    uint8_t getBlockLight(int worldX, int worldY, int worldZ) const;
    uint8_t getSkyLight(int worldX, int worldY, int worldZ) const;
    int getSurfaceY(int worldX, int worldZ) const;
    bool hasSkyAccess(int worldX, int worldY, int worldZ) const;
    PrecipitationType precipitationAt(int worldX, int worldY, int worldZ) const;

    // Sets a block and marks affected chunks dirty
    void setBlock(int worldX, int worldY, int worldZ, BlockId id);

    // ── Chunk management ─────────────────────────────────────────────
    Chunk* getChunk(int cx, int cz);

    // Clear all chunks and recreate generator with a new seed.
    // Next update() + getChunk() calls will regenerate world from scratch.
    void resetForNewSeed(uint64_t newSeed);

    // Update chunk loading/unloading around player position
    void update(const glm::dvec3& playerPosition, int loadBudgetOverride = 0);

    // ── Async generation pipeline ──────────────────────────────────────
    // Enqueue terrain generation. Groups ungenerated chunks into N×N regions
    // for perfect cross-chunk continuity. Remaining singletons use the old path.
    void enqueueGeneration();

    // Check for newly-generated chunks and apply any pending cross-region
    // tree leaves that were waiting for those chunks to finish.
    void processCompletedGenerations(bool rebuildLightingNow = true);

    // Spin-wait for initial chunk generation (called once on first startGame)
    void waitForInitialGeneration(int maxWaitMs = 150);

    struct GenerationProgress { size_t completed = 0; size_t total = 0; };
    GenerationProgress generationProgress() const;
    GenerationProgress loadingProgress() const;
    void persistGeneratedChunks();

    // Enqueue mesh builds for dirty chunks (async via thread pool)
    void enqueueMeshBuilds(
        int maxInFlight = Config::CHUNK_MESH_TASKS_IN_FLIGHT);

    // Check for completed async mesh builds and upload them to GPU.
    // maxUploads caps GL uploads per frame to avoid pipeline stalls.
    void processCompletedMeshes(Renderer* renderer, int maxUploads = 4,
                                size_t maxUploadBytes =
                                    Config::MESH_UPLOAD_BYTES_PER_FRAME);

    // Synchronous build (for first frame or when thread pool unavailable)
    void buildMeshesSync(Renderer* renderer, int maxCount = 16);
    void invalidateGpuMeshes();
    void restoreGpuMeshes();

    // ── Raycast ──────────────────────────────────────────────────────
    struct RaycastHit {
        glm::ivec3 blockPos;
        glm::ivec3 faceNormal;
    };
    std::optional<RaycastHit> raycast(const glm::dvec3& origin,
                                      const glm::vec3& direction,
                                      float maxDistance) const;

    // ── Rendering ────────────────────────────────────────────────────
    const std::vector<Chunk*>& getActiveChunks() const { return m_activeChunks; }
    uint64_t streamingRevision() const { return m_streamingRevision; }
    bool streamingTargetReady() const {
        return m_streamCursor >= m_desiredChunks.size() &&
               !m_streamCleanupPending;
    }

    // ── Chunk coordinate helpers ─────────────────────────────────────
    static inline int worldToChunkX(double wx) {
        return static_cast<int>(std::floor(wx / Config::CHUNK_SIZE_X));
    }
    static inline int worldToChunkZ(double wz) {
        return static_cast<int>(std::floor(wz / Config::CHUNK_SIZE_Z));
    }

private:
    struct PairHash {
        size_t operator()(const std::pair<int,int>& p) const {
            return std::hash<int64_t>{}((static_cast<int64_t>(p.first) << 32)
                                        | static_cast<uint32_t>(p.second));
        }
    };

    using ChunkMap = std::unordered_map<std::pair<int,int>, std::unique_ptr<Chunk>, PairHash>;
    struct BlockPosHash {
        size_t operator()(const glm::ivec3& p) const {
            size_t h = std::hash<int>{}(p.x);
            h ^= std::hash<int>{}(p.y) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<int>{}(p.z) + 0x9e3779b9u + (h << 6) + (h >> 2);
            return h;
        }
    };
    struct ScheduledFluidTick {
        uint64_t due = 0;
        glm::ivec3 position{0};
    };
    struct ScheduledFluidLater {
        bool operator()(const ScheduledFluidTick& a,
                        const ScheduledFluidTick& b) const {
            if (a.due != b.due) return a.due > b.due;
            if (a.position.y != b.position.y) return a.position.y > b.position.y;
            if (a.position.z != b.position.z) return a.position.z > b.position.z;
            return a.position.x > b.position.x;
        }
    };
    ChunkMap m_chunks;
    std::unordered_map<glm::ivec3, uint8_t, BlockPosHash> m_fireAges;
    std::priority_queue<ScheduledFluidTick, std::vector<ScheduledFluidTick>,
                        ScheduledFluidLater> m_fluidTicks;
    std::unordered_map<glm::ivec3, uint64_t, BlockPosHash> m_scheduledFluidDue;
    std::vector<glm::ivec3> m_tntIgnitions;
    uint64_t m_currentWorldTick = 0;
    std::vector<Chunk*> m_activeChunks;

    mutable std::shared_mutex m_chunkMutex;

    WorldGenerator m_generator;
    ThreadPool* m_threadPool = nullptr;
    SaveStore* m_saveStore = nullptr;

    int m_chunksPerFrame = 16;  // First frame loads more
    bool m_firstUpdate = true;
    int m_centerChunkX = 0;
    int m_centerChunkZ = 0;
    int m_streamCenterChunkX = std::numeric_limits<int>::max();
    int m_streamCenterChunkZ = std::numeric_limits<int>::max();
    int m_streamRenderDistance = -1;
    std::vector<std::pair<int,int>> m_desiredChunks;
    std::unordered_set<uint64_t> m_desiredChunkSet;
    size_t m_streamCursor = 0;
    bool m_streamCleanupPending = false;
    uint64_t m_streamingRevision = 0;
    std::atomic<int> m_generationTasksInFlight{0};

    // ── Pending block queue ───────────────────────────────────────────
    // Cross-region tree leaves that need to be applied when the target
    // chunk finishes generation. Keyed by target chunk coordinates.
    using PendingBlockVec = std::vector<RegionGenerationData::PendingBlock>;
    std::unordered_map<std::pair<int,int>, PendingBlockVec, PairHash> m_pendingBlocks;
    using OverrideMap = std::unordered_map<uint32_t, BlockId>;
    std::unordered_map<std::pair<int,int>, OverrideMap, PairHash> m_blockOverrides;
    std::unordered_set<std::pair<int,int>, PairHash> m_dirtyOverrideChunks;
    std::unordered_set<std::pair<int,int>, PairHash> m_pendingOverrideSaves;
    std::unordered_set<std::pair<int,int>, PairHash> m_overridesApplied;
    using BlockEntityMap = std::unordered_map<uint32_t, BlockEntity>;
    std::unordered_map<std::pair<int,int>, BlockEntityMap, PairHash> m_blockEntities;
    std::unordered_set<std::pair<int,int>, PairHash> m_dirtyBlockEntityChunks;
    std::unordered_set<std::pair<int,int>, PairHash> m_pendingBlockEntitySaves;
    std::unordered_set<std::pair<int,int>, PairHash> m_blockEntitiesApplied;
    bool m_lightDirty = true;
    bool m_lightHasSources = false;

    // Apply queued pending blocks to a newly-generated chunk
    void applyPendingBlocks(int cx, int cz);
    void applySavedOverrides(int cx, int cz);
    void saveOverrides(int cx, int cz);
    void loadBlockEntities(int cx, int cz);
    void saveBlockEntities(int cx, int cz);
    void rebuildLighting();
    void updateLightingAt(const glm::ivec3& position);
    bool growSapling(const glm::ivec3& position, BlockId sapling);
    bool hasWaterForFarmland(const glm::ivec3& position, bool raining = false) const;
    void scheduleFluidAround(const glm::ivec3& position, uint64_t minimumDelay = 1);
    bool generatedAt(int worldX, int worldZ) const;
    void updateFluidCell(const glm::ivec3& position, uint64_t tick);

    void markDirty(int cx, int cz);
    static uint64_t packedChunkKey(int cx, int cz) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
               static_cast<uint32_t>(cz);
    }
};
