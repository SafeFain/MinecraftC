#include "world/BiomeMap.h"

// ── Biome property table ────────────────────────────────────────────────
// Indexed by Biome enum value

const BiomeProperties BIOME_TABLE[BIOME_COUNT] = {
    // OCEAN — deep water, sandy seafloor
    // heightMul=1.0 preserves baseHeightRaw output; offset=-5 for deeper floor
    {
        BlockId::SAND, BlockId::SAND,
        1.0f, -5.0f,
        0.0f, TreeType::NONE, TreeType::NONE,
        63, 999
    },
    // BEACH — flat sandy coastal strip (heights preserved as-is)
    {
        BlockId::SAND, BlockId::SAND,
        1.0f, 0.0f,
        0.0f, TreeType::NONE, TreeType::NONE,
        63, 999
    },
    // PLAINS — gentle grassland, slight elevation boost
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 2.0f,
        0.02f, TreeType::OAK, TreeType::NONE,
        63, 999
    },
    // FOREST — rolling woodland, moderate elevation
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 4.0f,
        0.15f, TreeType::OAK, TreeType::BIRCH,
        63, 999
    },
    // DESERT — flat arid expanse
    {
        BlockId::SAND, BlockId::SAND,
        1.0f, 0.0f,
        0.01f, TreeType::CACTUS, TreeType::NONE,
        63, 999
    },
    // MOUNTAINS — tall stone peaks, amplified height + snow caps.
    // HeightMul/Offset toned down to reduce boundary cliffs vs neighbors.
    {
        BlockId::STONE, BlockId::STONE,
        1.08f, 7.0f,
        0.02f, TreeType::SPRUCE, TreeType::NONE,
        63, 140
    },
    // HILLS — rolling terrain, noticeable elevation
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 6.0f,
        0.05f, TreeType::OAK, TreeType::NONE,
        63, 999
    },
    // SWAMP — low-lying wet ground (negative offset = often flooded)
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, -3.0f,
        0.08f, TreeType::SWAMP_OAK, TreeType::NONE,
        63, 999
    },
    // TAIGA — cool conifer forest, moderate elevation
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 3.0f,
        0.10f, TreeType::SPRUCE, TreeType::NONE,
        63, 128
    },
    // SNOW_TUNDRA — cold flat treeless plain
    {
        BlockId::SNOW, BlockId::DIRT,
        1.0f, 1.0f,
        0.0f, TreeType::NONE, TreeType::NONE,
        63, 76
    },
    // JUNGLE — dense tropical forest, elevated canopy
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 5.0f,
        0.25f, TreeType::JUNGLE, TreeType::OAK,
        63, 999
    },
    // SAVANNA — flat arid grassland
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 1.0f,
        0.03f, TreeType::ACACIA, TreeType::NONE,
        63, 999
    },
    // DEEP_OCEAN
    {
        BlockId::GRAVEL, BlockId::STONE,
        1.0f, 0.0f,
        0.0f, TreeType::NONE, TreeType::NONE,
        66, 999
    },
    // RIVER
    {
        BlockId::GRAVEL, BlockId::CLAY,
        1.0f, 0.0f,
        0.0f, TreeType::NONE, TreeType::NONE,
        63, 999
    },
    // STONY_SHORE
    {
        BlockId::STONE, BlockId::GRAVEL,
        1.0f, 0.0f,
        0.0f, TreeType::NONE, TreeType::NONE,
        63, 999
    },
    // MEADOW
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 0.0f,
        0.015f, TreeType::OAK, TreeType::NONE,
        63, 145
    },
    // BIRCH_FOREST
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 0.0f,
        0.16f, TreeType::BIRCH, TreeType::OAK,
        63, 999
    },
    // BADLANDS
    {
        BlockId::RED_SAND, BlockId::TERRACOTTA,
        1.0f, 0.0f,
        0.005f, TreeType::CACTUS, TreeType::NONE,
        63, 999
    },
    // FLOWER_FOREST
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 3.0f,
        0.13f, TreeType::OAK, TreeType::BIRCH,
        63, 999
    },
    // SUNFLOWER_PLAINS
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 2.0f,
        0.015f, TreeType::OAK, TreeType::NONE,
        63, 999
    },
};
