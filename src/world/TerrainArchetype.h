#pragma once

#include <cstdint>

enum class TerrainArchetype : uint8_t {
    DEEP_OCEAN_TRENCH,
    ISLAND_ARC,
    COASTAL_CLIFFS,
    ROLLING_LOWLANDS,
    BROAD_RIVER_VALLEY,
    WETLAND_BASIN,
    WOODED_HILLS,
    HIGH_PLATEAU,
    DUNE_SEA,
    RED_ROCK_CANYON,
    KARST_TOWERS,
    ALPINE_RANGE,
    GLACIAL_RANGE,
    VOLCANIC_CALDERA,
    WINDSWEPT_RIDGES,
    COUNT
};

constexpr int TERRAIN_ARCHETYPE_COUNT =
    static_cast<int>(TerrainArchetype::COUNT);

struct BasinInfo {
    float channelDistance = 1000000.0f;
    float channelWidth = 0.0f;
    float valleyWidth = 0.0f;
    float channelWeight = 0.0f;
    float lakeWeight = 0.0f;
    int channelBedY = 0;
    int channelWaterY = 0;
    uint16_t upstreamSize = 0;
    bool inlandLake = false;
};

const char* terrainArchetypeName(TerrainArchetype archetype);
