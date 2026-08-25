#pragma once

#include "world/BiomeMap.h"
#include "world/RegionGenerationData.h"
#include "world/WorldGenContext.h"

#include <cstdint>
#include <functional>
#include <vector>

// Order-independent world-grid vegetation placement. Candidate anchors and
// their priorities are functions of world cell coordinates, never generation
// order or region origin.
class TreeGenerator {
public:
    struct TreePlacement {
        int localX, localZ;
        int baseY;
        int trunkHeight;
        TreeType type;
    };

    explicit TreeGenerator(uint64_t seed);

    void generateTreesRegion(
        int worldOriginX, int worldOriginZ,
        int regionSizeX, int regionSizeZ,
        const int* heightMap,
        const Biome* biomeMap,
        const uint8_t* riverMap,
        int padding,
        std::vector<RegionGenerationData::TreePlacement>& placementsOut) const;

    std::vector<TreePlacement> generateTrees(
        int chunkWorldX, int chunkWorldZ,
        const int heightMap[16][16],
        const Biome biomeMap[16][16],
        const bool riverMap[16][16]);

    using TerrainSampler = std::function<void(
        int worldX, int worldZ, int& height, Biome& biome, bool& river)>;
    std::vector<TreePlacement> generateTreesForArea(
        int worldOriginX, int worldOriginZ, int width, int depth,
        const TerrainSampler& sampleTerrain) const;

    static TreeType chooseTreeType(Biome biome, uint64_t seed, int x, int z);

private:
    struct Candidate {
        int x = 0;
        int z = 0;
        uint64_t priority = 0;
    };

    WorldGenContext m_context;
    uint64_t m_treeSeed;

    Candidate candidateForCell(int cellX, int cellZ) const;
    bool winsSpacing(const Candidate& candidate, float radius) const;
    bool accepts(Biome biome, int height, bool river,
                 int worldX, int worldZ) const;
    int trunkHeight(TreeType type, int worldX, int worldZ) const;
    int biomeTrunkHeight(Biome biome, TreeType type,
                         int worldX, int worldZ) const;
    static float radiusFor(Biome biome);
    static float chanceFor(Biome biome);
    static int floorDiv(int value, int divisor);
};
