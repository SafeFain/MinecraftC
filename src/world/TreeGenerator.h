#pragma once

#include "world/BiomeMap.h"
#include "world/RegionGenerationData.h"
#include <vector>
#include <cstdint>

class Noise;

// ── Tree Generator ──────────────────────────────────────────────────────
// Uses Poisson Disk Sampling to place trees at naturally-spaced intervals.
//
// Two modes:
//   Region-wide (primary): generateTreesRegion() uses a shared placement
//     grid so distance checks span all chunks within the region.
//   Per-chunk (fallback):   generateTrees() for singleton chunks that
//     can't form a full region.
//
class TreeGenerator {
public:
    struct TreePlacement {
        int localX, localZ;      // local to chunk (generateTrees) or region world origin (generateTreesRegion)
        int baseY;               // ground Y (trunk starts here)
        int trunkHeight;
        TreeType type;
    };

    TreeGenerator(uint64_t seed);

    // ── Region-wide Poisson disk (primary path) ─────────────────────────
    // placementsOut: tree positions relative to worldOriginX/worldOriginZ
    void generateTreesRegion(
        int worldOriginX, int worldOriginZ,
        int regionSizeX, int regionSizeZ,
        const int*     heightMap,    // flat array: [z * regionSizeX + x]
        const Biome*   biomeMap,     // flat array: [z * regionSizeX + x]
        const uint8_t* riverMap,     // flat array: [z * regionSizeX + x], 0 or 1
        int padding,
        std::vector<RegionGenerationData::TreePlacement>& placementsOut);

    // ── Per-chunk Poisson disk (legacy fallback) ────────────────────────
    std::vector<TreePlacement> generateTrees(
        int chunkWorldX, int chunkWorldZ,
        const int   heightMap[16][16],
        const Biome biomeMap[16][16],
        const bool  riverMap[16][16]);

    // Determine tree type from biome (random choice between biome's tree types)
    static TreeType chooseTreeType(Biome biome, uint64_t seed, int x, int z);

private:
    uint64_t m_seed;

    float biomeMinRadius(Biome b) const;

    // Legacy per-chunk validation
    bool isValidPlacement(int lx, int lz, int groundY, Biome biome,
                          const int  heightMap[16][16],
                          const bool riverMap[16][16]) const;

    // Region-wide validation (flat arrays)
    bool isValidPlacementRegion(int lx, int lz, int groundY, Biome biome,
                                const int*     heightMap,
                                const uint8_t* riverMap,
                                int regionSizeX) const;
};
