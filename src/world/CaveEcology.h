#pragma once

#include "world/BiomeBlockLogic.h"
#include "world/CaveGenerator.h"
#include "world/SurfaceRules.h"

namespace CaveEcology {
inline BlockId surface(uint64_t seed, int x, int y, int z, CaveBiome biome,
                       BlockId fallback) {
    const float patch = SurfaceRules::patch(seed,x,z,
        0x45434F3136434156ULL ^ WorldGenContext::mix(
            static_cast<uint64_t>(SurfaceRules::floorDiv(y,16))));
    if (patch < 0.60f) return fallback;
    switch (biome) {
        case CaveBiome::Neutral: return BlockId::IRON_STAINED_ROCK;
        case CaveBiome::VerdantGrotto: return BlockId::CAVE_MOSS;
        case CaveBiome::DripstoneKarst:
            return patch > 0.77f ? BlockId::GYPSUM : BlockId::WET_LIMESTONE;
        case CaveBiome::CrystalHollow:
            return patch > 0.77f ? BlockId::QUARTZ_BLOCK : BlockId::AMETHYST_BLOCK;
        case CaveBiome::VolcanicDepths: return BlockId::SULFUR_ROCK;
    }
    return fallback;
}

// Each cave-floor plane uses an independent 8x8 grid. Reconstructing nearby
// anchors makes a five-column cluster identical on either side of a boundary.
inline BlockId cluster(uint64_t seed, int x, int floorY, int z) {
    const int cx = SurfaceRules::floorDiv(x,8), cz = SurfaceRules::floorDiv(z,8);
    for (int dz=-1; dz<=1; ++dz) for (int dx=-1; dx<=1; ++dx) {
        const int cellX=cx+dx, cellZ=cz+dz;
        const uint64_t hash = WorldGenContext::hashPosition(
            WorldGenContext(seed).derive(0x45434F3136435259ULL),cellX,floorY,cellZ);
        if (hash%100 >= 15) continue;
        const int ax=cellX*8+static_cast<int>((hash>>8)%8);
        const int az=cellZ*8+static_cast<int>((hash>>24)%8);
        if (std::abs(x-ax)+std::abs(z-az)<=1)
            return (hash>>40)%2 ? BlockId::AMETHYST_CLUSTER : BlockId::QUARTZ_CLUSTER;
    }
    return BlockId::AIR;
}
} // namespace CaveEcology
