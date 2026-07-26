#include "world/OreGenerator.h"
#include "world/Noise.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr uint64_t COAL_DOMAIN = 0x4F52455F434F414CULL;
constexpr uint64_t IRON_DOMAIN = 0x4F52455F49524F4EULL;
constexpr uint64_t GOLD_DOMAIN = 0x4F52455F474F4C44ULL;
constexpr uint64_t DIAM_DOMAIN = 0x4F52455F4449414DULL;
constexpr uint64_t VEIN_DOMAIN = 0x4F52455F5645494EULL;
}

void OreGenerator::configure(FastNoiseLite& noise, int seed, float frequency) {
    noise.SetSeed(seed);
    noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
    noise.SetFrequency(frequency);
    noise.SetFractalType(FastNoiseLite::FractalType_Ridged);
    noise.SetFractalOctaves(2);
    noise.SetFractalLacunarity(2.0f);
    noise.SetFractalGain(0.55f);
}

OreGenerator::OreGenerator(const Noise&, uint64_t seed)
    : m_context(seed)
{
    configure(m_coal, m_context.noiseSeed(COAL_DOMAIN), 0.045f);
    configure(m_iron, m_context.noiseSeed(IRON_DOMAIN), 0.052f);
    configure(m_gold, m_context.noiseSeed(GOLD_DOMAIN), 0.060f);
    configure(m_diamond, m_context.noiseSeed(DIAM_DOMAIN), 0.070f);
    configure(m_vein, m_context.noiseSeed(VEIN_DOMAIN), 0.012f);
}

float OreGenerator::triangle(float y, float minY, float peakY, float maxY) {
    if (y <= minY || y >= maxY) return 0.0f;
    if (y <= peakY) return (y - minY) / (peakY - minY);
    return (maxY - y) / (maxY - peakY);
}

BlockId OreGenerator::getOre(float x, float y, float z, BlockId existing) const {
    if (existing != BlockId::STONE && existing != BlockId::DEEPSLATE)
        return BlockId::AIR;

    float vein = 0.5f + 0.5f * m_vein.GetNoise(x, y * 0.75f, z);
    auto score = [vein, x, y, z](const FastNoiseLite& noise) {
        float n = 0.5f + 0.5f * noise.GetNoise(x, y, z);
        return n * 0.72f + vein * 0.28f;
    };

    const float diamondY = std::clamp((16.0f - y) / 76.0f, 0.0f, 1.0f);
    if (diamondY > 0.0f && score(m_diamond) > 0.975f - diamondY * 0.020f)
        return BlockId::DIAMOND_ORE;

    const float goldY = triangle(y, -64.0f, -16.0f, 33.0f);
    if (goldY > 0.0f && score(m_gold) > 0.968f - goldY * 0.018f)
        return BlockId::GOLD_ORE;

    const float lowIron = triangle(y, -64.0f, 16.0f, 73.0f);
    const float highIron = std::clamp((y - 80.0f) / 176.0f, 0.0f, 1.0f);
    const float ironY = std::max(lowIron, highIron);
    if (ironY > 0.0f && score(m_iron) > 0.962f - ironY * 0.022f)
        return BlockId::IRON_ORE;

    const float coalY = triangle(y, -1.0f, 96.0f, 257.0f);
    if (coalY > 0.0f && score(m_coal) > 0.958f - coalY * 0.024f)
        return BlockId::COAL_ORE;

    return BlockId::AIR;
}
