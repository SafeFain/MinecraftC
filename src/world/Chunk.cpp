#include "world/Chunk.h"
#include <algorithm>

Chunk::Chunk(int cx, int cz) : cx(cx), cz(cz) {
    // blocks already zero-initialized (AIR)
    // column_max_y already zero-initialized
}

BlockId Chunk::getBlock(int x, int y, int z) const {
    if (x < 0 || x >= Config::CHUNK_SIZE_X ||
        y < 0 || y >= Config::CHUNK_SIZE_Y ||
        z < 0 || z >= Config::CHUNK_SIZE_Z) {
        return BlockId::AIR;
    }
    return static_cast<BlockId>(m_blocks[index(x, y, z)]);
}

void Chunk::setBlock(int x, int y, int z, BlockId id) {
    if (x < 0 || x >= Config::CHUNK_SIZE_X ||
        y < 0 || y >= Config::CHUNK_SIZE_Y ||
        z < 0 || z >= Config::CHUNK_SIZE_Z) {
        return;
    }

    m_blocks[index(x, y, z)] = static_cast<uint8_t>(id);
    m_dirty = true;

    // Update column max height
    if (id == BlockId::AIR) {
        if (y >= m_columnMaxY[x][z]) {
            recalcColumnMax(x, z);
        }
    } else {
        if (y > m_columnMaxY[x][z]) {
            m_columnMaxY[x][z] = y;
        }
    }
}

void Chunk::recalcColumnMax(int x, int z) {
    for (int y = Config::CHUNK_SIZE_Y - 1; y >= 0; --y) {
        if (m_blocks[index(x, y, z)] != 0) {
            m_columnMaxY[x][z] = y;
            return;
        }
    }
    m_columnMaxY[x][z] = 0;
}
