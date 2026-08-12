#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

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
    DEEP_OCEAN,
    RIVER,
    STONY_SHORE,
    MEADOW,
    BIRCH_FOREST,
    BADLANDS,
    FLOWER_FOREST,
    SUNFLOWER_PLAINS,
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
    int       waterLevel    = 63;      // sea-level override per biome (for swamps etc.)
    int       snowLine       = 999;    // height above which surface becomes SNOW (999=never)
};

// Global biome property table
extern const BiomeProperties BIOME_TABLE[BIOME_COUNT];

inline const BiomeProperties& getBiomeProps(Biome b) {
    return BIOME_TABLE[static_cast<uint8_t>(b)];
}

inline constexpr std::string_view biomeCommandName(Biome biome) {
    constexpr std::string_view names[BIOME_COUNT] = {
        "ocean", "beach", "plain", "forest", "desert", "mountains",
        "hills", "swamp", "taiga", "snow_tundra", "jungle", "savanna",
        "deep_ocean", "river", "stony_shore", "meadow", "birch_forest",
        "badlands", "flower_forest", "sunflower_plains"
    };
    return names[static_cast<uint8_t>(biome)];
}

inline std::optional<Biome> parseBiomeCommandName(std::string_view name) {
    // Keep the documented singular `plain`, but accept the common plural too.
    if (name == "plains") return Biome::PLAINS;
    for (int i = 0; i < BIOME_COUNT; ++i) {
        const Biome biome = static_cast<Biome>(i);
        if (name == biomeCommandName(biome)) return biome;
    }
    return std::nullopt;
}
