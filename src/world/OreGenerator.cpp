#include "world/OreGenerator.h"
#include "world/Noise.h"
#include "Config.h"

#include <random>
#include <cmath>

OreGenerator::OreGenerator(const Noise& noise, uint64_t seed)
    : m_noise(noise)
{
    std::mt19937_64 rng(seed);
    auto randOff = [&rng]() -> float {
        return static_cast<float>(rng() % 10000) * 0.1f;
    };

    m_coalOffX    = randOff(); m_coalOffY    = randOff(); m_coalOffZ    = randOff();
    m_ironOffX    = randOff(); m_ironOffY    = randOff(); m_ironOffZ    = randOff();
    m_goldOffX    = randOff(); m_goldOffY    = randOff(); m_goldOffZ    = randOff();
    m_diamondOffX = randOff(); m_diamondOffY = randOff(); m_diamondOffZ = randOff();
}

BlockId OreGenerator::getOre(float wx, float wy, float wz,
                              BlockId existing) const {
    // Only replace stone-like blocks
    if (existing != BlockId::STONE && existing != BlockId::DEEPSLATE) {
        return BlockId::AIR;
    }

    // Coal: common, wide Y range, high threshold = large veins
    if (wy >= static_cast<float>(Config::ORE_COAL_MIN_Y) &&
        wy <= static_cast<float>(Config::ORE_COAL_MAX_Y)) {
        float n = std::abs(m_noise.octave3D(
            wx * Config::ORE_COAL_SCALE + m_coalOffX,
            wy * Config::ORE_COAL_SCALE + m_coalOffY,
            wz * Config::ORE_COAL_SCALE + m_coalOffZ,
            2, Config::NOISE_PERSISTENCE, Config::NOISE_LACUNARITY));
        if (n > Config::ORE_COAL_THRESHOLD) return BlockId::COAL_ORE;
    }

    // Iron: medium frequency, mid Y range
    if (wy >= static_cast<float>(Config::ORE_IRON_MIN_Y) &&
        wy <= static_cast<float>(Config::ORE_IRON_MAX_Y)) {
        float n = std::abs(m_noise.octave3D(
            wx * Config::ORE_IRON_SCALE + m_ironOffX,
            wy * Config::ORE_IRON_SCALE + m_ironOffY,
            wz * Config::ORE_IRON_SCALE + m_ironOffZ,
            2, Config::NOISE_PERSISTENCE, Config::NOISE_LACUNARITY));
        if (n > Config::ORE_IRON_THRESHOLD) return BlockId::IRON_ORE;
    }

    // Gold: lower Y, rarer
    if (wy >= static_cast<float>(Config::ORE_GOLD_MIN_Y) &&
        wy <= static_cast<float>(Config::ORE_GOLD_MAX_Y)) {
        float n = std::abs(m_noise.octave3D(
            wx * Config::ORE_GOLD_SCALE + m_goldOffX,
            wy * Config::ORE_GOLD_SCALE + m_goldOffY,
            wz * Config::ORE_GOLD_SCALE + m_goldOffZ,
            2, Config::NOISE_PERSISTENCE, Config::NOISE_LACUNARITY));
        if (n > Config::ORE_GOLD_THRESHOLD) return BlockId::GOLD_ORE;
    }

    // Diamond: deepest, rarest
    if (wy >= static_cast<float>(Config::ORE_DIAMOND_MIN_Y) &&
        wy <= static_cast<float>(Config::ORE_DIAMOND_MAX_Y)) {
        float n = std::abs(m_noise.octave3D(
            wx * Config::ORE_DIAMOND_SCALE + m_diamondOffX,
            wy * Config::ORE_DIAMOND_SCALE + m_diamondOffY,
            wz * Config::ORE_DIAMOND_SCALE + m_diamondOffZ,
            2, Config::NOISE_PERSISTENCE, Config::NOISE_LACUNARITY));
        if (n > Config::ORE_DIAMOND_THRESHOLD) return BlockId::DIAMOND_ORE;
    }

    return BlockId::AIR;
}
