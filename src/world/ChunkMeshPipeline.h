#pragma once

#include <cstddef>
#include <cstdint>

#include "Config.h"

class Chunk;
class ChunkStore;
class IGameRenderer;
class ThreadPool;
class World;

// Owns the async chunk mesh pipeline: dirty-chunk collection, worker
// dispatch, revision-checked GPU upload, synchronous fallback building, and
// GPU mesh release/restore around context loss. Block/light sampling goes
// through the owning World; the chunk lock is the ChunkStore's, taken per
// collection pass.
class ChunkMeshPipeline {
public:
    ChunkMeshPipeline(World& world, ChunkStore& chunks)
        : m_world(world), m_chunks(chunks) {}

    void setThreadPool(ThreadPool* pool) { m_threadPool = pool; }

    // Enqueue mesh builds for dirty chunks (async via thread pool).
    void enqueueMeshBuilds(
        int maxInFlight = Config::CHUNK_MESH_TASKS_IN_FLIGHT);

    // Check for completed async mesh builds and upload them to GPU.
    // maxUploads caps uploads per frame to avoid pipeline stalls.
    void processCompletedMeshes(IGameRenderer* renderer, int maxUploads = 4,
                                size_t maxUploadBytes =
                                    Config::MESH_UPLOAD_BYTES_PER_FRAME);

    // Synchronous build (for first frame or when thread pool unavailable).
    void buildMeshesSync(IGameRenderer* renderer, int maxCount = 16);

    void invalidateGpuMeshes();
    void restoreGpuMeshes();

    // Release one chunk's GPU mesh. Caller must hold the ChunkStore lock
    // (the unload path already does); takes the per-chunk mesh mutex.
    void releaseChunkMesh(Chunk* chunk);

    // Release every loaded chunk's GPU mesh (reset / teardown). Locks.
    void releaseAllMeshes();

private:
    World& m_world;
    ChunkStore& m_chunks;
    ThreadPool* m_threadPool = nullptr;
    IGameRenderer* m_renderer = nullptr;
};
