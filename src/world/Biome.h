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
    GLACIAL_PEAKS,
    ALPINE_TUNDRA,
    ROCKY_STEPPE,
    LIMESTONE_HIGHLANDS,
    KARST_FOREST,
    VOLCANIC_HIGHLANDS,
    BLACK_SAND_COAST,
    RED_CANYON,
    LUSH_VALLEY,
    DRY_WOODLAND,
    COUNT
};

constexpr int BIOME_COUNT = static_cast<int>(Biome::COUNT);

std::string_view biomeCommandName(Biome biome);
std::optional<Biome> parseBiomeCommandName(std::string_view name);
