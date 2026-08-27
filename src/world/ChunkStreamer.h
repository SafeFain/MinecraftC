#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <glm/glm.hpp>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Config.h"
#include "game/SaveStore.h"
#include "world/RegionGenerationData.h"

class Chunk;
class ChunkStore;
class SaveStore;
class ThreadPool;
class World;

// Streaming/generation progress counters. World exposes this as
// World::GenerationProgress (a using alias) so existing callers keep working.
struct StreamingProgress {
    size_t completed = 0;
    size_t total = 0;
    size_t cacheHits = 0;
    size_t cacheMisses = 0;
    size_t lightingReady = 0;
    size_t meshReady = 0;
    size_t renderable = 0;
};

// A single low-priority I/O lane keeps filesystem work off the gameplay
// thread and prevents cache bursts from stealing all terrain workers.
class ChunkIoQueue {
public:
    ChunkIoQueue() : m_worker([this] { run(); }) {}
    ~ChunkIoQueue() { stop(); }

    ChunkIoQueue(const ChunkIoQueue&) = delete;
    ChunkIoQueue& operator=(const ChunkIoQueue&) = delete;

    void enqueue(std::function<void()> task) {
        {
            std::lock_guard lock(m_mutex);
            if (m_stopping) return;
            m_tasks.push_back(std::move(task));
        }
        m_wake.notify_one();
    }

    void drain() {
        std::unique_lock lock(m_mutex);
        m_done.wait(lock, [this] { return m_tasks.empty() && m_active == 0; });
    }

    void stop() {
        {
            std::lock_guard lock(m_mutex);
            if (m_stopping) return;
            m_stopping = true;
        }
        m_wake.notify_all();
        if (m_worker.joinable()) m_worker.join();
    }

private:
    void run() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock lock(m_mutex);
                m_wake.wait(lock, [this] { return m_stopping || !m_tasks.empty(); });
                if (m_stopping && m_tasks.empty()) return;
                task = std::move(m_tasks.front());
                m_tasks.pop_front();
                ++m_active;
            }
            try { task(); } catch (...) { /* cache work is recoverable */ }
            {
                std::lock_guard lock(m_mutex);
                --m_active;
            }
            m_done.notify_all();
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_done;
    std::deque<std::function<void()>> m_tasks;
    std::thread m_worker;
    size_t m_active = 0;
    bool m_stopping = false;
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
    ~ChunkStreamer();

    void setThreadPool(ThreadPool* pool) { m_threadPool = pool; }
    void setSaveStore(SaveStore* store) { m_saveStore = store; }

    // Update chunk loading/unloading around the player position.
    void update(const glm::dvec3& playerPosition, int loadBudgetOverride = 0,
                const glm::dvec3& playerVelocity = glm::dvec3(0.0));

    // Enqueue terrain generation. Groups ungenerated chunks into N×N regions
    // for perfect cross-chunk continuity; remaining singletons use the old
    // path.
    void enqueueGeneration();

    // Check for newly-generated chunks and apply any pending cross-region
    // tree leaves that were waiting for those chunks to finish.
    void processCompletedGenerations(
        bool rebuildLightingNow = true,
        double mainThreadBudgetMs = Config::STREAMING_MAIN_BUDGET_MS);

    // Spin-wait for initial chunk generation (called once on first startGame).
    void waitForInitialGeneration(int maxWaitMs = 150);

    StreamingProgress generationProgress() const;
    StreamingProgress loadingProgress() const;
    void persistGeneratedChunks();
    std::optional<ChunkEntityLoadData>
    takePrefetchedChunkEntities(int cx, int cz);
    uint64_t streamingRevision() const { return m_streamingRevision; }
    bool streamingTargetReady() const {
        return m_streamCursor >= m_visibleChunkCount &&
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
    bool applyPendingBlocksUnlocked(int cx, int cz, ChunkStore& store);

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
    std::unordered_set<uint64_t> m_visibleChunkSet;
    size_t m_visibleChunkCount = 0;
    std::unordered_set<uint64_t> m_warmChunkSet;
    std::unordered_set<uint64_t> m_boundaryLightingChunks;
    std::deque<std::pair<int, int>> m_warmChunkOrder;
    size_t m_streamCursor = 0;
    bool m_streamCleanupPending = false;
    uint64_t m_streamingRevision = 0;
    std::atomic<int> m_generationTasksInFlight{0};
    std::atomic<int> m_cacheReadTasksInFlight{0};
    std::atomic<int> m_cacheWriteTasksInFlight{0};
    uint64_t m_streamEpoch = 0;
    ChunkIoQueue m_cacheIo;
    std::mutex m_cacheCompletionMutex;
    std::mutex m_generationCompletionMutex;
    std::deque<std::pair<int, int>> m_generationCompletions;

    struct CacheCompletion {
        int cx = 0;
        int cz = 0;
        uint64_t epoch = 0;
        bool hit = false;
        std::vector<uint8_t> blocks;
        std::vector<BlockOverride> overrides;
        std::vector<PersistedBlockEntity> blockEntities;
        std::vector<WorldMetadata::PersistedEntity> entities;
        uint32_t entityPopulationVersion = 0;
    };
    struct CacheWriteCompletion {
        int cx = 0;
        int cz = 0;
        uint64_t epoch = 0;
        bool success = false;
    };
    std::deque<CacheCompletion> m_cacheCompletions;
    std::deque<CacheWriteCompletion> m_cacheWriteCompletions;
    std::unordered_map<std::pair<int, int>, ChunkEntityLoadData, PairHash>
        m_prefetchedEntities;
    size_t m_cacheHitCount = 0;
    size_t m_cacheMissCount = 0;

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

    void enqueueCacheReads();
    void processCacheCompletions();
    void processCacheWriteCompletions();
    void queueGenerationCompletion(int cx, int cz);
    void drainCacheWrites();
    void queueBaseCacheWrite(Chunk* chunk);
    void queueBaseCacheWriteUnlocked(Chunk* chunk);
};
