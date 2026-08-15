#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Config.h"
#include "world/RegionGenerationData.h"

class ChunkStore;
class SaveStore;
class ThreadPool;
class World;

// Streaming/generation progress counters. World exposes this as
// World::GenerationProgress (a using alias) so existing callers keep working.
struct StreamingProgress {
    size_t completed = 0;
    size_t total = 0;
};

// Owns the chunk streaming and generation pipeline: the desired-chunk set
// around the player, bounded load/unload per update, the async region and
// singleton generation dispatch, completion application (pending blocks,
// saved overrides, block entities), initial-generation wait, progress
// reporting, and base-cache persistence. Block access and lighting/persistence
// side effects go through the owning World; the chunk lock is the
// ChunkStore's, taken per pass.
class ChunkStreamer {
public:
    ChunkStreamer(World& world, ChunkStore& chunks)
        : m_world(world), m_chunks(chunks) {}

    void setThreadPool(ThreadPool* pool) { m_threadPool = pool; }
    void setSaveStore(SaveStore* store) { m_saveStore = store; }

    // Update chunk loading/unloading around the player position.
    void update(const glm::dvec3& playerPosition, int loadBudgetOverride = 0);

    // Enqueue terrain generation. Groups ungenerated chunks into N×N regions
    // for perfect cross-chunk continuity; remaining singletons use the old
    // path.
    void enqueueGeneration();

    // Check for newly-generated chunks and apply any pending cross-region
    // tree leaves that were waiting for those chunks to finish.
    void processCompletedGenerations(bool rebuildLightingNow = true);

    // Spin-wait for initial chunk generation (called once on first startGame).
    void waitForInitialGeneration(int maxWaitMs = 150);

    StreamingProgress generationProgress() const;
    StreamingProgress loadingProgress() const;
    void persistGeneratedChunks();
    uint64_t streamingRevision() const { return m_streamingRevision; }
    bool streamingTargetReady() const {
        return m_streamCursor >= m_desiredChunks.size() &&
               !m_streamCleanupPending;
    }
    int centerChunkX() const { return m_centerChunkX; }
    int centerChunkZ() const { return m_centerChunkZ; }
    int meshChunksPerFrame() const { return m_chunksPerFrame; }

    // Drop streaming and generation state (seed reset / teardown). The
    // streaming revision bumps so dependents observe the world change.
    void clear();

    // Apply queued pending blocks to a newly-generated chunk.
    // Caller must hold the ChunkStore lock.
    void applyPendingBlocksUnlocked(int cx, int cz, ChunkStore& store);

private:
    struct PairHash {
        size_t operator()(const std::pair<int,int>& p) const {
            // Shift through uint64_t: left-shifting a negative int64_t is UB.
            return std::hash<uint64_t>{}(
                (static_cast<uint64_t>(static_cast<uint32_t>(p.first)) << 32) |
                static_cast<uint32_t>(p.second));
        }
    };

    World& m_world;
    ChunkStore& m_chunks;
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
    std::unordered_map<std::pair<int,int>, PendingBlockVec, PairHash>
        m_pendingBlocks;

    static uint64_t packedChunkKey(int cx, int cz) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
               static_cast<uint32_t>(cz);
    }
};
