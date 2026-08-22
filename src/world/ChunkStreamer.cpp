#include "world/ChunkStreamer.h"

#include "core/RuntimeClock.h"
#include "game/SaveStore.h"
#include "threading/ThreadPool.h"
#include "world/Chunk.h"
#include "world/ChunkMesh.h"
#include "world/ChunkStore.h"
#include "world/FluidLogic.h"
#include "world/FluidScheduler.h"
#include "world/World.h"
#include "world/WorldGenContext.h"
#include "world/WorldGenerator.h"
#include "world/WorldPersistence.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

ChunkStreamer::~ChunkStreamer() {
    // Cache writes contain only deterministic, recoverable terrain, but a
    // normal world switch should still finish them before the SaveStore is
    // detached.  Generation workers are drained by the existing session
    // shutdown path.
    drainCacheWrites();
}

void ChunkStreamer::drainCacheWrites() {
    // A single I/O lane may finish one write and schedule the next dirty
    // snapshot from processCacheWriteCompletions().  Drain until that chain is
    // exhausted instead of stopping after the first wave.
    for (;;) {
        m_cacheIo.drain();
        processCacheWriteCompletions();
        bool pending = m_cacheWriteTasksInFlight.load() != 0;
        if (!pending && m_saveStore != nullptr) {
            m_chunks.withShared([&](ChunkStore& store) {
                store.forEachSharedUnlocked([&](const Chunk* chunk) {
                    if (chunk->baseCacheInProgress.load() ||
                        chunk->baseCacheDirty.load())
                        pending = true;
                });
            });
        }
        if (!pending) break;
    }
}

void ChunkStreamer::clear() {
    drainCacheWrites();
    m_firstUpdate = true;
    m_chunksPerFrame = 16;
    m_streamCenterChunkX = std::numeric_limits<int>::max();
    m_streamCenterChunkZ = std::numeric_limits<int>::max();
    m_streamRenderDistance = -1;
    m_desiredChunks.clear();
    m_desiredChunkSet.clear();
    m_visibleChunkSet.clear();
    m_visibleChunkCount = 0;
    m_streamCursor = 0;
    m_streamCleanupPending = false;
    m_pendingBlocks.clear();
    m_warmChunkSet.clear();
    m_boundaryLightingChunks.clear();
    m_warmChunkOrder.clear();
    {
        std::lock_guard lock(m_cacheCompletionMutex);
        m_cacheCompletions.clear();
        m_cacheWriteCompletions.clear();
    }
    {
        std::lock_guard lock(m_generationCompletionMutex);
        m_generationCompletions.clear();
    }
    m_cacheHitCount = 0;
    m_cacheMissCount = 0;
    m_prefetchedEntities.clear();
    ++m_streamEpoch;
    ++m_streamingRevision;
}

void ChunkStreamer::queueGenerationCompletion(int cx, int cz) {
    std::lock_guard lock(m_generationCompletionMutex);
    m_generationCompletions.emplace_back(cx, cz);
}

void ChunkStreamer::update(const glm::dvec3& playerPos, int loadBudgetOverride,
                           const glm::dvec3& playerVelocity) {
    int pcx = World::worldToChunkX(playerPos.x);
    int pcz = World::worldToChunkZ(playerPos.z);
    m_centerChunkX = pcx;
    m_centerChunkZ = pcz;

    const bool targetChanged = pcx != m_streamCenterChunkX ||
        pcz != m_streamCenterChunkZ ||
        Config::RENDER_DISTANCE != m_streamRenderDistance;
    if (targetChanged) {
        m_streamCenterChunkX = pcx;
        m_streamCenterChunkZ = pcz;
        m_streamRenderDistance = Config::RENDER_DISTANCE;
        m_desiredChunks.clear();
        m_desiredChunkSet.clear();
        m_visibleChunkCount = 0;
        m_visibleChunkSet.clear();
        const int r2 = Config::RENDER_DISTANCE * Config::RENDER_DISTANCE;
        for (int dx = -Config::RENDER_DISTANCE; dx <= Config::RENDER_DISTANCE; ++dx) {
            for (int dz = -Config::RENDER_DISTANCE; dz <= Config::RENDER_DISTANCE; ++dz) {
                if (dx * dx + dz * dz > r2) continue;
                const int cx = pcx + dx;
                const int cz = pcz + dz;
                m_desiredChunks.emplace_back(cx, cz);
                m_desiredChunkSet.insert(packedChunkKey(cx, cz));
                m_visibleChunkSet.insert(packedChunkKey(cx, cz));
            }
        }
        m_visibleChunkCount = m_desiredChunks.size();
        std::sort(m_desiredChunks.begin(), m_desiredChunks.end(),
            [pcx, pcz](const auto& a, const auto& b) {
                const int64_t adx = static_cast<int64_t>(a.first) - pcx;
                const int64_t adz = static_cast<int64_t>(a.second) - pcz;
                const int64_t bdx = static_cast<int64_t>(b.first) - pcx;
                const int64_t bdz = static_cast<int64_t>(b.second) - pcz;
                return adx * adx + adz * adz < bdx * bdx + bdz * bdz;
            });

        // Add a very small forward prefetch strip outside the visible circle.
        // It is deliberately excluded from the loading progress total so a
        // player can enter the world as soon as the actual render target is
        // ready while the next movement direction is prepared in the
        // background.
        const glm::dvec2 horizontal(playerVelocity.x, playerVelocity.z);
        if (glm::length(horizontal) > 0.25) {
            const glm::dvec2 direction = glm::normalize(horizontal);
            const int stepX = direction.x > 0.35 ? 1 : direction.x < -0.35 ? -1 : 0;
            const int stepZ = direction.y > 0.35 ? 1 : direction.y < -0.35 ? -1 : 0;
            const int ahead = Config::RENDER_DISTANCE + Config::CHUNK_PREFETCH_AHEAD;
            for (int side = -1; side <= 1; ++side) {
                int cx = pcx + stepX * ahead;
                int cz = pcz + stepZ * ahead;
                if (stepX != 0 && stepZ == 0) cz += side;
                else if (stepZ != 0 && stepX == 0) cx += side;
                else if (stepX != 0 && stepZ != 0) {
                    cx += side * -stepZ;
                    cz += side * stepX;
                }
                if (stepX == 0 && stepZ == 0) continue;
                const uint64_t key = packedChunkKey(cx, cz);
                if (m_desiredChunkSet.insert(key).second)
                    m_desiredChunks.emplace_back(cx, cz);
            }
        }
        m_streamCursor = 0;
        m_streamCleanupPending = true;
        ++m_streamEpoch;
    }

    bool activeChanged = false;
    if (m_streamCleanupPending) {
        m_chunks.withUnique([&](ChunkStore& store) {
            std::vector<std::pair<int,int>> toRemove;
            toRemove.reserve(Config::CHUNK_UNLOADS_PER_FRAME);
            bool cleanupRemaining = false;
            store.forEachUniqueUnlocked([&](Chunk* chunk) {
                const std::pair<int,int> key{chunk->cx, chunk->cz};
                if (m_desiredChunkSet.count(
                        packedChunkKey(key.first, key.second)) != 0)
                    return;
                if (m_warmChunkSet.count(packedChunkKey(key.first, key.second)) != 0)
                    return;
                if (chunk->meshInProgress.load() ||
                    chunk->generationInProgress.load() ||
                    chunk->cacheReadInProgress.load() ||
                    chunk->baseCacheInProgress.load())
                    cleanupRemaining = true;
                else if (chunk->generated.load() &&
                         std::abs(key.first - pcx) <= Config::RENDER_DISTANCE + 2 &&
                         std::abs(key.second - pcz) <= Config::RENDER_DISTANCE + 2 &&
                         static_cast<int>(m_warmChunkOrder.size()) <
                             Config::CHUNK_WARM_CACHE_LIMIT) {
                    // Keep a small CPU-only hysteresis ring for short
                    // backtracks.  GPU handles are released, and the dirty
                    // flag causes a safe rebuild when the chunk is promoted.
                    m_world.m_persistence.saveOverrides(key.first, key.second);
                    m_world.m_persistence.saveBlockEntities(key.first, key.second);
                    m_world.m_meshes.releaseChunkMesh(chunk);
                    chunk->markDirty();
                    chunk->lifecycle = Chunk::LifecycleState::Warm;
                    const uint64_t packed = packedChunkKey(key.first, key.second);
                    m_warmChunkSet.insert(packed);
                    m_warmChunkOrder.push_back(key);
                    activeChanged = true;
                }
                else if (static_cast<int>(toRemove.size()) <
                         Config::CHUNK_UNLOADS_PER_FRAME)
                    toRemove.push_back(key);
                else
                    cleanupRemaining = true;
            });
            for (const auto& key : toRemove) {
                Chunk* chunk = store.findUnlocked(key.first, key.second);
                if (chunk == nullptr) continue;
                // Retained chunks keep their derived light. Unloading an
                // out-of-range neighbor does not change world blocks, and
                // invalidating the boundary strip here caused a synchronous
                // relight whenever the player crossed a 16-block boundary.
                m_world.m_persistence.saveOverrides(key.first, key.second);
                m_world.m_persistence.saveBlockEntities(key.first, key.second);
                m_world.m_meshes.releaseChunkMesh(chunk);
                store.eraseUnlocked(key.first, key.second);
                m_world.m_persistence.eraseOverridesApplied(
                    key.first, key.second);
                m_world.m_persistence.eraseBlockEntities(key.first, key.second);
                m_world.m_persistence.eraseBlockEntitiesApplied(
                    key.first, key.second);
                m_prefetchedEntities.erase(key);
                m_boundaryLightingChunks.erase(
                    packedChunkKey(key.first, key.second));
                activeChanged = true;
            }
            while (static_cast<int>(m_warmChunkOrder.size()) >
                   Config::CHUNK_WARM_CACHE_LIMIT) {
                const auto key = m_warmChunkOrder.front();
                m_warmChunkOrder.pop_front();
                m_warmChunkSet.erase(packedChunkKey(key.first, key.second));
                Chunk* chunk = store.findUnlocked(key.first, key.second);
                if (chunk == nullptr) continue;
                m_world.m_persistence.saveOverrides(key.first, key.second);
                m_world.m_persistence.saveBlockEntities(key.first, key.second);
                m_world.m_meshes.releaseChunkMesh(chunk);
                store.eraseUnlocked(key.first, key.second);
                m_world.m_persistence.eraseOverridesApplied(key.first, key.second);
                m_world.m_persistence.eraseBlockEntities(key.first, key.second);
                m_world.m_persistence.eraseBlockEntitiesApplied(key.first, key.second);
                m_prefetchedEntities.erase(key);
                m_boundaryLightingChunks.erase(
                    packedChunkKey(key.first, key.second));
                activeChanged = true;
            }
            m_streamCleanupPending = cleanupRemaining;
        });
    }

    const int loadBudget = loadBudgetOverride > 0 ? loadBudgetOverride :
        (m_firstUpdate ? Config::INITIAL_CHUNK_LOADS_PER_FRAME
                       : Config::CHUNK_LOADS_PER_FRAME);
    m_firstUpdate = false;
    m_chunksPerFrame = Config::CHUNK_LOADS_PER_FRAME;
    int loaded = 0;
    while (m_streamCursor < m_desiredChunks.size() && loaded < loadBudget) {
        const auto key = m_desiredChunks[m_streamCursor++];
        if (!m_chunks.contains(key.first, key.second)) {
            m_world.getChunk(key.first, key.second);
            ++loaded;
            activeChanged = true;
        } else if (m_warmChunkSet.erase(packedChunkKey(key.first, key.second)) != 0) {
            m_warmChunkOrder.erase(std::remove(
                m_warmChunkOrder.begin(), m_warmChunkOrder.end(), key),
                m_warmChunkOrder.end());
            if (Chunk* chunk = m_chunks.find(key.first, key.second)) {
                chunk->lifecycle = chunk->generated.load()
                    ? Chunk::LifecycleState::WaitingForMesh
                    : Chunk::LifecycleState::Requested;
                chunk->markDirty();
            }
            activeChanged = true;
        }
    }

    if (activeChanged) {
        // Keep chunks whose asynchronous unload is still pending visible for
        // one last frame.  This preserves the old safety invariant that an
        // out-of-range chunk is not reported as gone before its override and
        // entity snapshots have been handed to the save queue.
        m_chunks.rebuildActiveChunks(
            pcx, pcz, m_streamCleanupPending ? nullptr : &m_visibleChunkSet);
        ++m_streamingRevision;
    }
}

void ChunkStreamer::enqueueCacheReads() {
    const int available = Config::CHUNK_CACHE_READ_TASKS_IN_FLIGHT -
        m_cacheReadTasksInFlight.load();
    if (available <= 0) return;

    std::vector<std::pair<int, int>> candidates;
    m_chunks.withShared([&](ChunkStore& store) {
        for (const auto& key : m_desiredChunks) {
            const Chunk* chunk = store.findUnlocked(key.first, key.second);
            if (chunk == nullptr || chunk->generated.load() ||
                chunk->cacheChecked.load() || chunk->cacheReadInProgress.load())
                continue;
            candidates.push_back(key);
            if (static_cast<int>(candidates.size()) >= available) break;
        }
    });

    if (!m_saveStore) {
        m_chunks.withUnique([&](ChunkStore& store) {
            for (const auto& key : candidates) {
                Chunk* chunk = store.findUnlocked(key.first, key.second);
                if (chunk == nullptr) continue;
                chunk->cacheChecked = true;
                chunk->lifecycle = Chunk::LifecycleState::Requested;
            }
        });
        return;
    }

    const auto directory = m_saveStore->worldDirectory();
    const uint64_t epoch = m_streamEpoch;
    for (const auto& key : candidates) {
        bool claimed = false;
        m_chunks.withUnique([&](ChunkStore& store) {
            Chunk* chunk = store.findUnlocked(key.first, key.second);
            if (chunk == nullptr || chunk->generated.load() ||
                chunk->cacheChecked.load() || chunk->cacheReadInProgress.exchange(true))
                return;
            chunk->lifecycle = Chunk::LifecycleState::CacheReading;
            claimed = true;
        });
        if (!claimed) continue;
        ++m_cacheReadTasksInFlight;
        ChunkStreamer* streamer = this;
        m_cacheIo.enqueue([streamer, directory, key, epoch] {
            bool hit = false;
            std::vector<uint8_t> blocks;
            std::vector<BlockOverride> overrides;
            std::vector<PersistedBlockEntity> blockEntities;
            std::vector<WorldMetadata::PersistedEntity> entities;
            try {
                SaveStore loader(directory);
                ChunkLoadBundle bundle = loader.loadChunkLoadBundle(
                    key.first, key.second,
                    WorldGenContext::CHUNK_CACHE_VERSION);
                if (bundle.generated) {
                    hit = true;
                    blocks = std::move(*bundle.generated);
                }
                overrides = std::move(bundle.overrides);
                blockEntities = std::move(bundle.blockEntities);
                entities = std::move(bundle.entities);
            } catch (...) {
                // A corrupt cache is equivalent to a miss; deterministic
                // generation will repair it when the chunk is published.
            }
            {
                std::lock_guard lock(streamer->m_cacheCompletionMutex);
                streamer->m_cacheCompletions.push_back(
                    {key.first, key.second, epoch, hit, std::move(blocks),
                     std::move(overrides), std::move(blockEntities),
                     std::move(entities)});
            }
            --streamer->m_cacheReadTasksInFlight;
        });
    }
}

void ChunkStreamer::processCacheCompletions() {
    std::deque<CacheCompletion> completed;
    {
        std::lock_guard lock(m_cacheCompletionMutex);
        completed.swap(m_cacheCompletions);
    }
    if (completed.empty()) return;

    bool changed = false;
    m_chunks.withUnique([&](ChunkStore& store) {
        for (auto& result : completed) {
            Chunk* chunk = store.findUnlocked(result.cx, result.cz);
            if (chunk == nullptr || !chunk->cacheReadInProgress.load()) continue;
            m_world.m_persistence.installLoadedChunkDataUnlocked(
                result.cx, result.cz, result.overrides, result.blockEntities);
            m_prefetchedEntities[{result.cx, result.cz}] =
                std::move(result.entities);
            chunk->cacheReadInProgress = false;
            chunk->cacheChecked = true;
            chunk->cacheHit = result.hit;
            if (result.hit) {
                chunk->loadRawBlocks(result.blocks);
                chunk->generated = true;
                chunk->generationInProgress = false;
                chunk->lifecycle = Chunk::LifecycleState::LocalLighting;
                queueGenerationCompletion(result.cx, result.cz);
                ++m_cacheHitCount;
                changed = true;
            } else {
                chunk->lifecycle = Chunk::LifecycleState::Requested;
                ++m_cacheMissCount;
            }
        }
    });
    if (changed) ++m_streamingRevision;
}

void ChunkStreamer::processCacheWriteCompletions() {
    std::deque<CacheWriteCompletion> completed;
    {
        std::lock_guard lock(m_cacheCompletionMutex);
        completed.swap(m_cacheWriteCompletions);
    }
    for (const auto& result : completed) {
        Chunk* chunk = nullptr;
        m_chunks.withUnique([&](ChunkStore& store) {
            chunk = store.findUnlocked(result.cx, result.cz);
            if (chunk == nullptr) return;
            chunk->baseCacheInProgress = false;
            if (result.success && !chunk->baseCacheDirty.load())
                chunk->clearBaseSnapshot();
            if (chunk->baseCacheDirty.load()) {
                chunk->baseCacheDirty = false;
                queueBaseCacheWriteUnlocked(chunk);
            }
        });
    }
    if (m_cacheWriteTasksInFlight.load() < Config::CHUNK_CACHE_WRITE_TASKS_IN_FLIGHT) {
        m_chunks.withUnique([&](ChunkStore& store) {
            store.forEachUniqueUnlocked([&](Chunk* chunk) {
                if (m_cacheWriteTasksInFlight.load() >=
                    Config::CHUNK_CACHE_WRITE_TASKS_IN_FLIGHT) return;
                if (chunk->baseCacheDirty.load() &&
                    !chunk->baseCacheInProgress.load()) {
                    chunk->baseCacheDirty = false;
                    queueBaseCacheWriteUnlocked(chunk);
                }
            });
        });
    }
}

void ChunkStreamer::queueBaseCacheWrite(Chunk* chunk) {
    if (chunk == nullptr) return;
    m_chunks.withUnique([&](ChunkStore&) { queueBaseCacheWriteUnlocked(chunk); });
}

void ChunkStreamer::queueBaseCacheWriteUnlocked(Chunk* chunk) {
    if (!m_saveStore || chunk == nullptr || !chunk->hasBaseSnapshot()) return;
    if (chunk->baseCacheInProgress.exchange(true)) {
        chunk->baseCacheDirty = true;
        return;
    }
    if (m_cacheWriteTasksInFlight.load() >= Config::CHUNK_CACHE_WRITE_TASKS_IN_FLIGHT) {
        chunk->baseCacheInProgress = false;
        chunk->baseCacheDirty = true;
        return;
    }
    const auto directory = m_saveStore->worldDirectory();
    const int cx = chunk->cx;
    const int cz = chunk->cz;
    const uint64_t epoch = m_streamEpoch;
    const std::vector<uint8_t> blocks = chunk->baseSnapshot();
    ++m_cacheWriteTasksInFlight;
    ChunkStreamer* streamer = this;
    m_cacheIo.enqueue([streamer, directory, cx, cz, epoch, blocks] {
        bool success = false;
        try {
            SaveStore saver(directory);
            saver.saveGeneratedChunk(cx, cz, blocks,
                                     WorldGenContext::CHUNK_CACHE_VERSION);
            success = true;
        } catch (...) {
            // Cache writes are best effort; terrain remains deterministic.
        }
        {
            std::lock_guard lock(streamer->m_cacheCompletionMutex);
            streamer->m_cacheWriteCompletions.push_back(
                {cx, cz, epoch, success});
        }
        --streamer->m_cacheWriteTasksInFlight;
    });
}

void ChunkStreamer::enqueueGeneration() {
    if (!m_threadPool) return;

    processCacheCompletions();
    processCacheWriteCompletions();
    enqueueCacheReads();

    const int workerCount = static_cast<int>(m_threadPool->threadCount());
    const int boundedGenerationTasks = std::min(
        Config::CHUNK_GEN_TASKS_IN_FLIGHT,
        std::max(1, workerCount > 1 ? workerCount - 1 : 1));
    const int taskSlots = boundedGenerationTasks -
        m_generationTasksInFlight.load();
    if (taskSlots <= 0) return;

    std::vector<std::pair<int,int>> ungenerated;
    struct RegionTask {
        int originCX, originCZ;
        std::vector<Chunk*> chunks;  // row-major: [lcz * R + lcx]
    };
    std::vector<RegionTask> regions;
    std::unordered_set<uint64_t> available;
    std::unordered_set<uint64_t> visited;
    const int R = Config::REGION_SIZE_CHUNKS;  // 3
    const int PADDING = Config::REGION_PADDING;

    m_chunks.withShared([&](ChunkStore& store) {
        // Collect all ungenerated, not-in-progress chunk coords
        store.forEachUniqueUnlocked([&](Chunk* chunk) {
            if (chunk->cacheChecked.load() && !chunk->generated.load() &&
                !chunk->generationInProgress.load()) {
                ungenerated.push_back({chunk->cx, chunk->cz});
            }
        });
        if (ungenerated.empty()) return;
        std::sort(ungenerated.begin(), ungenerated.end(), [this](const auto& a, const auto& b) {
            const int64_t adx = static_cast<int64_t>(a.first) - m_centerChunkX;
            const int64_t adz = static_cast<int64_t>(a.second) - m_centerChunkZ;
            const int64_t bdx = static_cast<int64_t>(b.first) - m_centerChunkX;
            const int64_t bdz = static_cast<int64_t>(b.second) - m_centerChunkZ;
            return adx * adx + adz * adz < bdx * bdx + bdz * bdz;
        });

        // Build a set for fast lookup
        for (auto& [cx, cz] : ungenerated) {
            available.insert(packedChunkKey(cx, cz));
        }

        // Greedy: try to form R×R regions from ungenerated chunks
        for (auto& [cx, cz] : ungenerated) {
            if (static_cast<int>(regions.size()) >= taskSlots) break;
            uint64_t key = packedChunkKey(cx, cz);
            if (visited.count(key)) continue;

            // Check if a full R×R region is available starting at (cx, cz)
            RegionTask region;
            region.originCX = cx;
            region.originCZ = cz;
            bool complete = true;

            for (int dcz = 0; dcz < R && complete; ++dcz) {
                for (int dcx = 0; dcx < R && complete; ++dcx) {
                    uint64_t nkey = packedChunkKey(cx + dcx, cz + dcz);
                    if (!available.count(nkey) || visited.count(nkey)) {
                        complete = false;
                        break;
                    }
                    // Get the chunk pointer
                    Chunk* chunk = store.findUnlocked(cx + dcx, cz + dcz);
                    if (chunk == nullptr) { complete = false; break; }
                    region.chunks.push_back(chunk);
                }
            }

            if (complete) {
                // Mark all chunks in region as visited and in-progress
                for (int dcz = 0; dcz < R; ++dcz) {
                    for (int dcx = 0; dcx < R; ++dcx) {
                        uint64_t nkey = packedChunkKey(cx + dcx, cz + dcz);
                        visited.insert(nkey);
                        store.findUnlocked(cx + dcx, cz + dcz)
                            ->generationInProgress = true;
                        store.findUnlocked(cx + dcx, cz + dcz)
                            ->lifecycle = Chunk::LifecycleState::Generating;
                    }
                }
                regions.push_back(std::move(region));
            }
        }
    });

    // Enqueue region tasks
    for (auto& reg : regions) {
        ChunkStreamer* streamerPtr = this;
        WorldGenerator* genPtr = &m_world.generator();
        int regionDistance2 = std::numeric_limits<int>::max();
        for (const Chunk* chunk : reg.chunks) {
            const int dx = chunk->cx - m_centerChunkX;
            const int dz = chunk->cz - m_centerChunkZ;
            regionDistance2 = std::min(regionDistance2, dx * dx + dz * dz);
        }

        ++m_generationTasksInFlight;
        m_threadPool->enqueuePriority([streamerPtr, genPtr, reg = std::move(reg), R, PADDING]() {
            struct Completion {
                std::atomic<int>& count;
                ~Completion() { --count; }
            } completion{streamerPtr->m_generationTasksInFlight};
            // Clone chunk pointers (non-const because we need to mutate)
            auto chunks = reg.chunks;

            std::vector<RegionGenerationData::PendingBlock> pendingOut;
            genPtr->generateRegion(reg.originCX, reg.originCZ, R, PADDING,
                                   chunks, pendingOut);

            // Store pending blocks under the chunk mutex
            streamerPtr->m_chunks.withUnique([&](ChunkStore&) {
                for (auto& pb : pendingOut) {
                    int tcx = World::worldToChunkX(static_cast<double>(pb.worldX));
                    int tcz = World::worldToChunkZ(static_cast<double>(pb.worldZ));
                    streamerPtr->m_pendingBlocks[{tcx, tcz}].push_back(pb);
                }
            });
            for (const Chunk* chunk : reg.chunks) {
                if (chunk != nullptr)
                    streamerPtr->queueGenerationCompletion(chunk->cx, chunk->cz);
            }
            // A region can finish after one of its cross-region leaves' target
            // chunks was already published.  Requeue those targets so the
            // completion consumer applies the decoration without scanning all
            // resident chunks every frame.
            std::unordered_set<uint64_t> pendingTargets;
            for (const auto& pb : pendingOut) {
                const int tcx = World::worldToChunkX(
                    static_cast<double>(pb.worldX));
                const int tcz = World::worldToChunkZ(
                    static_cast<double>(pb.worldZ));
                pendingTargets.insert(streamerPtr->packedChunkKey(tcx, tcz));
            }
            for (const uint64_t packed : pendingTargets) {
                const int tcx = static_cast<int>(static_cast<int32_t>(packed >> 32));
                const int tcz = static_cast<int>(static_cast<int32_t>(packed & 0xffffffffu));
                streamerPtr->queueGenerationCompletion(tcx, tcz);
            }
        }, 1000000 - regionDistance2);
    }

    // Remaining ungenerated chunks (not part of any region) — legacy singleton path
    int scheduledTasks = static_cast<int>(regions.size());
    m_chunks.withShared([&](ChunkStore& store) {
        for (auto& [cx, cz] : ungenerated) {
            if (scheduledTasks >= taskSlots) break;
            uint64_t key = packedChunkKey(cx, cz);
            if (visited.count(key)) continue;

            Chunk* chunkPtr = store.findUnlocked(cx, cz);
            if (chunkPtr == nullptr) continue;
            if (chunkPtr->generated.load() || chunkPtr->generationInProgress.load()) continue;

            chunkPtr->generationInProgress = true;
            chunkPtr->lifecycle = Chunk::LifecycleState::Generating;
            visited.insert(key);

            WorldGenerator* genPtr = &m_world.generator();

            // Legacy neighborQuery for singleton chunks
            auto neighborQuery = [this, genPtr](int wx, int wz) -> std::optional<HeightBiome> {
                int ncx = World::worldToChunkX(static_cast<double>(wx));
                int ncz = World::worldToChunkZ(static_cast<double>(wz));
                bool available = false;
                m_chunks.withShared([&](ChunkStore& store) {
                    const Chunk* neighbor = store.findUnlocked(ncx, ncz);
                    available = neighbor != nullptr && neighbor->generated.load();
                });
                if (!available) return std::nullopt;
                return genPtr->queryHeightBiome(wx, wz);
            };

            auto blockSetter = [this](int wx, int wy, int wz, BlockId id) {
                if (!Config::isValidWorldY(wy)) return;
                int bsx = World::worldToChunkX(static_cast<double>(wx));
                int bsz = World::worldToChunkZ(static_cast<double>(wz));
                int lx = wx - bsx * Config::CHUNK_SIZE_X;
                int lz = wz - bsz * Config::CHUNK_SIZE_Z;
                if (lx < 0) { bsx -= 1; lx += Config::CHUNK_SIZE_X; }
                if (lz < 0) { bsz -= 1; lz += Config::CHUNK_SIZE_Z; }
                m_chunks.withUnique([&](ChunkStore& store) {
                    Chunk* target = store.findUnlocked(bsx, bsz);
                    if (target != nullptr && target->generated.load()) {
                        target->setBlock(lx, wy, lz, id);
                        queueGenerationCompletion(bsx, bsz);
                    } else {
                        m_pendingBlocks[{bsx, bsz}].push_back({wx, wy, wz, id});
                    }
                });
            };

            ChunkStreamer* streamerPtr = this;
            ++m_generationTasksInFlight;
            ++scheduledTasks;
            const int dx = cx - m_centerChunkX;
            const int dz = cz - m_centerChunkZ;
            const int distance2 = dx * dx + dz * dz;
            m_threadPool->enqueuePriority([streamerPtr, chunkPtr, genPtr, neighborQuery, blockSetter]() {
                struct Completion {
                    std::atomic<int>& count;
                    ~Completion() { --count; }
                } completion{streamerPtr->m_generationTasksInFlight};
                genPtr->generate(*chunkPtr, neighborQuery, blockSetter);
                chunkPtr->generated = true;
                chunkPtr->generationInProgress = false;
                streamerPtr->queueGenerationCompletion(chunkPtr->cx, chunkPtr->cz);
            }, 1000000 - distance2);
        }
    });
}

void ChunkStreamer::processCompletedGenerations(bool rebuildLightingNow,
                                                double mainThreadBudgetMs) {
    processCacheCompletions();
    processCacheWriteCompletions();
    // Apply pending tree leaves for chunks that have finished generating
    std::vector<glm::ivec3> fluidSeeds;
    std::deque<std::pair<int, int>> completed;
    {
        std::lock_guard lock(m_generationCompletionMutex);
        completed.swap(m_generationCompletions);
    }
    std::unordered_set<uint64_t> completedKeys;
    std::vector<std::pair<int, int>> uniqueCompleted;
    uniqueCompleted.reserve(completed.size());
    for (const auto& key : completed) {
        if (completedKeys.insert(packedChunkKey(key.first, key.second)).second)
            uniqueCompleted.push_back(key);
    }
    bool generationStateChanged = false;
    bool budgetExhausted = false;
    RuntimeClock budgetClock;
    const auto budgetStart = budgetClock.now();
    const double effectiveBudgetMs = rebuildLightingNow ? mainThreadBudgetMs : 0.0;
    m_chunks.withUnique([&](ChunkStore& store) {
        // Persisted edits are always fluid wake-up sources at chunk load time.
        // AIR edits are retained because they can expose a natural fluid
        // across the newly available seam.
        auto collectFluidSeeds = [&](int cx, int cz, int edgeX, int edgeZ) {
            const Chunk* source = store.findUnlocked(cx, cz);
            if (source == nullptr || !source->generated.load()) return;
            m_world.m_persistence.forEachOverrideInChunkUnlocked(
                cx, cz, [&](uint32_t index, BlockId block) {
                    int x = 0, z = 0, y = 0;
                    decodeChunkIndex(index, x, z, y);
                    if (edgeX >= 0 && x != edgeX) return;
                    if (edgeZ >= 0 && z != edgeZ) return;
                    // Ordinary solid edits do not create a new flow source.
                    if (!isFluid(block) && block != BlockId::AIR) return;
                    fluidSeeds.push_back({
                        cx * Config::CHUNK_SIZE_X + x, y,
                        cz * Config::CHUNK_SIZE_Z + z});
                });
        };
        // Generated fluids are terrain, but their exposed surface cells must
        // still wake when a chunk is first attached.  Looking only at the
        // highest non-air cell of each column avoids the old 384-level seam
        // scan and avoids scheduling every submerged ocean cell.  Interior
        // cells are queued only when a local neighbor can receive a flow;
        // every edge fluid is queued so a later neighbor can continue a
        // waterfall across the seam.
        auto collectGeneratedFluidSeeds = [&](int cx, int cz,
                                               int edgeX, int edgeZ) {
            const Chunk* source = store.findUnlocked(cx, cz);
            if (source == nullptr || !source->generated.load()) return;
            const bool edgeScan = edgeX >= 0 || edgeZ >= 0;
            for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
                if (edgeZ >= 0 && z != edgeZ) continue;
                for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
                    if (edgeX >= 0 && x != edgeX) continue;
                    const int y = source->getColumnMaxY(x, z);
                    if (!Config::isValidWorldY(y)) continue;
                    const BlockId block = source->getBlock(x, y, z);
                    const auto state = decodeFluidState(block);
                    if (!state.has_value()) continue;

                    bool wake = edgeScan || x == 0 ||
                        x == Config::CHUNK_SIZE_X - 1 || z == 0 ||
                        z == Config::CHUNK_SIZE_Z - 1;
                    const uint8_t amount = state->falling
                        ? static_cast<uint8_t>(8) : state->amount;
                    if (!wake) {
                        for (const glm::ivec3& offset :
                             FLUID_HORIZONTAL_OFFSETS) {
                            const int nx = x + offset.x;
                            const int nz = z + offset.z;
                            if (nx < 0 || nx >= Config::CHUNK_SIZE_X ||
                                nz < 0 || nz >= Config::CHUNK_SIZE_Z)
                                continue;
                            if (fluidCanReceiveAmount(
                                    source->getBlock(nx, y, nz),
                                    state->lava, amount, state->falling)) {
                                wake = true;
                                break;
                            }
                        }
                    }
                    if (!wake && y > Config::WORLD_MIN_Y &&
                        fluidCanReceiveAmount(
                            source->getBlock(x, y - 1, z), state->lava, 8,
                            true)) {
                        wake = true;
                    }
                    if (wake) {
                        fluidSeeds.push_back({
                            cx * Config::CHUNK_SIZE_X + x, y,
                            cz * Config::CHUNK_SIZE_Z + z});
                    }
                }
            }
        };
        for (const auto& key : uniqueCompleted) {
            if (effectiveBudgetMs > 0.0 &&
                RuntimeClock::seconds(RuntimeClock::elapsed(
                    budgetStart, budgetClock.now())) * 1000.0 >= effectiveBudgetMs) {
                budgetExhausted = true;
                break;
            }
            Chunk* chunk = store.findUnlocked(key.first, key.second);
            if (chunk == nullptr) continue;
            if (!chunk->generated.load()) continue;
            const bool firstApply =
                !m_world.m_persistence.isOverridesApplied(key.first, key.second);
            const bool pendingChanged = applyPendingBlocksUnlocked(
                key.first, key.second, store);
            if (!firstApply && !pendingChanged) continue;
            if (firstApply)
                chunk->captureBaseSnapshot();
            if (chunk->hasBaseSnapshot())
                queueBaseCacheWriteUnlocked(chunk);
            if (firstApply) {
                m_world.m_persistence.applySavedOverridesUnlocked(
                    key.first, key.second);
                m_world.m_persistence.loadBlockEntities(key.first, key.second);
            }
            if (firstApply) {
                generationStateChanged = true;
                m_world.markLightDirty();
                // Existing meshes may have sampled this not-yet-generated
                // chunk as air with zero light.  Invalidate the complete
                // one-voxel dependency footprint, including diagonals used by
                // corner AO and smooth lighting.
                for (const auto& offset :
                     ChunkMesh::NEIGHBOR_DEPENDENCY_OFFSETS) {
                    Chunk* neighbor = store.findUnlocked(
                        key.first + offset[0], key.second + offset[1]);
                    if (neighbor != nullptr && neighbor->generated.load()) {
                        neighbor->markDirty();
                    }
                }
                // Wake every persisted edit in the newly generated chunk,
                // plus only the facing edge edits in its already-generated
                // neighbors.  An AIR edit is intentionally included: it can
                // expose a natural fluid on the other side of the seam.
                collectFluidSeeds(key.first, key.second, -1, -1);
                collectFluidSeeds(key.first - 1, key.second,
                                  Config::CHUNK_SIZE_X - 1, -1);
                collectFluidSeeds(key.first + 1, key.second, 0, -1);
                collectFluidSeeds(key.first, key.second - 1,
                                  -1, Config::CHUNK_SIZE_Z - 1);
                collectFluidSeeds(key.first, key.second + 1, -1, 0);

                // Re-activate exposed natural surface fluids without
                // inspecting every block in the 384-high boundary strips.
                collectGeneratedFluidSeeds(key.first, key.second, -1, -1);
                collectGeneratedFluidSeeds(key.first - 1, key.second,
                                           Config::CHUNK_SIZE_X - 1, -1);
                collectGeneratedFluidSeeds(key.first + 1, key.second, 0, -1);
                collectGeneratedFluidSeeds(key.first, key.second - 1,
                                           -1, Config::CHUNK_SIZE_Z - 1);
                collectGeneratedFluidSeeds(key.first, key.second + 1, -1, 0);
            } else if (pendingChanged) {
                generationStateChanged = true;
                m_world.markLightDirty();
                for (const auto& offset :
                     ChunkMesh::NEIGHBOR_DEPENDENCY_OFFSETS) {
                    Chunk* neighbor = store.findUnlocked(
                        key.first + offset[0], key.second + offset[1]);
                    if (neighbor != nullptr && neighbor->generated.load())
                        neighbor->markDirty();
                }
            }
            chunk->lifecycle = Chunk::LifecycleState::BoundaryLighting;
            m_boundaryLightingChunks.insert(
                packedChunkKey(key.first, key.second));
        }
    });
    if (generationStateChanged) ++m_streamingRevision;
    for (const glm::ivec3& position : fluidSeeds)
        m_world.m_fluids.scheduleAround(position);
    if (rebuildLightingNow && !budgetExhausted && m_world.lightDirty())
        m_world.rebuildLightingNow();
    if (rebuildLightingNow && !budgetExhausted) {
        m_chunks.withUnique([&](ChunkStore& store) {
            for (const uint64_t packed : m_boundaryLightingChunks) {
                const int cx = static_cast<int>(static_cast<int32_t>(packed >> 32));
                const int cz = static_cast<int>(static_cast<int32_t>(packed & 0xffffffffu));
                Chunk* chunk = store.findUnlocked(cx, cz);
                if (chunk != nullptr && chunk->generated.load() &&
                    chunk->lightingInitialized.load() &&
                    chunk->lifecycle.load() != Chunk::LifecycleState::Renderable)
                    chunk->lifecycle = Chunk::LifecycleState::WaitingForMesh;
            }
        });
        m_boundaryLightingChunks.clear();
    }
}

StreamingProgress ChunkStreamer::generationProgress() const {
    StreamingProgress progress;
    m_chunks.withShared([&](ChunkStore& store) {
        progress.total = m_visibleChunkCount;
        for (size_t i = 0; i < m_visibleChunkCount &&
             i < m_desiredChunks.size(); ++i) {
            const auto& key = m_desiredChunks[i];
            const Chunk* chunk = store.findUnlocked(key.first, key.second);
            if (chunk != nullptr && chunk->generated.load()) {
                ++progress.completed;
                if (chunk->cacheHit.load()) ++progress.cacheHits;
                if (!chunk->cacheHit.load() && chunk->cacheChecked.load())
                    ++progress.cacheMisses;
                if (chunk->lightingInitialized.load()) ++progress.lightingReady;
                if (chunk->meshReady.load()) ++progress.meshReady;
                if (chunk->lifecycle.load() == Chunk::LifecycleState::Renderable)
                    ++progress.renderable;
            }
        }
    });
    return progress;
}

StreamingProgress ChunkStreamer::loadingProgress() const {
    StreamingProgress progress;
    m_chunks.withShared([&](ChunkStore& store) {
        progress.total = m_visibleChunkCount;
        for (size_t i = 0; i < m_visibleChunkCount &&
             i < m_desiredChunks.size(); ++i) {
            const auto& key = m_desiredChunks[i];
            const Chunk* chunk = store.findUnlocked(key.first, key.second);
            if (chunk == nullptr) continue;
            if (chunk->generated.load()) {
                if (chunk->cacheHit.load()) ++progress.cacheHits;
                if (!chunk->cacheHit.load() && chunk->cacheChecked.load())
                    ++progress.cacheMisses;
                if (chunk->lightingInitialized.load()) ++progress.lightingReady;
                if (chunk->meshReady.load()) ++progress.meshReady;
                if (chunk->lifecycle.load() == Chunk::LifecycleState::Renderable)
                    ++progress.renderable;
            }
            if (chunk->generated.load() && !chunk->isDirty() &&
                !chunk->meshInProgress.load() && !chunk->meshReady.load()) {
                ++progress.completed;
            }
        }
    });
    return progress;
}

void ChunkStreamer::persistGeneratedChunks() {
    if (!m_saveStore) return;
    m_chunks.withUnique([this](ChunkStore& store) {
        store.forEachUniqueUnlocked([this](Chunk* chunk) {
            if (!chunk->generated.load() || chunk->cacheHit.load()) return;
            if (!chunk->hasBaseSnapshot()) chunk->captureBaseSnapshot();
            if (!chunk->baseCacheInProgress.load()) {
                chunk->baseCacheDirty = false;
                queueBaseCacheWriteUnlocked(chunk);
            }
        });
    });
    drainCacheWrites();
}

std::optional<std::vector<WorldMetadata::PersistedEntity>>
ChunkStreamer::takePrefetchedChunkEntities(int cx, int cz) {
    const auto it = m_prefetchedEntities.find({cx, cz});
    if (it == m_prefetchedEntities.end()) return std::nullopt;
    auto result = std::move(it->second);
    m_prefetchedEntities.erase(it);
    return result;
}

void ChunkStreamer::waitForInitialGeneration(int maxWaitMs) {
    RuntimeClock clock; const auto start = clock.now();

    while (true) {
        // Check if all chunks are generated
        bool allGenerated = true;
        m_chunks.withShared([&](ChunkStore& store) {
            store.forEachSharedUnlocked([&](const Chunk* chunk) {
                if (!chunk->generated.load() && !chunk->generationInProgress.load()) {
                    // Not generated and not being worked on — shouldn't happen
                    // if enqueue was called, but handle gracefully
                    allGenerated = false;
                    return;
                }
                if (chunk->generationInProgress.load()) {
                    allGenerated = false;
                    return;
                }
            });
        });

        if (allGenerated) break;

        const auto elapsed = RuntimeClock::milliseconds(
            RuntimeClock::elapsed(start, clock.now()));
        if (elapsed >= static_cast<uint64_t>(std::max(0, maxWaitMs))) break;

        // A small thread pool may deliberately keep one worker available for
        // the main thread. Feed the bounded generation window while waiting
        // so a target larger than that window is not left unscheduled.
        enqueueGeneration();

        // Yield to let worker threads run
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
}

bool ChunkStreamer::applyPendingBlocksUnlocked(int cx, int cz,
                                               ChunkStore& store) {
    auto it = m_pendingBlocks.find({cx, cz});
    if (it == m_pendingBlocks.end()) return false;

    Chunk* chunk = store.findUnlocked(cx, cz);
    if (chunk == nullptr || !chunk->generated.load()) return false;
    bool changed = false;
    for (auto& pb : it->second) {
        int lx = pb.worldX - chunk->worldX();
        int lz = pb.worldZ - chunk->worldZ();
        if (lx >= 0 && lx < 16 && lz >= 0 && lz < 16 &&
            Config::isValidWorldY(pb.worldY)) {
            BlockId cur = chunk->getBlock(lx, pb.worldY, lz);
            bool leaf = cur == BlockId::LEAVES || cur == BlockId::BIRCH_LEAVES ||
                        cur == BlockId::SPRUCE_LEAVES || cur == BlockId::JUNGLE_LEAVES ||
                        cur == BlockId::ACACIA_LEAVES;
            if (cur == BlockId::AIR || leaf || cur == BlockId::SNOW) {
                chunk->setBlock(lx, pb.worldY, lz, pb.id);
                changed = true;
                if (chunk->hasBaseSnapshot()) {
                    chunk->setBaseBlock(lx, pb.worldY, lz, pb.id);
                    chunk->baseCacheDirty = true;
                }
            }
        }
    }
    m_pendingBlocks.erase(it);
    return changed;
}
