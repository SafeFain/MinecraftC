#pragma once

#include <unordered_map>
#include <memory>
#include <vector>
#include <shared_mutex>
#include <optional>
#include <functional>

#include <glm/glm.hpp>

#include "world/Chunk.h"
#include "world/WorldGenerator.h"
#include "world/RegionGenerationData.h"

class Renderer;
class ThreadPool;

class World {
public:
    World();
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // ── Thread pool ──────────────────────────────────────────────────
    void setThreadPool(ThreadPool* pool) { m_threadPool = pool; }

    // ── Block queries ────────────────────────────────────────────────
    BlockId getBlock(int worldX, int worldY, int worldZ) const;

    // Sets a block and marks affected chunks dirty
    void setBlock(int worldX, int worldY, int worldZ, BlockId id);

    // ── Chunk management ─────────────────────────────────────────────
    Chunk* getChunk(int cx, int cz);

    // Clear all chunks and recreate generator with a new seed.
    // Next update() + getChunk() calls will regenerate world from scratch.
    void resetForNewSeed(uint64_t newSeed);

    // Update chunk loading/unloading around player position
    void update(const glm::vec3& playerPosition);

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
    std::optional<RaycastHit> raycast(const glm::vec3& origin,
                                      const glm::vec3& direction,
                                      float maxDistance) const;

    // ── Rendering ────────────────────────────────────────────────────
    const std::vector<Chunk*>& getActiveChunks() const { return m_activeChunks; }

    // ── Chunk coordinate helpers ─────────────────────────────────────
    static inline int worldToChunkX(float wx) {
        return static_cast<int>(std::floor(wx / Config::CHUNK_SIZE_X));
    }
    static inline int worldToChunkZ(float wz) {
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

    int m_chunksPerFrame = 16;  // First frame loads more
    bool m_firstUpdate = true;

    // ── Pending block queue ───────────────────────────────────────────
    // Cross-region tree leaves that need to be applied when the target
    // chunk finishes generation. Keyed by target chunk coordinates.
    using PendingBlockVec = std::vector<RegionGenerationData::PendingBlock>;
    std::unordered_map<std::pair<int,int>, PendingBlockVec, PairHash> m_pendingBlocks;

    // Apply queued pending blocks to a newly-generated chunk
    void applyPendingBlocks(int cx, int cz);

    void markDirty(int cx, int cz);
};
