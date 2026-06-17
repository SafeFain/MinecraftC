#pragma once

#include <cstdint>

// ── Biome enum ──────────────────────────────────────────────────────────

enum class Biome : uint8_t {
    OCEAN = 0,
    BEACH,
    PLAINS,
    FOREST,
    DESERT,
    MOUNTAINS,
    HILLS,
    SWAMP,
    TAIGA,
    SNOW_TUNDRA,
    JUNGLE,
    SAVANNA,
    COUNT
};

// Number of biomes
constexpr int BIOME_COUNT = static_cast<int>(Biome::COUNT);

// ── Tree type (for vegetation generation) ───────────────────────────────

enum class TreeType : uint8_t {
    NONE,
    OAK,
    BIRCH,
    SPRUCE,
    JUNGLE,
    ACACIA,
    SWAMP_OAK,
    CACTUS
};

// ── Biome properties ────────────────────────────────────────────────────

#include "world/Block.h"

struct BiomeProperties {
    BlockId   surfaceBlock  = BlockId::GRASS;
    BlockId   subsoilBlock  = BlockId::DIRT;
    float     heightMul     = 0.5f;    // terrain amplitude multiplier
    float     heightOffset  = 5.0f;    // additive elevation (blocks)
    float     treeDensity   = 0.05f;   // 0=no trees, higher=denser
    TreeType  treeType1     = TreeType::OAK;
    TreeType  treeType2     = TreeType::NONE;
    int       waterLevel    = 40;      // sea-level override per biome (for swamps etc.)
    int       snowLine       = 999;    // height above which surface becomes SNOW (999=never)
};

// Global biome property table
extern const BiomeProperties BIOME_TABLE[BIOME_COUNT];

inline const BiomeProperties& getBiomeProps(Biome b) {
    return BIOME_TABLE[static_cast<uint8_t>(b)];
}
