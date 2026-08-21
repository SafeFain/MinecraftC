#include "world/ChunkStreamer.h"

#include "core/RuntimeClock.h"
#include "game/SaveStore.h"
#include "threading/ThreadPool.h"
#include "world/Chunk.h"
#include "world/ChunkMesh.h"
#include "world/ChunkStore.h"
#include "world/FluidScheduler.h"
#include "world/World.h"
#include "world/WorldGenContext.h"
#include "world/WorldGenerator.h"
#include "world/WorldPersistence.h"

#include <algorithm>
#include <chrono>
#include <thread>

void ChunkStreamer::clear() {
    m_firstUpdate = true;
    m_chunksPerFrame = 16;
    m_streamCenterChunkX = std::numeric_limits<int>::max();
    m_streamCenterChunkZ = std::numeric_limits<int>::max();
    m_streamRenderDistance = -1;
    m_desiredChunks.clear();
    m_desiredChunkSet.clear();
    m_streamCursor = 0;
    m_streamCleanupPending = false;
    m_pendingBlocks.clear();
    ++m_streamingRevision;
}

void ChunkStreamer::update(const glm::dvec3& playerPos, int loadBudgetOverride) {
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
        const int r2 = Config::RENDER_DISTANCE * Config::RENDER_DISTANCE;
        for (int dx = -Config::RENDER_DISTANCE; dx <= Config::RENDER_DISTANCE; ++dx) {
            for (int dz = -Config::RENDER_DISTANCE; dz <= Config::RENDER_DISTANCE; ++dz) {
                if (dx * dx + dz * dz > r2) continue;
                const int cx = pcx + dx;
                const int cz = pcz + dz;
                m_desiredChunks.emplace_back(cx, cz);
                m_desiredChunkSet.insert(packedChunkKey(cx, cz));
            }
        }
        std::sort(m_desiredChunks.begin(), m_desiredChunks.end(),
            [pcx, pcz](const auto& a, const auto& b) {
                const int64_t adx = static_cast<int64_t>(a.first) - pcx;
                const int64_t adz = static_cast<int64_t>(a.second) - pcz;
                const int64_t bdx = static_cast<int64_t>(b.first) - pcx;
                const int64_t bdz = static_cast<int64_t>(b.second) - pcz;
                return adx * adx + adz * adz < bdx * bdx + bdz * bdz;
            });
        m_streamCursor = 0;
        m_streamCleanupPending = true;
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
                if (chunk->meshInProgress.load() ||
                    chunk->generationInProgress.load())
                    cleanupRemaining = true;
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
        }
    }

    if (activeChanged) {
        m_chunks.rebuildActiveChunks(pcx, pcz);
        ++m_streamingRevision;
    }
}

void ChunkStreamer::enqueueGeneration() {
    if (!m_threadPool) return;

    const int taskSlots = Config::CHUNK_GEN_TASKS_IN_FLIGHT -
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
            if (!chunk->generated.load() && !chunk->generationInProgress.load()) {
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
            }, 1000000 - distance2);
        }
    });
}

void ChunkStreamer::processCompletedGenerations(bool rebuildLightingNow) {
    // Apply pending tree leaves for chunks that have finished generating
    std::vector<glm::ivec3> fluidSeeds;
    bool generationStateChanged = false;
    m_chunks.withUnique([&](ChunkStore& store) {
        // Only persisted edits are fluid wake-up sources at chunk load time.
        // Generated rivers and lakes are already stable terrain; scanning all
        // 384 Y levels of every seam both stalls the main thread and can turn
        // a harmless terrain boundary into an unbounded waterfall.
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
                    // AIR edits are retained because they can expose a
                    // natural fluid across the newly available seam.
                    if (!isFluid(block) && block != BlockId::AIR) return;
                    fluidSeeds.push_back({
                        cx * Config::CHUNK_SIZE_X + x, y,
                        cz * Config::CHUNK_SIZE_Z + z});
                });
        };
        store.forEachUniqueUnlocked([&](Chunk* chunk) {
            const std::pair<int,int> key{chunk->cx, chunk->cz};
            if (!chunk->generated.load()) return;
            const bool firstApply =
                !m_world.m_persistence.isOverridesApplied(key.first, key.second);
            applyPendingBlocksUnlocked(key.first, key.second, store);
            m_world.m_persistence.applySavedOverridesUnlocked(
                key.first, key.second);
            m_world.m_persistence.loadBlockEntities(key.first, key.second);
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
            }
        });
    });
    if (generationStateChanged) ++m_streamingRevision;
    for (const glm::ivec3& position : fluidSeeds)
        m_world.m_fluids.scheduleAround(position);
    if (rebuildLightingNow && m_world.lightDirty()) m_world.rebuildLightingNow();
}

StreamingProgress ChunkStreamer::generationProgress() const {
    StreamingProgress progress;
    m_chunks.withShared([&](ChunkStore& store) {
        progress.total = m_desiredChunks.size();
        for (const auto& key : m_desiredChunks) {
            const Chunk* chunk = store.findUnlocked(key.first, key.second);
            if (chunk != nullptr && chunk->generated.load())
                ++progress.completed;
        }
    });
    return progress;
}

StreamingProgress ChunkStreamer::loadingProgress() const {
    StreamingProgress progress;
    m_chunks.withShared([&](ChunkStore& store) {
        progress.total = m_desiredChunks.size();
        for (const auto& key : m_desiredChunks) {
            const Chunk* chunk = store.findUnlocked(key.first, key.second);
            if (chunk == nullptr) continue;
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
    m_chunks.withShared([this](ChunkStore& store) {
        store.forEachSharedUnlocked([this](const Chunk* chunk) {
            if (!chunk->generated.load()) return;
            std::vector<uint8_t> blocks(
                chunk->rawBlocks(), chunk->rawBlocks() + Config::CHUNK_VOLUME);
            m_saveStore->saveGeneratedChunk(
                chunk->cx, chunk->cz, blocks,
                WorldGenContext::CHUNK_CACHE_VERSION);
        });
    });
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

        // Yield to let worker threads run
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
}

void ChunkStreamer::applyPendingBlocksUnlocked(int cx, int cz,
                                               ChunkStore& store) {
    auto it = m_pendingBlocks.find({cx, cz});
    if (it == m_pendingBlocks.end()) return;

    Chunk* chunk = store.findUnlocked(cx, cz);
    if (chunk == nullptr || !chunk->generated.load()) return;
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
            }
        }
    }
    m_pendingBlocks.erase(it);
}
