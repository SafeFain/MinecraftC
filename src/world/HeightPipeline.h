#pragma once

#include "world/BiomeMap.h"
#include "world/RegionGenerationData.h"
#include "world/WorldGenContext.h"
#include "FastNoiseLite.h"

#include <cstdint>
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
    int waterLevel = 0;
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

    static float clamp01(float value);
    static float smoothstep(float edge0, float edge1, float value);
    static float peaksAndValleys(float weirdness);
    static float spline(float value, const float* xs, const float* ys, int count);
    Biome selectBiome(const ClimateSample& climate, int height,
                      bool river, bool coast, bool deepOcean) const;
};
