#pragma once

#include "world/Block.h"
#include <cstdint>

class Noise;

// ── Ore Generator ────────────────────────────────────────────────────────
// Places ore veins in stone/deepslate using 3D noise thresholding.
// Each ore type has its own noise scale, threshold, and Y range.
//
class OreGenerator {
public:
    OreGenerator(const Noise& noise, uint64_t seed);

    // Returns BlockId::AIR if no ore here, or the appropriate ore block.
    // Only replaces STONE or DEEPSLATE.
    BlockId getOre(float worldX, float worldY, float worldZ,
                   BlockId existingBlock) const;

private:
    const Noise& m_noise;

    // Seed-derived offsets for each ore layer (decorrelates them)
    float m_coalOffX,    m_coalOffY,    m_coalOffZ;
    float m_ironOffX,    m_ironOffY,    m_ironOffZ;
    float m_goldOffX,    m_goldOffY,    m_goldOffZ;
    float m_diamondOffX, m_diamondOffY, m_diamondOffZ;
};
