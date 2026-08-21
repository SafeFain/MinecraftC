#include "world/BiomeMap.h"

// ── Biome property table ────────────────────────────────────────────────
// Indexed by Biome enum value

const BiomeDefinition BIOME_TABLE[BIOME_COUNT] = {
    // OCEAN — deep water, sandy seafloor
    {
        BlockId::SAND, BlockId::SAND,
        1.0f, -5.0f,
        0.0f, TreeType::NONE, TreeType::NONE,
        63, 999, BiomePrecipitation::Rain, "ocean", 0, 5.5f, 1, 4
    },
    // BEACH — flat sandy coastal strip (heights preserved as-is)
    {
        BlockId::SAND, BlockId::SAND,
        1.0f, 0.0f,
        0.0f, TreeType::NONE, TreeType::NONE,
        63, 999, BiomePrecipitation::Rain, "beach", 0, 5.5f, 1, 4
    },
    // PLAINS — gentle grassland, slight elevation boost
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 2.0f,
        0.02f, TreeType::OAK, TreeType::NONE,
        63, 999, BiomePrecipitation::Rain, "plain", 18, 5.5f, 1, 4
    },
    // FOREST — rolling woodland, moderate elevation
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 4.0f,
        0.15f, TreeType::OAK, TreeType::BIRCH,
        63, 999, BiomePrecipitation::Rain, "forest", 14, 4.0f, 1, 4
    },
    // DESERT — flat arid expanse
    {
        BlockId::SAND, BlockId::SAND,
        1.0f, 0.0f,
        0.01f, TreeType::CACTUS, TreeType::NONE,
        63, 999, BiomePrecipitation::None, "desert", 0, 5.5f, 1, 4
    },
    // MOUNTAINS — tall stone peaks, amplified height + snow caps.
    {
        BlockId::STONE, BlockId::STONE,
        1.08f, 7.0f,
        0.02f, TreeType::SPRUCE, TreeType::NONE,
        63, 140, BiomePrecipitation::Rain, "mountains", 0, 5.5f, 1, 4
    },
    // HILLS — rolling terrain, noticeable elevation
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 6.0f,
        0.05f, TreeType::OAK, TreeType::NONE,
        63, 999, BiomePrecipitation::Rain, "hills", 0, 5.0f, 1, 4
    },
    // SWAMP — low-lying wet ground (often flooded)
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, -3.0f,
        0.08f, TreeType::SWAMP_OAK, TreeType::NONE,
        63, 999, BiomePrecipitation::Rain, "swamp", 0, 4.5f, 1, 4
    },
    // TAIGA — cool conifer forest, moderate elevation
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 3.0f,
        0.10f, TreeType::SPRUCE, TreeType::NONE,
        63, 128, BiomePrecipitation::Rain, "taiga", 6, 4.0f, 1, 4
    },
    // SNOW_TUNDRA — cold flat treeless plain
    {
        BlockId::SNOW, BlockId::DIRT,
        1.0f, 1.0f,
        0.0f, TreeType::NONE, TreeType::NONE,
        63, 76, BiomePrecipitation::Snow, "snow_tundra", 0, 5.5f, 1, 4
    },
    // JUNGLE — dense tropical forest, elevated canopy
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 5.0f,
        0.25f, TreeType::JUNGLE, TreeType::OAK,
        63, 999, BiomePrecipitation::Rain, "jungle", 22, 3.0f, 1, 4
    },
    // SAVANNA — flat arid grassland
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 1.0f,
        0.03f, TreeType::ACACIA, TreeType::NONE,
        63, 999, BiomePrecipitation::None, "savanna", 8, 5.5f, 1, 4
    },
    // DEEP_OCEAN
    {
        BlockId::GRAVEL, BlockId::STONE,
        1.0f, 0.0f,
        0.0f, TreeType::NONE, TreeType::NONE,
        66, 999, BiomePrecipitation::Rain, "deep_ocean", 0, 5.5f, 1, 4
    },
    // RIVER
    {
        BlockId::GRAVEL, BlockId::CLAY,
        1.0f, 0.0f,
        0.0f, TreeType::NONE, TreeType::NONE,
        63, 999, BiomePrecipitation::Rain, "river", 0, 5.5f, 1, 4
    },
    // STONY_SHORE
    {
        BlockId::STONE, BlockId::GRAVEL,
        1.0f, 0.0f,
        0.0f, TreeType::NONE, TreeType::NONE,
        63, 999, BiomePrecipitation::Rain, "stony_shore", 0, 5.5f, 1, 4
    },
    // MEADOW
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 0.0f,
        0.015f, TreeType::OAK, TreeType::NONE,
        63, 145, BiomePrecipitation::Rain, "meadow", 45, 5.0f, 1, 4
    },
    // BIRCH_FOREST
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 0.0f,
        0.16f, TreeType::BIRCH, TreeType::OAK,
        63, 999, BiomePrecipitation::Rain, "birch_forest", 14, 4.0f, 1, 4
    },
    // BADLANDS
    {
        BlockId::RED_SAND, BlockId::TERRACOTTA,
        1.0f, 0.0f,
        0.005f, TreeType::CACTUS, TreeType::NONE,
        63, 999, BiomePrecipitation::None, "badlands", 0, 5.5f, 1, 4
    },
    // FLOWER_FOREST
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 3.0f,
        0.13f, TreeType::OAK, TreeType::BIRCH,
        63, 999, BiomePrecipitation::Rain, "flower_forest", 29, 4.0f, 1, 4
    },
    // SUNFLOWER_PLAINS
    {
        BlockId::GRASS, BlockId::DIRT,
        1.0f, 2.0f,
        0.015f, TreeType::OAK, TreeType::NONE,
        63, 999, BiomePrecipitation::Rain, "sunflower_plains", 22, 5.5f, 1, 4
    },
    // GLACIAL_PEAKS
    {BlockId::PACKED_ICE, BlockId::STONE, 1.0f, 0.0f,
     0.0f, TreeType::NONE, TreeType::NONE, 63, 64,
     BiomePrecipitation::Snow, "glacial_peaks", 0, 5.5f, 1, 4},
    // ALPINE_TUNDRA
    {BlockId::SNOW, BlockId::COARSE_DIRT, 1.0f, 0.0f,
     0.01f, TreeType::SPRUCE, TreeType::NONE, 63, 96,
     BiomePrecipitation::Snow, "alpine_tundra", 3, 5.5f, 1, 4},
    // ROCKY_STEPPE
    {BlockId::COARSE_DIRT, BlockId::DIRT, 1.0f, 0.0f,
     0.02f, TreeType::ACACIA, TreeType::NONE, 63, 999,
     BiomePrecipitation::Rain, "rocky_steppe", 0, 5.5f, 1, 4},
    // LIMESTONE_HIGHLANDS
    {BlockId::LIMESTONE, BlockId::LIMESTONE, 1.0f, 0.0f,
     0.01f, TreeType::NONE, TreeType::NONE, 63, 999,
     BiomePrecipitation::Rain, "limestone_highlands", 0, 5.5f, 1, 4},
    // KARST_FOREST
    {BlockId::MOSS, BlockId::LIMESTONE, 1.0f, 0.0f,
     0.12f, TreeType::JUNGLE, TreeType::OAK, 63, 999,
     BiomePrecipitation::Rain, "karst_forest", 18, 4.0f, 1, 4},
    // VOLCANIC_HIGHLANDS
    {BlockId::TUFF, BlockId::TUFF, 1.0f, 0.0f,
     0.0f, TreeType::NONE, TreeType::NONE, 63, 999,
     BiomePrecipitation::None, "volcanic_highlands", 0, 5.5f, 1, 4},
    // BLACK_SAND_COAST
    {BlockId::BLACK_SAND, BlockId::BASALT, 1.0f, 0.0f,
     0.0f, TreeType::NONE, TreeType::NONE, 63, 999,
     BiomePrecipitation::None, "black_sand_coast", 0, 5.5f, 1, 4},
    // RED_CANYON
    {BlockId::RED_SAND, BlockId::TERRACOTTA, 1.0f, 0.0f,
     0.01f, TreeType::CACTUS, TreeType::NONE, 63, 999,
     BiomePrecipitation::None, "red_canyon", 0, 5.5f, 1, 4},
    // LUSH_VALLEY
    {BlockId::MOSS, BlockId::DIRT, 1.0f, 0.0f,
     0.16f, TreeType::OAK, TreeType::BIRCH, 63, 999,
     BiomePrecipitation::Rain, "lush_valley", 31, 4.0f, 1, 4},
    // DRY_WOODLAND
    {BlockId::COARSE_DIRT, BlockId::DIRT, 1.0f, 0.0f,
     0.08f, TreeType::ACACIA, TreeType::OAK, 63, 999,
     BiomePrecipitation::None, "dry_woodland", 7, 5.0f, 1, 4},
};

std::string_view biomeCommandName(Biome biome) {
    return getBiomeProps(biome).commandName;
}

std::optional<Biome> parseBiomeCommandName(std::string_view name) {
    if (name == "plains") return Biome::PLAINS;
    for (int i = 0; i < BIOME_COUNT; ++i) {
        const Biome biome = static_cast<Biome>(i);
        if (name == biomeCommandName(biome)) return biome;
    }
    return std::nullopt;
}
