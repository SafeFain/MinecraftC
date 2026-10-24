#pragma once

#include "world/Block.h"
#include "world/Noise.h"
#include "world/NetherStructures.h"
#include "Config.h"
#include "world/RegionGenerationData.h"
#include <array>
#include <cstdint>
#include <vector>

class Chunk;

enum class NetherBiome : uint8_t {
    Wastes, SoulSandValley, CrimsonForest, WarpedForest, BasaltDeltas
};

struct NetherSpan {
    int bottom = 0, top = 0;
    BlockId block = BlockId::AIR;
};

// One point authority supplies exact chunks, safe landings and cave-aware LOD.
class NetherGenerator {
public:
    static constexpr uint32_t VERSION = 1;
    explicit NetherGenerator(uint64_t seed) : m_seed(seed), m_noise(seed) {}
    NetherBiome biomeAt(int x, int z) const;
    std::vector<NetherStructure> structuresAt(int x,int z) const;
    std::optional<NetherStructure> locate(const std::string&,int x,int z,int radius=8192) const;
    BlockId sample(int x, int y, int z) const;
    std::array<BlockId, Config::CHUNK_SIZE_Y> column(int x, int z) const;
    std::vector<NetherSpan> spans(int x, int z) const;
    int surface(int x, int z) const;
    glm::ivec3 spawnBlock() const;
    void generate(Chunk& chunk) const;
    std::vector<RegionGenerationData::PendingBlock> structureBlocks(int,int,int,int) const;

private:
    bool solid(int x, int y, int z) const;
    BlockId base(int x, int y, int z) const;
    uint64_t m_seed;
    Noise m_noise;
};
