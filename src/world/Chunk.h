#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <limits>

#include "Config.h"
#include "world/Block.h"
#include "world/ChunkMesh.h"

enum class ChunkLifecycleState : uint8_t {
    Requested,
    CacheReading,
    Generating,
    LocalLighting,
    BoundaryLighting,
    WaitingForMesh,
    Renderable,
    Warm
};

class Chunk {
public:
    using LifecycleState = ChunkLifecycleState;

    Chunk(int cx, int cz);

    // Chunk coordinates
    int cx, cz;

    int worldX() const { return cx * Config::CHUNK_SIZE_X; }
    int worldZ() const { return cz * Config::CHUNK_SIZE_Z; }

    // X/Z are local coordinates; Y is an absolute world coordinate.
    BlockId getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, BlockId id);

    // Column height cache
    int getColumnMaxY(int x, int z) const { return m_columnMaxY[x][z]; }
    const int (*getColumnMaxYData() const)[Config::CHUNK_SIZE_Z] { return m_columnMaxY; }
    void copyColumnMaxY(int out[Config::CHUNK_SIZE_X][Config::CHUNK_SIZE_Z]) const {
        std::shared_lock lock(m_dataMutex);
        for (int x = 0; x < Config::CHUNK_SIZE_X; ++x)
            for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z)
                out[x][z] = m_columnMaxY[x][z];
    }

    // Global max Y across all columns (for tighter frustum culling AABB)
    int getGlobalMaxY() const {
        int maxY = Config::WORLD_MIN_Y - 1;
        for (int x = 0; x < Config::CHUNK_SIZE_X; ++x)
            for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z)
                if (m_columnMaxY[x][z] > maxY) maxY = m_columnMaxY[x][z];
        return maxY;
    }

    // Dirty flag
    bool isDirty() const { return m_dirty; }
    void markClean()  { m_dirty = false; }
    void markDirty()  { m_dirty = true; }

    // Raw block array access (for WorldGenerator)
    uint8_t& blockAt(int x, int y, int z) { return m_blocks[index(x, y, z)]; }
    const uint8_t& blockAt(int x, int y, int z) const { return m_blocks[index(x, y, z)]; }
    // Generation owns an unpublished chunk exclusively. It may fill through
    // blockAt(), update column maxima, then publish the whole edit once.
    void finishBulkBlockEdit() { ++m_dataRevision; m_dirty = true; }
    const uint8_t* rawBlocks() const { return m_blocks.data(); }
    void copyRawState(std::vector<uint8_t>& blocks,
                      std::vector<uint8_t>& light) const {
        std::shared_lock lock(m_dataMutex);
        blocks.assign(m_blocks.begin(), m_blocks.end());
        light.assign(m_light.begin(), m_light.end());
    }
    void loadRawBlocks(const std::vector<uint8_t>& blocks);

    // A base snapshot is captured before player overrides are applied.  It
    // lets the asynchronous generated-cache writer persist terrain without
    // accidentally folding edits or derived fluid states into the base cache.
    void captureBaseSnapshot() {
        m_baseBlocks.assign(m_blocks.begin(), m_blocks.end());
    }
    bool hasBaseSnapshot() const { return !m_baseBlocks.empty(); }
    const std::vector<uint8_t>& baseSnapshot() const { return m_baseBlocks; }
    void setBaseBlock(int x, int y, int z, BlockId id) {
        if (!m_baseBlocks.empty()) m_baseBlocks[index(x, y, z)] =
            static_cast<uint8_t>(id);
    }
    void clearBaseSnapshot() { m_baseBlocks.clear(); m_baseBlocks.shrink_to_fit(); }
    uint8_t getPackedLight(int x, int y, int z) const {
        if (x < 0 || x >= Config::CHUNK_SIZE_X ||
            !Config::isValidWorldY(y) || z < 0 || z >= Config::CHUNK_SIZE_Z)
            return 0;
        std::shared_lock lock(m_dataMutex);
        return m_light[index(x,y,z)];
    }
    uint8_t getBlockLight(int x, int y, int z) const { return getPackedLight(x,y,z) & 0x0f; }
    uint8_t getSkyLight(int x, int y, int z) const { return getPackedLight(x,y,z) >> 4; }
    void setBlockLight(int x, int y, int z, uint8_t value) {
        std::unique_lock lock(m_dataMutex);
        auto& packed=m_light[index(x,y,z)];
        const uint8_t next=static_cast<uint8_t>((packed&0xf0)|(value&0x0f));
        if(packed!=next){packed=next;++m_dataRevision;}
    }
    void setSkyLight(int x, int y, int z, uint8_t value) {
        std::unique_lock lock(m_dataMutex);
        auto& packed=m_light[index(x,y,z)];
        const uint8_t next=static_cast<uint8_t>((packed&0x0f)|((value&0x0f)<<4));
        if(packed!=next){packed=next;++m_dataRevision;}
    }
    void clearLight() {
        std::unique_lock lock(m_dataMutex);
        m_light.fill(0);
        ++m_dataRevision;
    }
    uint64_t dataRevision() const { return m_dataRevision.load(); }

    // Fluid-derived edits can be visually coalesced without changing the
    // block-data revision used by persistence and mesh correctness checks.
    void markFluidMutation(uint64_t tick) {
        m_fluidRevision.fetch_add(1, std::memory_order_relaxed);
        m_lastFluidMutationTick.store(tick, std::memory_order_relaxed);
    }
    uint64_t fluidRevision() const {
        return m_fluidRevision.load(std::memory_order_relaxed);
    }
    uint64_t fluidMeshRevision() const {
        return m_fluidMeshRevision.load(std::memory_order_relaxed);
    }
    void markNonFluidMutation() {
        m_nonFluidRevision.fetch_add(1, std::memory_order_relaxed);
    }
    uint64_t nonFluidRevision() const {
        return m_nonFluidRevision.load(std::memory_order_relaxed);
    }
    uint64_t nonFluidMeshRevision() const {
        return m_nonFluidMeshRevision.load(std::memory_order_relaxed);
    }
    uint64_t pendingNonFluidMeshRevision() const {
        return m_pendingNonFluidMeshRevision.load(std::memory_order_relaxed);
    }
    uint64_t pendingFluidMeshRevision() const {
        return m_pendingFluidMeshRevision.load(std::memory_order_relaxed);
    }
    uint64_t lastFluidMeshAttemptTick() const {
        return m_lastFluidMeshAttemptTick.load(std::memory_order_relaxed);
    }
    void markFluidMeshAttempt(uint64_t tick) {
        m_lastFluidMeshAttemptTick.store(tick, std::memory_order_relaxed);
    }
    void markFluidMeshRevision(uint64_t revision) {
        m_fluidMeshRevision.store(revision, std::memory_order_relaxed);
    }
    void markPendingFluidMeshRevision(uint64_t revision) {
        m_pendingFluidMeshRevision.store(revision, std::memory_order_relaxed);
    }
    void markPendingNonFluidMeshRevision(uint64_t revision) {
        m_pendingNonFluidMeshRevision.store(revision, std::memory_order_relaxed);
    }
    void markNonFluidMeshRevision(uint64_t revision) {
        m_nonFluidMeshRevision.store(revision, std::memory_order_relaxed);
    }

    // Column max write access (for WorldGenerator)
    void setColumnMaxY(int x, int z, int val) { m_columnMaxY[x][z] = val; }

    // Double-buffered mesh
    ChunkMesh& getMesh() { return m_mesh; }
    const ChunkMesh& getMesh() const { return m_mesh; }
    std::mutex& getMeshMutex() { return m_meshMutex; }

    // Atomic flags for async chunk generation (runs before mesh building)
    std::atomic<bool> generationInProgress{false}; // worker is generating terrain into this chunk
    std::atomic<bool> generated{false};            // generation complete, block data is valid
    std::atomic<bool> lightingInitialized{false};
    std::atomic<LifecycleState> lifecycle{LifecycleState::Requested};

    // Cache I/O is deliberately separate from generation.  A chunk remains
    // unpublished until cacheChecked is true, so a cache miss can cleanly
    // enter the region/singleton generation selector.
    std::atomic<bool> cacheReadInProgress{false};
    std::atomic<bool> cacheChecked{false};
    std::atomic<bool> cacheHit{false};
    std::atomic<bool> baseCacheInProgress{false};
    std::atomic<bool> baseCacheDirty{false};

    // Atomic flags for async mesh building
    std::atomic<bool> meshReady{false};      // worker finished building pending mesh
    std::atomic<bool> meshInProgress{false}; // worker is currently building
    ChunkMesh m_pendingMesh;                 // built by worker thread
    std::atomic<uint64_t> pendingMeshRevision{0};

private:
    // Flat array: blocks[x + z*16 + y*16*16]
    std::array<uint8_t, Config::CHUNK_VOLUME> m_blocks{};
    std::array<uint8_t, Config::CHUNK_VOLUME> m_light{};
    std::vector<uint8_t> m_baseBlocks;
    mutable std::shared_mutex m_dataMutex;
    int m_columnMaxY[Config::CHUNK_SIZE_X][Config::CHUNK_SIZE_Z]{};

    bool m_dirty = true;
    std::atomic<uint64_t> m_dataRevision{1};
    std::atomic<uint64_t> m_fluidRevision{0};
    std::atomic<uint64_t> m_fluidMeshRevision{0};
    std::atomic<uint64_t> m_pendingFluidMeshRevision{0};
    std::atomic<uint64_t> m_nonFluidRevision{0};
    std::atomic<uint64_t> m_nonFluidMeshRevision{0};
    std::atomic<uint64_t> m_pendingNonFluidMeshRevision{0};
    std::atomic<uint64_t> m_lastFluidMutationTick{0};
    // UINT64_MAX means that no fluid-derived mesh snapshot has been
    // attempted yet; this keeps a real tick-0 attempt distinguishable.
    std::atomic<uint64_t> m_lastFluidMeshAttemptTick{
        std::numeric_limits<uint64_t>::max()};

    ChunkMesh m_mesh;
    std::mutex m_meshMutex;

    static inline int index(int x, int y, int z) {
        return x + z * Config::CHUNK_SIZE_X
                 + Config::worldYToStorageY(y) * Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z;
    }

    void recalcColumnMax(int x, int z);
};

// Decode a chunk-local override index (x + z*CHUNK_SIZE_X
// + storageY*CHUNK_SIZE_X*CHUNK_SIZE_Z) into local (x, z) and world Y.
// Inverse of Chunk::index(). The Y component is a world coordinate, not a
// storage offset.
inline void decodeChunkIndex(uint32_t index, int& x, int& z, int& worldY) {
    const int rem = static_cast<int>(
        index % (Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z));
    x = rem % Config::CHUNK_SIZE_X;
    z = rem / Config::CHUNK_SIZE_X;
    worldY = Config::storageYToWorldY(
        static_cast<int>(index / (Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z)));
}
