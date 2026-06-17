#pragma once

#include <cstdint>
#include <array>
#include <mutex>
#include <atomic>

#include "Config.h"
#include "world/Block.h"
#include "world/ChunkMesh.h"

class Chunk {
public:
    Chunk(int cx, int cz);

    // Chunk coordinates
    int cx, cz;

    int worldX() const { return cx * Config::CHUNK_SIZE_X; }
    int worldZ() const { return cz * Config::CHUNK_SIZE_Z; }

    // Voxel access (local coordinates: 0..15, 0..127, 0..15)
    BlockId getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, BlockId id);

    // Column height cache
    int getColumnMaxY(int x, int z) const { return m_columnMaxY[x][z]; }
    const int (*getColumnMaxYData() const)[Config::CHUNK_SIZE_Z] { return m_columnMaxY; }

    // Global max Y across all columns (for tighter frustum culling AABB)
    int getGlobalMaxY() const {
        int maxY = 0;
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

    // Column max write access (for WorldGenerator)
    void setColumnMaxY(int x, int z, int val) { m_columnMaxY[x][z] = val; }

    // Double-buffered mesh
    ChunkMesh& getMesh() { return m_mesh; }
    const ChunkMesh& getMesh() const { return m_mesh; }
    std::mutex& getMeshMutex() { return m_meshMutex; }

    // Atomic flags for async chunk generation (runs before mesh building)
    std::atomic<bool> generationInProgress{false}; // worker is generating terrain into this chunk
    std::atomic<bool> generated{false};            // generation complete, block data is valid

    // Atomic flags for async mesh building
    std::atomic<bool> meshReady{false};      // worker finished building pending mesh
    std::atomic<bool> meshInProgress{false}; // worker is currently building
    ChunkMesh m_pendingMesh;                 // built by worker thread

private:
    // Flat array: blocks[x + z*16 + y*16*16]
    std::array<uint8_t, Config::CHUNK_VOLUME> m_blocks{};
    int m_columnMaxY[Config::CHUNK_SIZE_X][Config::CHUNK_SIZE_Z]{};

    bool m_dirty = true;

    ChunkMesh m_mesh;
    std::mutex m_meshMutex;

    static inline int index(int x, int y, int z) {
        return x + z * Config::CHUNK_SIZE_X
                 + y * Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z;
    }

    void recalcColumnMax(int x, int z);
};
