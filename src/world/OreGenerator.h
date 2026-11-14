#pragma once

#include "world/Block.h"
#include "world/Biome.h"
#include "world/WorldGenContext.h"
#include "FastNoiseLite.h"

#include <cstdint>
#include <functional>

class Noise;

class OreGenerator {
public:
    OreGenerator(const Noise& legacyNoise, uint64_t seed);

    BlockId getOre(float worldX, float worldY, float worldZ,
                   BlockId existingBlock, Biome biome = Biome::PLAINS,
                   const std::function<bool()>& exposed = {}) const;
    static bool emeraldBiome(Biome biome);

private:
    WorldGenContext m_context;
    FastNoiseLite m_coal;
    FastNoiseLite m_iron;
    FastNoiseLite m_gold;
    FastNoiseLite m_diamond;
    FastNoiseLite m_vein;

    static float triangle(float y, float minY, float peakY, float maxY);
    static void configure(FastNoiseLite& noise, int seed, float frequency);
};
