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
        45, 999           // higher water level for deep ocean
    },
    // BEACH — flat sandy coastal strip (heights preserved as-is)
    {
        BlockId::SAND, BlockId::SAND,
        1.0f, 0.0f,
        0.0f, TreeType::NONE, TreeType::NONE,
        40, 999
    },
    // PLAINS — gentle grassland, slight elevation boost
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 2.0f,
        0.02f, TreeType::OAK, TreeType::NONE,
        40, 999
    },
    // FOREST — rolling woodland, moderate elevation
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 4.0f,
        0.15f, TreeType::OAK, TreeType::BIRCH,
        40, 999
    },
    // DESERT — flat arid expanse
    {
        BlockId::SAND, BlockId::SAND,
        1.0f, 0.0f,
        0.01f, TreeType::CACTUS, TreeType::NONE,
        40, 999
    },
    // MOUNTAINS — tall stone peaks, amplified height + snow caps.
    // HeightMul/Offset toned down to reduce boundary cliffs vs neighbors.
    {
        BlockId::STONE, BlockId::STONE,
        1.08f, 7.0f,
        0.02f, TreeType::SPRUCE, TreeType::NONE,
        40, 75            // snow above y=75
    },
    // HILLS — rolling terrain, noticeable elevation
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 6.0f,
        0.05f, TreeType::OAK, TreeType::NONE,
        40, 999
    },
    // SWAMP — low-lying wet ground (negative offset = often flooded)
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, -3.0f,
        0.08f, TreeType::SWAMP_OAK, TreeType::NONE,
        40, 999            // unified water level; wetness from low heightOffset
    },
    // TAIGA — cool conifer forest, moderate elevation
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 3.0f,
        0.10f, TreeType::SPRUCE, TreeType::NONE,
        40, 70             // snow above y=70
    },
    // SNOW_TUNDRA — cold flat treeless plain
    {
        BlockId::SNOW, BlockId::DIRT,
        1.0f, 1.0f,
        0.0f, TreeType::NONE, TreeType::NONE,
        40, 50             // snow above y=50
    },
    // JUNGLE — dense tropical forest, elevated canopy
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 5.0f,
        0.25f, TreeType::JUNGLE, TreeType::OAK,
        40, 999
    },
    // SAVANNA — flat arid grassland
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 1.0f,
        0.03f, TreeType::ACACIA, TreeType::NONE,
        40, 999
    },
};
