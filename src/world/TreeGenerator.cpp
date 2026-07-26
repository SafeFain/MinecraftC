#include "world/TreeGenerator.h"
#include "Config.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr int CELL_SIZE = 5;
constexpr uint64_t TREE_DOMAIN = 0x545245455F475249ULL;
constexpr uint64_t TYPE_DOMAIN = 0x545245455F545950ULL;
}

TreeGenerator::TreeGenerator(uint64_t seed)
    : m_context(seed), m_treeSeed(m_context.derive(TREE_DOMAIN)) {}

int TreeGenerator::floorDiv(int value, int divisor) {
    int quotient = value / divisor;
    int remainder = value % divisor;
    if (remainder != 0 && ((remainder < 0) != (divisor < 0))) --quotient;
    return quotient;
}

TreeGenerator::Candidate TreeGenerator::candidateForCell(int cellX, int cellZ) const {
    uint64_t h = WorldGenContext::hashPosition(m_treeSeed, cellX, 0, cellZ);
    Candidate result;
    result.x = cellX * CELL_SIZE + 1 + static_cast<int>(h % (CELL_SIZE - 2));
    result.z = cellZ * CELL_SIZE + 1 +
               static_cast<int>((h >> 16) % (CELL_SIZE - 2));
    result.priority = WorldGenContext::mix(h ^ 0xA0761D6478BD642FULL);
    return result;
}

float TreeGenerator::radiusFor(Biome biome) {
    switch (biome) {
        case Biome::JUNGLE:       return 3.0f;
        case Biome::FOREST:
        case Biome::BIRCH_FOREST:
        case Biome::TAIGA:        return 4.0f;
        case Biome::SWAMP:        return 4.5f;
        case Biome::HILLS:
        case Biome::MEADOW:       return 5.0f;
        default:                  return 5.5f;
    }
}

float TreeGenerator::chanceFor(Biome biome) {
    switch (biome) {
        case Biome::JUNGLE:       return 0.95f;
        case Biome::FOREST:       return 0.78f;
        case Biome::BIRCH_FOREST: return 0.82f;
        case Biome::TAIGA:        return 0.68f;
        case Biome::SWAMP:        return 0.52f;
        case Biome::HILLS:        return 0.24f;
        case Biome::MEADOW:       return 0.12f;
        case Biome::PLAINS:       return 0.10f;
        case Biome::SAVANNA:      return 0.18f;
        case Biome::MOUNTAINS:    return 0.08f;
        case Biome::DESERT:
        case Biome::BADLANDS:     return 0.12f;
        default:                  return 0.0f;
    }
}

bool TreeGenerator::winsSpacing(const Candidate& candidate, float radius) const {
    int cellX = floorDiv(candidate.x, CELL_SIZE);
    int cellZ = floorDiv(candidate.z, CELL_SIZE);
    int range = static_cast<int>(std::ceil(radius / CELL_SIZE)) + 1;
    float radius2 = radius * radius;
    for (int dz = -range; dz <= range; ++dz) {
        for (int dx = -range; dx <= range; ++dx) {
            if (dx == 0 && dz == 0) continue;
            Candidate other = candidateForCell(cellX + dx, cellZ + dz);
            float ox = static_cast<float>(other.x - candidate.x);
            float oz = static_cast<float>(other.z - candidate.z);
            if (ox * ox + oz * oz < radius2 &&
                other.priority > candidate.priority) {
                return false;
            }
        }
    }
    return true;
}

bool TreeGenerator::accepts(Biome biome, int height, bool river,
                            int worldX, int worldZ) const {
    float chance = chanceFor(biome);
    if (chance <= 0.0f || river || height <= Config::SEA_LEVEL ||
        height >= Config::CHUNK_SIZE_Y - 14) {
        return false;
    }
    uint64_t h = WorldGenContext::hashPosition(
        m_context.derive(0x545245455F414343ULL), worldX, height, worldZ);
    float roll = static_cast<float>(h & 0xFFFFFFULL) /
                 static_cast<float>(0x1000000ULL);
    return roll < chance;
}

TreeType TreeGenerator::chooseTreeType(Biome biome, uint64_t seed, int x, int z) {
    const BiomeProperties& props = getBiomeProps(biome);
    if (props.treeType1 == TreeType::NONE) return TreeType::NONE;
    uint64_t h = WorldGenContext::hashPosition(seed ^ TYPE_DOMAIN, x, 0, z);
    if (props.treeType2 != TreeType::NONE && h % 4 == 0)
        return props.treeType2;
    return props.treeType1;
}

int TreeGenerator::trunkHeight(TreeType type, int worldX, int worldZ) const {
    uint64_t h = WorldGenContext::hashPosition(
        m_context.derive(0x545245455F53495AULL), worldX, 0, worldZ);
    switch (type) {
        case TreeType::SPRUCE:    return 6 + static_cast<int>(h % 5);
        case TreeType::JUNGLE:    return 8 + static_cast<int>(h % 5);
        case TreeType::BIRCH:     return 5 + static_cast<int>(h % 3);
        case TreeType::ACACIA:    return 5 + static_cast<int>(h % 3);
        case TreeType::SWAMP_OAK: return 4 + static_cast<int>(h % 3);
        case TreeType::CACTUS:    return 1 + static_cast<int>(h % 3);
        default:                  return 4 + static_cast<int>(h % 3);
    }
}

void TreeGenerator::generateTreesRegion(
    int originX, int originZ, int width, int depth,
    const int* heights, const Biome* biomes, const uint8_t* rivers,
    int, std::vector<RegionGenerationData::TreePlacement>& output)
{
    output.clear();
    int minCellX = floorDiv(originX - CELL_SIZE, CELL_SIZE);
    int maxCellX = floorDiv(originX + width + CELL_SIZE, CELL_SIZE);
    int minCellZ = floorDiv(originZ - CELL_SIZE, CELL_SIZE);
    int maxCellZ = floorDiv(originZ + depth + CELL_SIZE, CELL_SIZE);

    for (int cellZ = minCellZ; cellZ <= maxCellZ; ++cellZ) {
        for (int cellX = minCellX; cellX <= maxCellX; ++cellX) {
            Candidate c = candidateForCell(cellX, cellZ);
            if (c.x < originX || c.x >= originX + width ||
                c.z < originZ || c.z >= originZ + depth) continue;
            int lx = c.x - originX;
            int lz = c.z - originZ;
            size_t index = static_cast<size_t>(lz) * width + lx;
            Biome biome = biomes[index];
            int height = heights[index];
            if (!accepts(biome, height, rivers[index] != 0, c.x, c.z) ||
                !winsSpacing(c, radiusFor(biome))) continue;
            TreeType type = chooseTreeType(biome, m_treeSeed, c.x, c.z);
            if (type == TreeType::NONE) continue;
            output.push_back({lx, lz, height, trunkHeight(type, c.x, c.z), type});
        }
    }
}

std::vector<TreeGenerator::TreePlacement> TreeGenerator::generateTrees(
    int originX, int originZ, const int heights[16][16],
    const Biome biomes[16][16], const bool rivers[16][16])
{
    std::vector<TreePlacement> output;
    int minCellX = floorDiv(originX - CELL_SIZE, CELL_SIZE);
    int maxCellX = floorDiv(originX + 16 + CELL_SIZE, CELL_SIZE);
    int minCellZ = floorDiv(originZ - CELL_SIZE, CELL_SIZE);
    int maxCellZ = floorDiv(originZ + 16 + CELL_SIZE, CELL_SIZE);
    for (int cellZ = minCellZ; cellZ <= maxCellZ; ++cellZ) {
        for (int cellX = minCellX; cellX <= maxCellX; ++cellX) {
            Candidate c = candidateForCell(cellX, cellZ);
            if (c.x < originX || c.x >= originX + 16 ||
                c.z < originZ || c.z >= originZ + 16) continue;
            int lx = c.x - originX;
            int lz = c.z - originZ;
            Biome biome = biomes[lx][lz];
            int height = heights[lx][lz];
            if (!accepts(biome, height, rivers[lx][lz], c.x, c.z) ||
                !winsSpacing(c, radiusFor(biome))) continue;
            TreeType type = chooseTreeType(biome, m_treeSeed, c.x, c.z);
            if (type != TreeType::NONE)
                output.push_back({lx, lz, height, trunkHeight(type, c.x, c.z), type});
        }
    }
    return output;
}
