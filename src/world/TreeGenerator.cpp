#include "world/TreeGenerator.h"
#include "world/Block.h"
#include "Config.h"

#include <cmath>
#include <algorithm>

// ── Simple splitmix64 PRNG (lightweight, no heap state) ──────────────────

static inline uint64_t splitmix64(uint64_t& state) {
    state += 0x9E3779B97F4A7C15ULL;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static inline int splitmixInt(uint64_t& state, int minVal, int maxVal) {
    return minVal + static_cast<int>(splitmix64(state) % static_cast<uint64_t>(maxVal - minVal + 1));
}

TreeGenerator::TreeGenerator(uint64_t seed) : m_seed(seed) {}

// ── Biome → minimum spacing (Poisson disk radius) ──────────────────────

float TreeGenerator::biomeMinRadius(Biome b) const {
    switch (b) {
        case Biome::JUNGLE:   return 2.0f;
        case Biome::FOREST:   return 3.0f;
        case Biome::TAIGA:    return 3.0f;
        case Biome::SWAMP:    return 4.0f;
        case Biome::HILLS:    return 5.0f;
        case Biome::PLAINS:   return 6.0f;
        case Biome::SAVANNA:  return 6.0f;
        case Biome::MOUNTAINS:return 7.0f;
        case Biome::DESERT:   return 7.0f;
        default:              return 8.0f;
    }
}

// ── Choose tree type from biome ─────────────────────────────────────────

TreeType TreeGenerator::chooseTreeType(Biome biome, uint64_t seed, int x, int z) {
    const BiomeProperties& props = getBiomeProps(biome);
    if (props.treeDensity <= 0.0f) return TreeType::NONE;

    // Use position to deterministically choose between treeType1 and treeType2
    uint64_t h = static_cast<uint64_t>(x) * 6364136223846793005ULL
               + static_cast<uint64_t>(z) * 1442695040888963407ULL + seed;
    bool useSecond = (h & 1) != 0 && props.treeType2 != TreeType::NONE;
    return useSecond ? props.treeType2 : props.treeType1;
}

// ── Validate tree placement ─────────────────────────────────────────────

bool TreeGenerator::isValidPlacement(int lx, int lz, int groundY, Biome biome,
                                     const int  heightMap[16][16],
                                     const bool riverMap[16][16]) const {
    // Must have tree density > 0 for this biome
    if (getBiomeProps(biome).treeDensity <= 0.0f) return false;

    // Not in river
    if (riverMap[lx][lz]) return false;

    // Ground must be above water level
    if (groundY <= Config::SEA_LEVEL) return false;

    // Check slope: sample 3×3 neighborhood heights
    int centerH = heightMap[lx][lz];
    int maxHDiff = 0;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            int nx = lx + dx;
            int nz = lz + dz;
            if (nx < 0 || nx >= 16 || nz < 0 || nz >= 16) continue;
            int diff = std::abs(heightMap[nx][nz] - centerH);
            maxHDiff = std::max(maxHDiff, diff);
        }
    }
    if (maxHDiff > 2) return false;  // too steep

    return true;
}

// ── Poisson Disk Sampling ───────────────────────────────────────────────

std::vector<TreeGenerator::TreePlacement>
TreeGenerator::generateTrees(
    int chunkWorldX, int chunkWorldZ,
    const int   heightMap[16][16],
    const Biome biomeMap[16][16],
    const bool  riverMap[16][16])
{
    std::vector<TreePlacement> placements;

    // Per-chunk RNG from world coordinates + seed (lightweight splitmix64)
    uint64_t rngState = static_cast<uint64_t>(chunkWorldX) * 6364136223846793005ULL
                      + static_cast<uint64_t>(chunkWorldZ) * 1442695040888963407ULL
                      + m_seed;

    // Poisson disk: generate candidates using Bridson algorithm
    // Simplified for 16×16 chunk: generate random candidates and check distance
    const int MAX_CANDIDATES = 30;
    const int MAX_PLACEMENTS = 50;

    for (int attempt = 0; attempt < MAX_CANDIDATES && static_cast<int>(placements.size()) < MAX_PLACEMENTS; ++attempt) {
        // Generate candidate in [0, 15] (full chunk area, edges included)
        int cx = splitmixInt(rngState, 0, 15);
        int cz = splitmixInt(rngState, 0, 15);

        Biome biome = biomeMap[cx][cz];
        float localR = biomeMinRadius(biome);
        if (localR <= 0.0f) continue;

        int groundY = heightMap[cx][cz];

        // Validate
        if (!isValidPlacement(cx, cz, groundY, biome, heightMap, riverMap)) {
            continue;
        }

        // Check Poisson disk distance against existing placements
        // Use per-placement biome radius so jungle trees can be dense
        // while plains trees stay sparse even in mixed-biome chunks
        bool tooClose = false;
        for (const auto& p : placements) {
            float dx = static_cast<float>(cx - p.localX);
            float dz = static_cast<float>(cz - p.localZ);
            float dist = std::sqrt(dx * dx + dz * dz);
            // Use the tighter (smaller) radius of the two placements
            float minDist = std::min(localR, biomeMinRadius(biomeMap[p.localX][p.localZ]));
            if (dist < minDist) {
                tooClose = true;
                break;
            }
        }
        if (tooClose) continue;

        // Choose tree type
        TreeType type = chooseTreeType(biome, rngState, cx, cz);
        if (type == TreeType::NONE) continue;

        // Determine trunk height based on tree type
        int trunkH = 4;
        uint64_t posHash = static_cast<uint64_t>(cx) * 7919ULL
                         + static_cast<uint64_t>(cz) * 6287ULL + rngState;
        switch (type) {
            case TreeType::SPRUCE:    trunkH = 6 + static_cast<int>(posHash % 5);  break;  // 6-10
            case TreeType::JUNGLE:    trunkH = 8 + static_cast<int>(posHash % 5);  break;  // 8-12
            case TreeType::BIRCH:     trunkH = 5 + static_cast<int>(posHash % 3);  break;  // 5-7
            case TreeType::ACACIA:    trunkH = 5 + static_cast<int>(posHash % 3);  break;  // 5-7
            case TreeType::SWAMP_OAK: trunkH = 4 + static_cast<int>(posHash % 3);  break;  // 4-6
            case TreeType::CACTUS:    trunkH = 1 + static_cast<int>(posHash % 3);  break;  // 1-3
            default:                  trunkH = 4 + static_cast<int>(posHash % 3);  break;  // 4-6 OAK
        }

        placements.push_back({cx, cz, groundY, trunkH, type});
    }

    return placements;
}

// ═══════════════════════════════════════════════════════════════════════════
// Region-wide Poisson Disk Sampling
// ═══════════════════════════════════════════════════════════════════════════

void TreeGenerator::generateTreesRegion(
    int worldOriginX, int worldOriginZ,
    int regionSizeX, int regionSizeZ,
    const int*     heightMap,
    const Biome*   biomeMap,
    const uint8_t* riverMap,
    int padding,
    std::vector<RegionGenerationData::TreePlacement>& placementsOut)
{
    // Per-region RNG: deterministic from world origin + seed (lightweight splitmix64)
    uint64_t rngState = static_cast<uint64_t>(worldOriginX) * 6364136223846793005ULL
                      + static_cast<uint64_t>(worldOriginZ) * 1442695040888963407ULL
                      + m_seed;

    // Track placed positions across the entire region using a flat grid
    // placedGrid[lz * regionSizeX + lx] = have we placed a tree at (lx, lz)?
    std::vector<bool> placedGrid(static_cast<size_t>(regionSizeX) * static_cast<size_t>(regionSizeZ), false);

    // Store placements with (localX, localZ) to check distances
    struct Placed { int x, z; };
    std::vector<Placed> placedList;

    const int MAX_CANDIDATES = 200;    // scaled for region
    const int MAX_PLACEMENTS = 400;

    // Only place trees within the region core (exclude padding area)
    const int coreStart = padding;
    const int coreEndX  = regionSizeX - padding;
    const int coreEndZ  = regionSizeZ - padding;

    for (int attempt = 0; attempt < MAX_CANDIDATES && static_cast<int>(placementsOut.size()) < MAX_PLACEMENTS; ++attempt) {
        int cx = splitmixInt(rngState, coreStart, coreEndX - 1);
        int cz = splitmixInt(rngState, coreStart, coreEndZ - 1);

        size_t idx = static_cast<size_t>(cz) * static_cast<size_t>(regionSizeX) + static_cast<size_t>(cx);
        if (placedGrid[idx]) continue;

        Biome biome = biomeMap[idx];
        float localR = biomeMinRadius(biome);
        if (localR <= 0.0f) continue;

        int groundY = heightMap[idx];

        if (!isValidPlacementRegion(cx, cz, groundY, biome, heightMap, riverMap, regionSizeX)) {
            continue;
        }

        // Check Poisson disk distance against all placed trees in the region
        bool tooClose = false;
        for (const auto& p : placedList) {
            float dx = static_cast<float>(cx - p.x);
            float dz = static_cast<float>(cz - p.z);
            float dist = std::sqrt(dx * dx + dz * dz);

            size_t pIdx = static_cast<size_t>(p.z) * static_cast<size_t>(regionSizeX) + static_cast<size_t>(p.x);
            float minDist = std::min(localR, biomeMinRadius(biomeMap[pIdx]));
            if (dist < minDist) {
                tooClose = true;
                break;
            }
        }
        if (tooClose) continue;

        // Choose tree type deterministically
        uint64_t posSeed = rngState;
        TreeType type = chooseTreeType(biome, posSeed, cx, cz);
        if (type == TreeType::NONE) continue;

        // Determine trunk height
        int trunkH = 4;
        uint64_t posHash = static_cast<uint64_t>(cx) * 7919ULL
                         + static_cast<uint64_t>(cz) * 6287ULL + rngState;
        switch (type) {
            case TreeType::SPRUCE:    trunkH = 6 + static_cast<int>(posHash % 5);  break;
            case TreeType::JUNGLE:    trunkH = 8 + static_cast<int>(posHash % 5);  break;
            case TreeType::BIRCH:     trunkH = 5 + static_cast<int>(posHash % 3);  break;
            case TreeType::ACACIA:    trunkH = 5 + static_cast<int>(posHash % 3);  break;
            case TreeType::SWAMP_OAK: trunkH = 4 + static_cast<int>(posHash % 3);  break;
            case TreeType::CACTUS:    trunkH = 1 + static_cast<int>(posHash % 3);  break;
            default:                  trunkH = 4 + static_cast<int>(posHash % 3);  break;
        }

        placedGrid[idx] = true;
        placedList.push_back({cx, cz});
        placementsOut.push_back({cx, cz, groundY, trunkH, type});
    }
}

// ── Region-wide placement validation (flat arrays) ──────────────────────

bool TreeGenerator::isValidPlacementRegion(
    int lx, int lz, int groundY, Biome biome,
    const int*     heightMap,
    const uint8_t* riverMap,
    int regionSizeX) const
{
    if (getBiomeProps(biome).treeDensity <= 0.0f) return false;

    size_t idx = static_cast<size_t>(lz) * static_cast<size_t>(regionSizeX) + static_cast<size_t>(lx);
    if (riverMap[idx]) return false;
    if (groundY <= Config::SEA_LEVEL) return false;

    // Check slope: sample 3×3 neighborhood
    int centerH = groundY;
    int maxHDiff = 0;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            int nx = lx + dx;
            int nz = lz + dz;
            if (nx < 0 || nx >= regionSizeX || nz < 0 || nz >= regionSizeX) continue;
            size_t nidx = static_cast<size_t>(nz) * static_cast<size_t>(regionSizeX) + static_cast<size_t>(nx);
            int diff = std::abs(heightMap[nidx] - centerH);
            maxHDiff = std::max(maxHDiff, diff);
        }
    }
    if (maxHDiff > 2) return false;

    return true;
}
