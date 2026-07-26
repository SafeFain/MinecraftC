#pragma once

#include <unordered_map>
#include <memory>
#include <vector>
#include <shared_mutex>
#include <optional>
#include <functional>
#include <unordered_set>

#include <glm/glm.hpp>

#include "world/Chunk.h"
#include "world/WorldGenerator.h"
#include "world/RegionGenerationData.h"
#include "world/BlockEntity.h"

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
    void flushModifiedChunks();
    void tickSurvival(const glm::dvec3& playerPosition, uint64_t tick);
    void tickBlockEntities();
    BlockEntity* getBlockEntity(const glm::ivec3& position);
    const BlockEntity* getBlockEntity(const glm::ivec3& position) const;
    std::vector<ItemStack> takeBlockEntityContents(const glm::ivec3& position);

    // ── Block queries ────────────────────────────────────────────────
    BlockId getBlock(int worldX, int worldY, int worldZ) const;
    uint8_t getBlockLight(int worldX, int worldY, int worldZ) const;

    // Sets a block and marks affected chunks dirty
    void setBlock(int worldX, int worldY, int worldZ, BlockId id);

    // ── Chunk management ─────────────────────────────────────────────
    Chunk* getChunk(int cx, int cz);

    // Clear all chunks and recreate generator with a new seed.
    // Next update() + getChunk() calls will regenerate world from scratch.
    void resetForNewSeed(uint64_t newSeed);

    // Update chunk loading/unloading around player position
    void update(const glm::dvec3& playerPosition);

    // ── Async generation pipeline ──────────────────────────────────────
    // Enqueue terrain generation. Groups ungenerated chunks into N×N regions
    // for perfect cross-chunk continuity. Remaining singletons use the old path.
    void enqueueGeneration();

    // Check for newly-generated chunks and apply any pending cross-region
    // tree leaves that were waiting for those chunks to finish.
    void processCompletedGenerations();

    // Spin-wait for initial chunk generation (called once on first startGame)
    void waitForInitialGeneration(int maxWaitMs = 150);

    // Enqueue mesh builds for dirty chunks (async via thread pool)
    void enqueueMeshBuilds();

    // Check for completed async mesh builds and upload them to GPU.
    // maxUploads caps GL uploads per frame to avoid pipeline stalls.
    void processCompletedMeshes(Renderer* renderer, int maxUploads = 4);

    // Synchronous build (for first frame or when thread pool unavailable)
    void buildMeshesSync(Renderer* renderer, int maxCount = 16);

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
    ChunkMap m_chunks;
    std::vector<Chunk*> m_activeChunks;

    mutable std::shared_mutex m_chunkMutex;

    WorldGenerator m_generator;
    ThreadPool* m_threadPool = nullptr;
    SaveStore* m_saveStore = nullptr;

    int m_chunksPerFrame = 16;  // First frame loads more
    bool m_firstUpdate = true;

    // ── Pending block queue ───────────────────────────────────────────
    // Cross-region tree leaves that need to be applied when the target
    // chunk finishes generation. Keyed by target chunk coordinates.
    using PendingBlockVec = std::vector<RegionGenerationData::PendingBlock>;
    std::unordered_map<std::pair<int,int>, PendingBlockVec, PairHash> m_pendingBlocks;
    using OverrideMap = std::unordered_map<uint16_t, BlockId>;
    std::unordered_map<std::pair<int,int>, OverrideMap, PairHash> m_blockOverrides;
    std::unordered_set<std::pair<int,int>, PairHash> m_dirtyOverrideChunks;
    std::unordered_set<std::pair<int,int>, PairHash> m_overridesApplied;
    using BlockEntityMap = std::unordered_map<uint16_t, BlockEntity>;
    std::unordered_map<std::pair<int,int>, BlockEntityMap, PairHash> m_blockEntities;
    std::unordered_set<std::pair<int,int>, PairHash> m_dirtyBlockEntityChunks;
    std::unordered_set<std::pair<int,int>, PairHash> m_blockEntitiesApplied;
    bool m_lightDirty = true;

    // Apply queued pending blocks to a newly-generated chunk
    void applyPendingBlocks(int cx, int cz);
    void applySavedOverrides(int cx, int cz);
    void saveOverrides(int cx, int cz);
    void loadBlockEntities(int cx, int cz);
    void saveBlockEntities(int cx, int cz);
    void rebuildBlockLight();

    void markDirty(int cx, int cz);
};
