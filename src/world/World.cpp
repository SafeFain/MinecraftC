#include "world/World.h"
#include "world/ChunkMesh.h"
#include "world/RegionGenerator.h"
#include "renderer/Renderer.h"
#include "threading/ThreadPool.h"
#include "debug/Log.h"
#include "Config.h"

#include <cmath>
#include <algorithm>
#include <thread>
#include <chrono>
#include <unordered_set>

World::World() : m_generator(Config::WORLD_SEED) {}

void World::resetForNewSeed(uint64_t newSeed) {
    std::unique_lock lock(m_chunkMutex);
    for (auto& [key, chunk] : m_chunks) {
        chunk->getMesh().destroy();
    }
    m_chunks.clear();
    m_activeChunks.clear();
    m_pendingBlocks.clear();
    m_firstUpdate = true;
    m_chunksPerFrame = 16;
    // Placement-new: WorldGenerator contains reference members (Noise&),
    // so move assignment is deleted. Reconstruct in-place.
    m_generator.~WorldGenerator();
    new (&m_generator) WorldGenerator(newSeed);
    Config::WORLD_SEED = newSeed;
}

World::~World() {
    std::unique_lock lock(m_chunkMutex);
    for (auto& [key, chunk] : m_chunks) {
        chunk->getMesh().destroy();
    }
}

// ── Block queries ─────────────────────────────────────────────────────

BlockId World::getBlock(int worldX, int worldY, int worldZ) const {
    if (worldY < 0 || worldY >= Config::CHUNK_SIZE_Y) {
        return BlockId::AIR;
    }

    int cx = worldToChunkX(static_cast<float>(worldX));
    int cz = worldToChunkZ(static_cast<float>(worldZ));

    int lx = worldX - cx * Config::CHUNK_SIZE_X;
    int lz = worldZ - cz * Config::CHUNK_SIZE_Z;
    if (lx < 0) { cx -= 1; lx += Config::CHUNK_SIZE_X; }
    if (lz < 0) { cz -= 1; lz += Config::CHUNK_SIZE_Z; }

    std::shared_lock lock(m_chunkMutex);
    auto it = m_chunks.find({cx, cz});
    if (it != m_chunks.end()) {
        return it->second->getBlock(lx, worldY, lz);
    }

    return BlockId::AIR;
}

void World::setBlock(int worldX, int worldY, int worldZ, BlockId id) {
    if (worldY < 0 || worldY >= Config::CHUNK_SIZE_Y) return;

    int cx = worldToChunkX(static_cast<float>(worldX));
    int cz = worldToChunkZ(static_cast<float>(worldZ));

    int lx = worldX - cx * Config::CHUNK_SIZE_X;
    int lz = worldZ - cz * Config::CHUNK_SIZE_Z;
    if (lx < 0) { cx -= 1; lx += Config::CHUNK_SIZE_X; }
    if (lz < 0) { cz -= 1; lz += Config::CHUNK_SIZE_Z; }

    Chunk* chunk = getChunk(cx, cz);
    chunk->setBlock(lx, worldY, lz, id);

    if (lx == 0)                   markDirty(cx - 1, cz);
    if (lx == Config::CHUNK_SIZE_X - 1) markDirty(cx + 1, cz);
    if (lz == 0)                   markDirty(cx, cz - 1);
    if (lz == Config::CHUNK_SIZE_Z - 1) markDirty(cx, cz + 1);
}

void World::markDirty(int cx, int cz) {
    std::shared_lock lock(m_chunkMutex);
    auto it = m_chunks.find({cx, cz});
    if (it != m_chunks.end()) {
        it->second->markDirty();
    }
}

// ── Chunk access ──────────────────────────────────────────────────────

Chunk* World::getChunk(int cx, int cz) {
    std::pair<int,int> key{cx, cz};

    {
        std::shared_lock lock(m_chunkMutex);
        auto it = m_chunks.find(key);
        if (it != m_chunks.end()) {
            return it->second.get();
        }
    }

    std::unique_lock lock(m_chunkMutex);
    auto it = m_chunks.find(key);
    if (it != m_chunks.end()) {
        return it->second.get();
    }

    auto chunk = std::make_unique<Chunk>(cx, cz);
    // Generation is deferred to enqueueGeneration() — the chunk
    // starts as all-AIR and will be populated by a worker thread.
    Chunk* ptr = chunk.get();
    m_chunks[key] = std::move(chunk);
    return ptr;
}

// ── Update (chunk loading/unloading) ──────────────────────────────────

void World::update(const glm::vec3& playerPos) {
    int pcx = worldToChunkX(playerPos.x);
    int pcz = worldToChunkZ(playerPos.z);

    // First frame: load more chunks
    if (m_firstUpdate) {
        m_chunksPerFrame = 16;
        m_firstUpdate = false;
    } else {
        m_chunksPerFrame = 4;
    }

    // Compute needed chunks
    std::vector<std::pair<int,int>> needed;
    int r2 = Config::RENDER_DISTANCE * Config::RENDER_DISTANCE;
    for (int dx = -Config::RENDER_DISTANCE; dx <= Config::RENDER_DISTANCE; ++dx) {
        for (int dz = -Config::RENDER_DISTANCE; dz <= Config::RENDER_DISTANCE; ++dz) {
            if (dx * dx + dz * dz <= r2) {
                needed.emplace_back(pcx + dx, pcz + dz);
            }
        }
    }

    // Remove out-of-range chunks — O(1) lookup via hash set
    {
        std::unique_lock lock(m_chunkMutex);

        // Build lookup set from needed coords for O(1) membership test
        std::unordered_set<int64_t> neededSet;
        neededSet.reserve(needed.size());
        for (auto& [cx, cz] : needed) {
            neededSet.insert((static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cz));
        }

        std::vector<std::pair<int,int>> toRemove;
        for (auto& [key, chunk] : m_chunks) {
            int64_t k64 = (static_cast<int64_t>(key.first) << 32) | static_cast<uint32_t>(key.second);
            if (neededSet.find(k64) == neededSet.end() && !chunk->meshInProgress.load()
                && !chunk->generationInProgress.load()) {
                toRemove.push_back(key);
            }
        }
        for (auto& key : toRemove) {
            auto it = m_chunks.find(key);
            if (it != m_chunks.end()) {
                it->second->getMesh().destroy();
                m_chunks.erase(it);
            }
        }
    }

    // Load/generate new chunks
    for (auto& key : needed) {
        bool exists = false;
        {
            std::shared_lock lock(m_chunkMutex);
            exists = m_chunks.find(key) != m_chunks.end();
        }
        if (!exists) {
            getChunk(key.first, key.second);
        }
    }

    // Rebuild active list
    {
        std::shared_lock lock(m_chunkMutex);
        m_activeChunks.clear();
        m_activeChunks.reserve(m_chunks.size());
        for (auto& [key, chunk] : m_chunks) {
            m_activeChunks.push_back(chunk.get());
        }
    }
}

// ── Async chunk generation (region-based) ─────────────────────────────

void World::enqueueGeneration() {
    if (!m_threadPool) return;

    std::shared_lock lock(m_chunkMutex);

    // Collect all ungenerated, not-in-progress chunk coords
    std::vector<std::pair<int,int>> ungenerated;
    for (auto& [key, chunk] : m_chunks) {
        if (!chunk->generated.load() && !chunk->generationInProgress.load()) {
            ungenerated.push_back(key);
        }
    }
    if (ungenerated.empty()) return;

    // Build a set for fast lookup
    std::unordered_set<int64_t> available;
    for (auto& [cx, cz] : ungenerated) {
        available.insert((static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cz));
    }
    std::unordered_set<int64_t> visited;

    const int R = Config::REGION_SIZE_CHUNKS;  // 3
    const int PADDING = Config::REGION_PADDING;

    // Greedy: try to form R×R regions from ungenerated chunks
    struct RegionTask {
        int originCX, originCZ;
        std::vector<Chunk*> chunks;  // row-major: [lcz * R + lcx]
    };
    std::vector<RegionTask> regions;

    for (auto& [cx, cz] : ungenerated) {
        int64_t key = (static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cz);
        if (visited.count(key)) continue;

        // Check if a full R×R region is available starting at (cx, cz)
        RegionTask region;
        region.originCX = cx;
        region.originCZ = cz;
        bool complete = true;

        for (int dcz = 0; dcz < R && complete; ++dcz) {
            for (int dcx = 0; dcx < R && complete; ++dcx) {
                int64_t nkey = (static_cast<int64_t>(cx + dcx) << 32) | static_cast<uint32_t>(cz + dcz);
                if (!available.count(nkey) || visited.count(nkey)) {
                    complete = false;
                    break;
                }
                // Get the chunk pointer
                auto it = m_chunks.find({cx + dcx, cz + dcz});
                if (it == m_chunks.end()) { complete = false; break; }
                region.chunks.push_back(it->second.get());
            }
        }

        if (complete) {
            // Mark all chunks in region as visited and in-progress
            for (int dcz = 0; dcz < R; ++dcz) {
                for (int dcx = 0; dcx < R; ++dcx) {
                    int64_t nkey = (static_cast<int64_t>(cx + dcx) << 32) | static_cast<uint32_t>(cz + dcz);
                    visited.insert(nkey);
                    auto it = m_chunks.find({cx + dcx, cz + dcz});
                    it->second->generationInProgress = true;
                }
            }
            regions.push_back(std::move(region));
        }
    }

    // Enqueue region tasks
    for (auto& reg : regions) {
        World* worldPtr = this;
        WorldGenerator* genPtr = &m_generator;

        m_threadPool->enqueuePriority([worldPtr, genPtr, reg = std::move(reg), R, PADDING]() {
            // Build RegionGenerator from WorldGenerator's sub-generators
            RegionGenerator regionGen(
                genPtr->getHeightPipeline(),
                genPtr->getCaveGenerator(),
                genPtr->getTreeGenerator(),
                genPtr->getOreGenerator(),
                genPtr->getSeed()
            );

            // Clone chunk pointers (non-const because we need to mutate)
            auto chunks = reg.chunks;

            std::vector<RegionGenerationData::PendingBlock> pendingOut;
            regionGen.generateRegion(reg.originCX, reg.originCZ,
                                     R, PADDING,
                                     chunks, pendingOut);

            // Store pending blocks under the chunk mutex
            {
                std::unique_lock nLock(worldPtr->m_chunkMutex);
                for (auto& pb : pendingOut) {
                    int tcx = World::worldToChunkX(static_cast<float>(pb.worldX));
                    int tcz = World::worldToChunkZ(static_cast<float>(pb.worldZ));
                    worldPtr->m_pendingBlocks[{tcx, tcz}].push_back(pb);
                }
            }
        }, 1);
    }

    // Remaining ungenerated chunks (not part of any region) — legacy singleton path
    for (auto& [cx, cz] : ungenerated) {
        int64_t key = (static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cz);
        if (visited.count(key)) continue;

        auto it = m_chunks.find({cx, cz});
        if (it == m_chunks.end()) continue;
        Chunk* chunkPtr = it->second.get();
        if (chunkPtr->generated.load() || chunkPtr->generationInProgress.load()) continue;

        chunkPtr->generationInProgress = true;
        visited.insert(key);

        WorldGenerator* genPtr = &m_generator;

        // Legacy neighborQuery for singleton chunks
        auto neighborQuery = [this, genPtr](int wx, int wz) -> std::optional<HeightBiome> {
            int ncx = World::worldToChunkX(static_cast<float>(wx));
            int ncz = World::worldToChunkZ(static_cast<float>(wz));
            {
                std::shared_lock nLock(m_chunkMutex);
                auto nit = m_chunks.find({ncx, ncz});
                if (nit == m_chunks.end() || !nit->second->generated.load()) {
                    return std::nullopt;
                }
            }
            return genPtr->queryHeightBiome(wx, wz);
        };

        auto blockSetter = [this](int wx, int wy, int wz, BlockId id) {
            if (wy < 0 || wy >= Config::CHUNK_SIZE_Y) return;
            int bsx = World::worldToChunkX(static_cast<float>(wx));
            int bsz = World::worldToChunkZ(static_cast<float>(wz));
            int lx = wx - bsx * Config::CHUNK_SIZE_X;
            int lz = wz - bsz * Config::CHUNK_SIZE_Z;
            if (lx < 0) { bsx -= 1; lx += Config::CHUNK_SIZE_X; }
            if (lz < 0) { bsz -= 1; lz += Config::CHUNK_SIZE_Z; }
            std::shared_lock nLock(m_chunkMutex);
            auto bit = m_chunks.find({bsx, bsz});
            if (bit != m_chunks.end() && bit->second->generated.load()) {
                bit->second->setBlock(lx, wy, lz, id);
            }
        };

        m_threadPool->enqueuePriority([chunkPtr, genPtr, neighborQuery, blockSetter]() {
            genPtr->generate(*chunkPtr, neighborQuery, blockSetter);
            chunkPtr->generated = true;
            chunkPtr->generationInProgress = false;
        }, 1);
    }
}

void World::processCompletedGenerations() {
    // Apply pending tree leaves for chunks that have finished generating
    std::shared_lock lock(m_chunkMutex);
    for (auto& [key, chunk] : m_chunks) {
        if (chunk->generated.load()) {
            applyPendingBlocks(key.first, key.second);
        }
    }
}

void World::applyPendingBlocks(int cx, int cz) {
    auto it = m_pendingBlocks.find({cx, cz});
    if (it == m_pendingBlocks.end()) return;

    Chunk* chunk = getChunk(cx, cz);
    for (auto& pb : it->second) {
        int lx = pb.worldX - chunk->worldX();
        int lz = pb.worldZ - chunk->worldZ();
        if (lx >= 0 && lx < 16 && lz >= 0 && lz < 16 &&
            pb.worldY >= 0 && pb.worldY < Config::CHUNK_SIZE_Y) {
            BlockId cur = chunk->getBlock(lx, pb.worldY, lz);
            if (cur == BlockId::AIR || cur == BlockId::LEAVES ||
                cur == BlockId::SNOW) {
                chunk->setBlock(lx, pb.worldY, lz, pb.id);
            }
        }
    }
    m_pendingBlocks.erase(it);
}

void World::waitForInitialGeneration(int maxWaitMs) {
    using namespace std::chrono;
    auto start = high_resolution_clock::now();

    while (true) {
        // Check if all chunks are generated
        bool allGenerated = true;
        {
            std::shared_lock lock(m_chunkMutex);
            for (auto& [key, chunk] : m_chunks) {
                if (!chunk->generated.load() && !chunk->generationInProgress.load()) {
                    // Not generated and not being worked on — shouldn't happen
                    // if enqueue was called, but handle gracefully
                    allGenerated = false;
                    break;
                }
                if (chunk->generationInProgress.load()) {
                    allGenerated = false;
                    break;
                }
            }
        }

        if (allGenerated) break;

        auto elapsed = duration_cast<milliseconds>(high_resolution_clock::now() - start).count();
        if (elapsed >= maxWaitMs) break;

        // Yield to let worker threads run
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
}

// ── Async mesh building ───────────────────────────────────────────────

void World::enqueueMeshBuilds() {
    if (!m_threadPool) return;

    int enqueued = 0;
    std::shared_lock lock(m_chunkMutex);

    for (auto& [key, chunk] : m_chunks) {
        if (enqueued >= m_chunksPerFrame) break;
        if (!chunk->isDirty()) continue;
        if (chunk->meshInProgress.load()) continue;
        if (!chunk->generated.load()) continue;  // not generated yet

        chunk->meshInProgress = true;
        chunk->markClean();  // Mark clean NOW so we don't re-enqueue

        // Capture a raw pointer — the chunk is owned by m_chunks and
        // won't be destroyed while meshInProgress is true
        Chunk* chunkPtr = chunk.get();
        World* worldPtr = this;

        m_threadPool->enqueuePriority([chunkPtr, worldPtr]() {
            // Build mesh into pending buffer
            auto neighborFunc = [worldPtr](int wx, int wy, int wz) -> BlockId {
                return worldPtr->getBlock(wx, wy, wz);
            };

            chunkPtr->m_pendingMesh.build(
                chunkPtr->worldX(), chunkPtr->worldZ(),
                &chunkPtr->blockAt(0, 0, 0),
                chunkPtr->getColumnMaxYData(),
                neighborFunc
            );

            // Signal completion
            chunkPtr->meshReady = true;
        }, 0);

        ++enqueued;
    }
}

void World::processCompletedMeshes(Renderer* renderer, int maxUploads) {
    if (!renderer) return;

    int uploaded = 0;
    std::shared_lock lock(m_chunkMutex);
    for (auto& [key, chunk] : m_chunks) {
        if (uploaded >= maxUploads) break;
        if (!chunk->meshReady.load()) continue;

        // Swap pending mesh into active
        {
            std::lock_guard meshLock(chunk->getMeshMutex());
            std::swap(chunk->getMesh().vertices, chunk->m_pendingMesh.vertices);
            std::swap(chunk->getMesh().indices, chunk->m_pendingMesh.indices);
            chunk->getMesh().indexCount = chunk->getMesh().indices.size();
        }

        // Upload on main thread (GL context)
        chunk->getMesh().upload();

        chunk->meshReady = false;
        chunk->meshInProgress = false;
        ++uploaded;
    }
}

// ── Synchronous mesh building ─────────────────────────────────────────

void World::buildMeshesSync(Renderer* renderer, int maxCount) {
    if (!renderer) return;

    int built = 0;
    std::shared_lock lock(m_chunkMutex);

    for (auto& [key, chunk] : m_chunks) {
        if (built >= maxCount) break;
        if (!chunk->isDirty()) continue;

        auto neighborFunc = [this](int wx, int wy, int wz) -> BlockId {
            return this->getBlock(wx, wy, wz);
        };

        {
            std::lock_guard meshLock(chunk->getMeshMutex());
            ChunkMesh& mesh = chunk->getMesh();
            mesh.build(
                chunk->worldX(), chunk->worldZ(),
                &chunk->blockAt(0, 0, 0),
                chunk->getColumnMaxYData(),
                neighborFunc
            );
            mesh.upload();
        }

        chunk->markClean();
        ++built;
    }
}

// ── Raycast ───────────────────────────────────────────────────────────

std::optional<World::RaycastHit> World::raycast(const glm::vec3& origin,
                                                 const glm::vec3& direction,
                                                 float maxDistance) const {
    glm::vec3 pos = origin;
    glm::ivec3 blockPos(
        static_cast<int>(std::floor(pos.x)),
        static_cast<int>(std::floor(pos.y)),
        static_cast<int>(std::floor(pos.z))
    );

    glm::ivec3 step(
        direction.x > 0.0f ? 1 : (direction.x < 0.0f ? -1 : 0),
        direction.y > 0.0f ? 1 : (direction.y < 0.0f ? -1 : 0),
        direction.z > 0.0f ? 1 : (direction.z < 0.0f ? -1 : 0)
    );

    glm::vec3 absDir = glm::abs(direction);
    glm::vec3 safeAbs = glm::max(absDir, glm::vec3(1e-10f));

    glm::vec3 tDelta(
        step.x != 0 ? 1.0f / safeAbs.x : INFINITY,
        step.y != 0 ? 1.0f / safeAbs.y : INFINITY,
        step.z != 0 ? 1.0f / safeAbs.z : INFINITY
    );

    glm::vec3 nextBoundary(
        step.x > 0 ? (blockPos.x + 1) - pos.x : pos.x - blockPos.x,
        step.y > 0 ? (blockPos.y + 1) - pos.y : pos.y - blockPos.y,
        step.z > 0 ? (blockPos.z + 1) - pos.z : pos.z - blockPos.z
    );
    glm::vec3 tMax = nextBoundary / safeAbs;

    glm::ivec3 lastNormal(0);

    int maxSteps = static_cast<int>(maxDistance * 3);
    for (int i = 0; i < maxSteps; ++i) {
        BlockId id = getBlock(blockPos.x, blockPos.y, blockPos.z);
        if (id != BlockId::AIR) {
            const BlockProperties& props = getBlockProps(id);
            if (props.solid) {
                return RaycastHit{blockPos, lastNormal};
            }
        }

        lastNormal = glm::ivec3(0);

        if (tMax.x <= tMax.y && tMax.x <= tMax.z) {
            lastNormal.x = -step.x;
            blockPos.x += step.x;
            tMax.x += tDelta.x;
        } else if (tMax.y <= tMax.x && tMax.y <= tMax.z) {
            lastNormal.y = -step.y;
            blockPos.y += step.y;
            tMax.y += tDelta.y;
        } else {
            lastNormal.z = -step.z;
            blockPos.z += step.z;
            tMax.z += tDelta.z;
        }

        if (blockPos.y < 0 || blockPos.y >= Config::CHUNK_SIZE_Y) {
            break;
        }
    }

    return std::nullopt;
}
