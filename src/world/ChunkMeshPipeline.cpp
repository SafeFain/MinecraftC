#include "world/ChunkMeshPipeline.h"

#include "renderer/GameRenderer.h"
#include "threading/ThreadPool.h"
#include "world/Chunk.h"
#include "world/ChunkMesh.h"
#include "world/ChunkStore.h"
#include "world/World.h"

#include <algorithm>

void ChunkMeshPipeline::enqueueMeshBuilds(int maxInFlight) {
    if (!m_threadPool) return;

    std::vector<Chunk*> candidates;
    int availableSlots = 0;
    m_chunks.withShared([&](ChunkStore& store) {
        int inFlight = 0;
        store.forEachSharedUnlocked([&](const Chunk* chunk) {
            if (chunk->meshInProgress.load()) ++inFlight;
        });
        availableSlots = std::max(1, maxInFlight) - inFlight;
        if (availableSlots <= 0) return;

        size_t total = 0;
        store.forEachSharedUnlocked([&](const Chunk*) { ++total; });
        candidates.reserve(total);
        store.forEachSharedUnlocked([&](Chunk* chunk) {
            if (!chunk->isDirty()) return;
            if (chunk->meshInProgress.load()) return;
            if (!chunk->generated.load()) return;  // not generated yet
            candidates.push_back(chunk);
        });
    });
    if (availableSlots <= 0) return;

    const int centerChunkX = m_world.centerChunkX();
    const int centerChunkZ = m_world.centerChunkZ();
    std::sort(candidates.begin(), candidates.end(),
              [centerChunkX, centerChunkZ](const Chunk* a, const Chunk* b) {
                  const int64_t adx = static_cast<int64_t>(a->cx) - centerChunkX;
                  const int64_t adz = static_cast<int64_t>(a->cz) - centerChunkZ;
                  const int64_t bdx = static_cast<int64_t>(b->cx) - centerChunkX;
                  const int64_t bdz = static_cast<int64_t>(b->cz) - centerChunkZ;
                  return adx * adx + adz * adz < bdx * bdx + bdz * bdz;
              });

    const int enqueueCount = std::min(
        {m_world.meshChunksPerFrame(), availableSlots,
         static_cast<int>(candidates.size())});
    for (int i = 0; i < enqueueCount; ++i) {
        Chunk* chunkPtr = candidates[static_cast<size_t>(i)];

        chunkPtr->meshInProgress = true;
        chunkPtr->markClean();  // Mark clean NOW so we don't re-enqueue
        const uint64_t revision = chunkPtr->dataRevision();

        // Capture a raw pointer — the chunk is owned by the ChunkStore and
        // won't be destroyed while meshInProgress is true
        World* worldPtr = &m_world;
        const int dx = chunkPtr->cx - centerChunkX;
        const int dz = chunkPtr->cz - centerChunkZ;
        const int distance2 = dx * dx + dz * dz;

        m_threadPool->enqueuePriority([chunkPtr, worldPtr, revision]() {
            // Build mesh into pending buffer
            auto neighborFunc = [worldPtr](int wx, int wy, int wz) -> BlockId {
                return worldPtr->getBlock(wx, wy, wz);
            };
            auto lightFunc = [worldPtr](int wx, int wy, int wz) -> LightSample {
                return worldPtr->getLight(wx, wy, wz);
            };

            chunkPtr->m_pendingMesh.build(
                chunkPtr->worldX(), chunkPtr->worldZ(),
                chunkPtr->rawBlocks(),
                chunkPtr->getColumnMaxYData(),
                neighborFunc, lightFunc
            );
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
        store.forEachSharedUnlocked([&](Chunk* chunk) {
            if (chunk->meshReady.load()) ready.push_back(chunk);
        });
    });
    const int centerChunkX = m_world.centerChunkX();
    const int centerChunkZ = m_world.centerChunkZ();
    std::sort(ready.begin(), ready.end(),
              [centerChunkX, centerChunkZ](const Chunk* a, const Chunk* b) {
                  const int64_t adx = static_cast<int64_t>(a->cx) - centerChunkX;
                  const int64_t adz = static_cast<int64_t>(a->cz) - centerChunkZ;
                  const int64_t bdx = static_cast<int64_t>(b->cx) - centerChunkX;
                  const int64_t bdz = static_cast<int64_t>(b->cz) - centerChunkZ;
                  return adx * adx + adz * adz < bdx * bdx + bdz * bdz;
              });
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
