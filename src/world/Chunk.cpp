#include "world/Chunk.h"
#include <algorithm>
#include <stdexcept>

Chunk::Chunk(int cx, int cz) : cx(cx), cz(cz) {
    // blocks already zero-initialized (AIR)
    for (auto& column : m_columnMaxY)
        std::fill(std::begin(column), std::end(column), Config::WORLD_MIN_Y - 1);
}

void Chunk::loadRawBlocks(const std::vector<uint8_t>& blocks) {
    if (blocks.size() != m_blocks.size())
        throw std::runtime_error("Generated chunk cache has the wrong size");
    std::unique_lock lock(m_dataMutex);
    std::copy(blocks.begin(), blocks.end(), m_blocks.begin());
    ++m_blockRevision;
    ++m_dataRevision;
    for (int x = 0; x < Config::CHUNK_SIZE_X; ++x)
        for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z)
            recalcColumnMax(x, z);
    m_dirty = true;
}

void Chunk::replaceRawLight(const std::vector<uint8_t>& light) {
    if (light.size() != m_light.size())
        throw std::runtime_error("Chunk light replacement has the wrong size");
    std::unique_lock lock(m_dataMutex);
    std::copy(light.begin(), light.end(), m_light.begin());
    ++m_dataRevision;
}

BlockId Chunk::getBlock(int x, int y, int z) const {
    if (x < 0 || x >= Config::CHUNK_SIZE_X ||
        !Config::isValidWorldY(y) ||
        z < 0 || z >= Config::CHUNK_SIZE_Z) {
        return BlockId::AIR;
    }
    std::shared_lock lock(m_dataMutex);
    return static_cast<BlockId>(m_blocks[index(x, y, z)]);
}

void Chunk::setBlock(int x, int y, int z, BlockId id) {
    if (x < 0 || x >= Config::CHUNK_SIZE_X ||
        !Config::isValidWorldY(y) ||
        z < 0 || z >= Config::CHUNK_SIZE_Z) {
        return;
    }

    std::unique_lock lock(m_dataMutex);
    if (m_blocks[index(x, y, z)] != static_cast<uint8_t>(id))
        ++m_blockRevision;
    m_blocks[index(x, y, z)] = static_cast<uint8_t>(id);
    ++m_dataRevision;
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
    for (int y = Config::WORLD_MAX_Y - 1; y >= Config::WORLD_MIN_Y; --y) {
        if (m_blocks[index(x, y, z)] != 0) {
            m_columnMaxY[x][z] = y;
            return;
        }
    }
    m_columnMaxY[x][z] = Config::WORLD_MIN_Y - 1;
}
