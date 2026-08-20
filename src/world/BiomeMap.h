#pragma once

#include <cstdint>
#include <string_view>

#include "world/Biome.h"

// ── Biome enum ──────────────────────────────────────────────────────────

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

enum class BiomePrecipitation : uint8_t { None, Rain, Snow };

struct BiomeDefinition {
    BlockId   surfaceBlock  = BlockId::GRASS;
    BlockId   subsoilBlock  = BlockId::DIRT;
    // Height fields remain compatibility metadata; HeightPipeline owns shape.
    // Tree density is the deterministic anchor acceptance rate divided by five.
    float     heightMul     = 0.5f;
    float     heightOffset  = 5.0f;
    float     treeDensity   = 0.05f;
    TreeType  treeType1     = TreeType::OAK;
    TreeType  treeType2     = TreeType::NONE;
    int       waterLevel    = 63;      // sea-level override per biome (for swamps etc.)
    int       snowLine       = 999;    // height above which surface becomes SNOW (999=never)
    BiomePrecipitation precipitation = BiomePrecipitation::Rain;
    std::string_view commandName;
    uint8_t decorationDensity = 0;
    float treeSpacing = 5.5f;
    uint8_t secondaryTreeWeight = 1;
    uint8_t totalTreeWeight = 4;
};

using BiomeProperties = BiomeDefinition;

// Global biome property table
extern const BiomeDefinition BIOME_TABLE[BIOME_COUNT];

inline const BiomeProperties& getBiomeProps(Biome b) {
    return BIOME_TABLE[static_cast<uint8_t>(b)];
}

inline BiomePrecipitation getBiomePrecipitation(Biome biome) {
    return getBiomeProps(biome).precipitation;
}
