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

    // X/Z are local coordinates; Y is an absolute world coordinate.
    BlockId getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, BlockId id);

    // Column height cache
    int getColumnMaxY(int x, int z) const { return m_columnMaxY[x][z]; }
    const int (*getColumnMaxYData() const)[Config::CHUNK_SIZE_Z] { return m_columnMaxY; }

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
    const uint8_t* rawBlocks() const { return m_blocks.data(); }
    void loadRawBlocks(const std::vector<uint8_t>& blocks);
    uint8_t getPackedLight(int x, int y, int z) const {
        return x < 0 || x >= Config::CHUNK_SIZE_X || !Config::isValidWorldY(y) ||
               z < 0 || z >= Config::CHUNK_SIZE_Z ? 0 : m_light[index(x,y,z)];
    }
    uint8_t getBlockLight(int x, int y, int z) const { return getPackedLight(x,y,z) & 0x0f; }
    uint8_t getSkyLight(int x, int y, int z) const { return getPackedLight(x,y,z) >> 4; }
    void setBlockLight(int x, int y, int z, uint8_t value) {
        auto& packed=m_light[index(x,y,z)];
        const uint8_t next=static_cast<uint8_t>((packed&0xf0)|(value&0x0f));
        if(packed!=next){packed=next;++m_dataRevision;}
    }
    void setSkyLight(int x, int y, int z, uint8_t value) {
        auto& packed=m_light[index(x,y,z)];
        const uint8_t next=static_cast<uint8_t>((packed&0x0f)|((value&0x0f)<<4));
        if(packed!=next){packed=next;++m_dataRevision;}
    }
    void clearLight() { m_light.fill(0); ++m_dataRevision; }
    uint64_t dataRevision() const { return m_dataRevision.load(); }

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

    // Atomic flags for async mesh building
    std::atomic<bool> meshReady{false};      // worker finished building pending mesh
    std::atomic<bool> meshInProgress{false}; // worker is currently building
    ChunkMesh m_pendingMesh;                 // built by worker thread
    std::atomic<uint64_t> pendingMeshRevision{0};

private:
    // Flat array: blocks[x + z*16 + y*16*16]
    std::array<uint8_t, Config::CHUNK_VOLUME> m_blocks{};
    std::array<uint8_t, Config::CHUNK_VOLUME> m_light{};
    int m_columnMaxY[Config::CHUNK_SIZE_X][Config::CHUNK_SIZE_Z]{};

    bool m_dirty = true;
    std::atomic<uint64_t> m_dataRevision{1};

    ChunkMesh m_mesh;
    std::mutex m_meshMutex;

    static inline int index(int x, int y, int z) {
        return x + z * Config::CHUNK_SIZE_X
                 + Config::worldYToStorageY(y) * Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z;
    }

    void recalcColumnMax(int x, int z);
};
