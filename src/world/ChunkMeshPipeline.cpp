#include "world/ChunkMeshPipeline.h"

#include "renderer/GameRenderer.h"
#include "threading/ThreadPool.h"
#include "world/Chunk.h"
#include "world/ChunkMesh.h"
#include "world/ChunkStore.h"
#include "world/World.h"

#include <algorithm>
#include <array>
#include <unordered_set>

namespace {
struct MeshSnapshot {
    static constexpr int WIDTH = Config::CHUNK_SIZE_X + 2;
    static constexpr int DEPTH = Config::CHUNK_SIZE_Z + 2;
    int baseX = 0;
    int baseZ = 0;
    std::vector<uint8_t> blocks;
    std::vector<uint8_t> light;
    std::vector<uint8_t> targetBlocks;
    int columns[Config::CHUNK_SIZE_X][Config::CHUNK_SIZE_Z]{};

    int ringIndex(int x, int y, int z) const {
        return (x + 1) + (z + 1) * WIDTH +
            Config::worldYToStorageY(y) * WIDTH * DEPTH;
    }

    BlockId block(int wx, int y, int wz) const {
        const int x = wx - baseX;
        const int z = wz - baseZ;
        if (x < -1 || x > Config::CHUNK_SIZE_X ||
            z < -1 || z > Config::CHUNK_SIZE_Z ||
            !Config::isValidWorldY(y)) return BlockId::AIR;
        return static_cast<BlockId>(blocks[ringIndex(x, y, z)]);
    }

    LightSample sampleLight(int wx, int y, int wz) const {
        const int x = wx - baseX;
        const int z = wz - baseZ;
        if (x < -1 || x > Config::CHUNK_SIZE_X ||
            z < -1 || z > Config::CHUNK_SIZE_Z ||
            !Config::isValidWorldY(y)) return {};
        return unpackLight(light[ringIndex(x, y, z)]);
    }

    const int (*columnData() const)[Config::CHUNK_SIZE_Z] {
        return columns;
    }
};

bool makeMeshSnapshot(ChunkStore& store, Chunk* target, MeshSnapshot& snapshot) {
    if (target == nullptr || !target->generated.load()) return false;
    snapshot.baseX = target->worldX();
    snapshot.baseZ = target->worldZ();
    snapshot.blocks.assign(static_cast<size_t>(MeshSnapshot::WIDTH) *
                               MeshSnapshot::DEPTH * Config::CHUNK_VOLUME / 256,
                           0);
    snapshot.light.assign(snapshot.blocks.size(), 0);
    snapshot.targetBlocks.clear();
    // The target arrays are copied separately so the 16×16×Y buffer passed to
    // ChunkMesh retains its original layout.  The ring starts as AIR/zero and
    // is filled from the target plus its eight possible neighbours.
    std::vector<uint8_t> targetLight;
    target->copyRawState(snapshot.targetBlocks, targetLight);
    target->copyColumnMaxY(snapshot.columns);
    for (int x = -1; x <= 1; ++x) {
        for (int z = -1; z <= 1; ++z) {
            Chunk* source = store.findUnlocked(target->cx + x, target->cz + z);
            if (source == nullptr || !source->generated.load()) continue;
            std::vector<uint8_t> sourceBlocks, sourceLight;
            source->copyRawState(sourceBlocks, sourceLight);
            const int minX = x == -1 ? Config::CHUNK_SIZE_X - 1 : 0;
            const int maxX = x == 1 ? 0 : Config::CHUNK_SIZE_X - 1;
            const int minZ = z == -1 ? Config::CHUNK_SIZE_Z - 1 : 0;
            const int maxZ = z == 1 ? 0 : Config::CHUNK_SIZE_Z - 1;
            for (int y = Config::WORLD_MIN_Y; y < Config::WORLD_MAX_Y; ++y) {
                for (int lx = minX; lx <= maxX; ++lx) {
                    for (int lz = minZ; lz <= maxZ; ++lz) {
                        const int wx = source->worldX() + lx - snapshot.baseX;
                        const int wz = source->worldZ() + lz - snapshot.baseZ;
                        if (wx < -1 || wx > Config::CHUNK_SIZE_X ||
                            wz < -1 || wz > Config::CHUNK_SIZE_Z) continue;
                        const int sourceIndex = lx + lz * Config::CHUNK_SIZE_X +
                            Config::worldYToStorageY(y) * Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z;
                        const int ring = snapshot.ringIndex(wx, y, wz);
                        snapshot.blocks[ring] = sourceBlocks[sourceIndex];
                        snapshot.light[ring] = sourceLight[sourceIndex];
                    }
                }
            }
        }
    }
    // The target was copied into the ring through the x/z=0 case above.  A
    // target copy is also retained for the builder's compact local indexing.
    return snapshot.targetBlocks.size() == static_cast<size_t>(Config::CHUNK_VOLUME);
}
}

void ChunkMeshPipeline::enqueueMeshBuilds(int maxInFlight) {
    if (!m_threadPool) return;

    std::vector<Chunk*> candidates;
    int availableSlots = 0;
    m_chunks.withShared([&](ChunkStore& store) {
        int inFlight = 0;
        store.forEachSharedUnlocked([&](const Chunk* chunk) {
            if (chunk->meshInProgress.load()) ++inFlight;
        });
        const int workerCount = static_cast<int>(m_threadPool->threadCount());
        const int boundedWorkers = std::max(1,
            workerCount > 1 ? workerCount - 1 : 1);
        availableSlots = std::min(std::max(1, maxInFlight), boundedWorkers) -
            inFlight;
        if (availableSlots <= 0) return;

        candidates.reserve(store.activeChunks().size() + 16);
        std::unordered_set<Chunk*> active;
        active.reserve(store.activeChunks().size());
        for (Chunk* chunk : store.activeChunks()) {
            active.insert(chunk);
            if (chunk->isDirty() && !chunk->meshInProgress.load() &&
                chunk->generated.load() &&
                chunk->lifecycle.load() != Chunk::LifecycleState::Warm)
                candidates.push_back(chunk);
        }
        // Prefetch chunks are intentionally not rendered yet, but may be
        // close enough to finish a mesh before they enter the active set.
        store.forEachSharedUnlocked([&](Chunk* chunk) {
            if (active.count(chunk) != 0 || !chunk->isDirty() ||
                chunk->meshInProgress.load() || !chunk->generated.load() ||
                chunk->lifecycle.load() == Chunk::LifecycleState::Warm) return;
            candidates.push_back(chunk);
        });
    });
    if (availableSlots <= 0) return;

    const int centerChunkX = m_world.centerChunkX();
    const int centerChunkZ = m_world.centerChunkZ();
    (void)centerChunkX;
    (void)centerChunkZ;

    const int enqueueCount = std::min(
        {m_world.meshChunksPerFrame(), availableSlots,
         static_cast<int>(candidates.size())});
    for (int i = 0; i < enqueueCount; ++i) {
        Chunk* chunkPtr = candidates[static_cast<size_t>(i)];

        chunkPtr->meshInProgress = true;
        chunkPtr->lifecycle = Chunk::LifecycleState::WaitingForMesh;
        chunkPtr->markClean();  // Mark clean NOW so we don't re-enqueue
        const uint64_t revision = chunkPtr->dataRevision();

        // Capture a raw pointer — the chunk is owned by the ChunkStore and
        // won't be destroyed while meshInProgress is true
        const int dx = chunkPtr->cx - centerChunkX;
        const int dz = chunkPtr->cz - centerChunkZ;
        const int distance2 = dx * dx + dz * dz;

        m_threadPool->enqueuePriority([this, chunkPtr, revision]() {
            MeshSnapshot snapshot;
            bool valid = false;
            m_chunks.withShared([&](ChunkStore& store) {
                valid = makeMeshSnapshot(store, chunkPtr, snapshot);
            });
            if (!valid) {
                chunkPtr->meshReady = false;
                chunkPtr->meshInProgress = false;
                chunkPtr->markDirty();
                return;
            }
            auto neighborFunc = [&snapshot](int wx, int wy, int wz) -> BlockId {
                return snapshot.block(wx, wy, wz);
            };
            auto lightFunc = [&snapshot](int wx, int wy, int wz) -> LightSample {
                return snapshot.sampleLight(wx, wy, wz);
            };
            chunkPtr->m_pendingMesh.build(
                chunkPtr->worldX(), chunkPtr->worldZ(),
                snapshot.targetBlocks.data(), snapshot.columnData(),
                neighborFunc, lightFunc);
            chunkPtr->pendingMeshRevision = revision;

            // Signal completion
            chunkPtr->meshReady = true;
        }, 500000 - distance2);
    }
}

void ChunkMeshPipeline::processCompletedMeshes(IGameRenderer* renderer,
                                               int maxUploads,
                                               size_t maxUploadBytes) {
    if (!renderer) return;
    m_renderer = renderer;

    std::vector<Chunk*> ready;
    m_chunks.withShared([&](ChunkStore& store) {
        std::unordered_set<Chunk*> active;
        active.reserve(store.activeChunks().size());
        for (Chunk* chunk : store.activeChunks()) {
            active.insert(chunk);
            if (chunk->meshReady.load()) ready.push_back(chunk);
        }
        store.forEachSharedUnlocked([&](Chunk* chunk) {
            if (active.count(chunk) == 0 && chunk->meshReady.load())
                ready.push_back(chunk);
        });
    });
    const int centerChunkX = m_world.centerChunkX();
    const int centerChunkZ = m_world.centerChunkZ();
    (void)centerChunkX;
    (void)centerChunkZ;
    const int uploadCount = std::min(maxUploads, static_cast<int>(ready.size()));
    size_t uploadedBytes = 0;
    for (int i = 0; i < uploadCount; ++i) {
        Chunk* chunk = ready[static_cast<size_t>(i)];

        if (chunk->pendingMeshRevision.load() != chunk->dataRevision()) {
            chunk->meshReady = false;
            chunk->meshInProgress = false;
            chunk->markDirty();
            continue;
        }

        const size_t bytes = chunk->m_pendingMesh.uploadBytes();
        if (uploadedBytes > 0 && uploadedBytes + bytes > maxUploadBytes) break;

        // Swap pending mesh into active
        {
            std::lock_guard meshLock(chunk->getMeshMutex());
            chunk->getMesh().adoptCpuGeometry(chunk->m_pendingMesh);
        }

        // Upload on main thread (GL context)
        renderer->uploadChunkMesh(chunk->getMesh());
        uploadedBytes += bytes;

        chunk->meshReady = false;
        chunk->meshInProgress = false;
        chunk->lifecycle = Chunk::LifecycleState::Renderable;
    }
}

void ChunkMeshPipeline::buildMeshesSync(IGameRenderer* renderer, int maxCount) {
    if (!renderer) return;
    m_renderer = renderer;

    int built = 0;
    m_chunks.withShared([&](ChunkStore& store) {
        store.forEachSharedUnlocked([&](Chunk* chunk) {
            if (built >= maxCount) return;
            if (!chunk->isDirty()) return;

            auto neighborFunc = [this](int wx, int wy, int wz) -> BlockId {
                return m_world.getBlock(wx, wy, wz);
            };
            auto lightFunc = [this](int wx, int wy, int wz) -> LightSample {
                return m_world.getLight(wx, wy, wz);
            };

            {
                std::lock_guard meshLock(chunk->getMeshMutex());
                ChunkMesh& mesh = chunk->getMesh();
                mesh.build(
                    chunk->worldX(), chunk->worldZ(),
                    chunk->rawBlocks(),
                    chunk->getColumnMaxYData(),
                    neighborFunc, lightFunc
                );
                renderer->uploadChunkMesh(mesh);
            }

            chunk->markClean();
            ++built;
        });
    });
}

void ChunkMeshPipeline::invalidateGpuMeshes() {
    m_chunks.withShared([&](ChunkStore& store) {
        store.forEachSharedUnlocked([&](Chunk* chunk) {
            std::lock_guard meshLock(chunk->getMeshMutex());
            if (m_renderer)
                m_renderer->releaseChunkMesh(chunk->getMesh());
            else
                chunk->getMesh().abandonGpuResources();
        });
    });
}

void ChunkMeshPipeline::restoreGpuMeshes() {
    m_chunks.withShared([&](ChunkStore&) {
        for (Chunk* chunk : m_chunks.activeChunks()) {
            std::lock_guard meshLock(chunk->getMeshMutex());
            if (!chunk->getMesh().empty() && m_renderer)
                m_renderer->uploadChunkMesh(chunk->getMesh());
        }
    });
}

void ChunkMeshPipeline::releaseChunkMesh(Chunk* chunk) {
    std::lock_guard meshLock(chunk->getMeshMutex());
    if (m_renderer) m_renderer->releaseChunkMesh(chunk->getMesh());
}

void ChunkMeshPipeline::releaseAllMeshes() {
    m_chunks.withUnique([&](ChunkStore& store) {
        store.forEachUniqueUnlocked([this](Chunk* chunk) {
            std::lock_guard meshLock(chunk->getMeshMutex());
            if (m_renderer) m_renderer->releaseChunkMesh(chunk->getMesh());
        });
    });
}
