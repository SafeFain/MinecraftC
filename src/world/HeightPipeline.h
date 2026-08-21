#pragma once

#include "world/BiomeMap.h"
#include "world/RegionGenerationData.h"
#include "world/WorldGenContext.h"
#include "world/TerrainArchetype.h"
#include "FastNoiseLite.h"

#include <cstdint>
#include <array>
#include <functional>
#include <optional>

class Noise;

struct HeightBiome {
    int height;
    Biome biome;
};

struct ClimateSample {
    float continentalness = 0.0f;
    float erosion = 0.0f;
    float weirdness = 0.0f;
    float peaksValleys = 0.0f;
    float temperature = 0.0f;
    float humidity = 0.0f;
};

struct SurfaceColumn {
    int height = 0;
    int nominalHeight = 0;
    int waterLevel = 0;
    float mountainFactor = 0.0f;
    float slope = 0.0f;
    int localRelief = 0;
    float riverWeight = 0.0f;
    float densityWeight = 0.0f;
    float primaryArchetypeWeight = 1.0f;
    float volcanicWeight = 0.0f;
    float craterWeight = 0.0f;
    int densityMinY = 0;
    int densityMaxY = 0;
    TerrainArchetype archetype = TerrainArchetype::ROLLING_LOWLANDS;
    TerrainArchetype secondaryArchetype = TerrainArchetype::ROLLING_LOWLANDS;
    float archetypeBlend = 0.0f;
    BasinInfo basin;
    Biome biome = Biome::OCEAN;
    bool river = false;
    ClimateSample climate;
};

using NeighborQuery = std::function<std::optional<HeightBiome>(int worldX, int worldZ)>;

// World-coordinate-only surface router inspired by Minecraft's multi-noise
// terrain model. Region and singleton generation are merely batching modes:
// sampleColumn(wx,wz) is the sole source of terrain truth.
class HeightPipeline {
public:
    HeightPipeline(const Noise& legacyNoise, uint64_t seed);

    SurfaceColumn sampleColumn(int worldX, int worldZ) const;
    bool isTerrainSolid(int worldX, int worldY, int worldZ,
                        const SurfaceColumn& column) const;

    void computePaddedRegion(int worldOriginX, int worldOriginZ,
                             int regionSizeX, int regionSizeZ, int padding,
                             RegionGenerationData::ColumnInfo* columnsOut);

    void computeChunkFromRegion(const RegionGenerationData& region,
                                int chunkWorldX, int chunkWorldZ,
                                int heightOut[16][16],
                                Biome biomeOut[16][16],
                                bool riverOut[16][16]);

    void compute(int chunkWorldX, int chunkWorldZ,
                 int heightOut[16][16],
                 Biome biomeOut[16][16],
                 bool riverOut[16][16],
                 const NeighborQuery& neighborQuery = {});

    float queryHeight(float worldX, float worldZ) const;
    HeightBiome queryHeightBiome(float worldX, float worldZ) const;

private:
    WorldGenContext m_context;
    FastNoiseLite m_warp;
    FastNoiseLite m_continental;
    FastNoiseLite m_erosion;
    FastNoiseLite m_weirdness;
    FastNoiseLite m_temperature;
    FastNoiseLite m_humidity;
    FastNoiseLite m_detail;
    FastNoiseLite m_ridges;
    FastNoiseLite m_surfaceWarp;
    FastNoiseLite m_surfaceDensity;

    static float clamp01(float value);
    static float smoothstep(float edge0, float edge1, float value);
    static float peaksAndValleys(float weirdness);
    static float spline(float value, const float* xs, const float* ys, int count);
    ClimateSample sampleClimate(float worldX, float worldZ) const;
    SurfaceColumn sampleBaseColumn(int worldX, int worldZ) const;
    SurfaceColumn sampleShapedColumn(int worldX, int worldZ) const;
    static void applySlope(SurfaceColumn& center,
                           const std::array<SurfaceColumn, 8>& neighbors);
    TerrainArchetype selectArchetype(uint64_t hash,
                                     const ClimateSample& climate) const;
    BasinInfo sampleBasin(int worldX, int worldZ) const;
    Biome selectBiome(const ClimateSample& climate, int height,
                      bool river, bool coast, bool deepOcean,
                      TerrainArchetype archetype) const;
};
