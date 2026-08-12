#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

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

constexpr int BIOME_COUNT = static_cast<int>(Biome::COUNT);

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
    if (name == "plains") return Biome::PLAINS;
    for (int i = 0; i < BIOME_COUNT; ++i) {
        const Biome biome = static_cast<Biome>(i);
        if (name == biomeCommandName(biome)) return biome;
    }
    return std::nullopt;
}
