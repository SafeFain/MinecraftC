#include "world/HeightPipeline.h"
#include "world/Noise.h"
#include "Config.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr uint64_t DOMAIN_WARP    = 0x574152505F585A31ULL;
constexpr uint64_t DOMAIN_CONT    = 0x434F4E54494E5431ULL;
constexpr uint64_t DOMAIN_EROSION = 0x45524F53494F4E31ULL;
constexpr uint64_t DOMAIN_WEIRD   = 0x57454952444E4553ULL;
constexpr uint64_t DOMAIN_TEMP    = 0x54454D5045524154ULL;
constexpr uint64_t DOMAIN_HUMID   = 0x48554D4944495459ULL;
constexpr uint64_t DOMAIN_DETAIL  = 0x44455441494C3031ULL;

void configureFbm(FastNoiseLite& noise, int seed, float frequency, int octaves) {
    noise.SetSeed(seed);
    noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
    noise.SetFrequency(frequency);
    noise.SetFractalType(FastNoiseLite::FractalType_FBm);
    noise.SetFractalOctaves(octaves);
    noise.SetFractalLacunarity(2.0f);
    noise.SetFractalGain(0.5f);
}
}

HeightPipeline::HeightPipeline(const Noise&, uint64_t seed)
    : m_context(seed)
{
    m_warp.SetSeed(m_context.noiseSeed(DOMAIN_WARP));
    m_warp.SetDomainWarpType(FastNoiseLite::DomainWarpType_OpenSimplex2Reduced);
    m_warp.SetDomainWarpAmp(120.0f);
    m_warp.SetFrequency(0.00075f);
    m_warp.SetFractalType(FastNoiseLite::FractalType_DomainWarpProgressive);
    m_warp.SetFractalOctaves(3);

    configureFbm(m_continental, m_context.noiseSeed(DOMAIN_CONT), 0.00042f, 4);
    configureFbm(m_erosion, m_context.noiseSeed(DOMAIN_EROSION), 0.00105f, 4);
    configureFbm(m_weirdness, m_context.noiseSeed(DOMAIN_WEIRD), 0.00175f, 3);
    configureFbm(m_temperature, m_context.noiseSeed(DOMAIN_TEMP), 0.00065f, 3);
    configureFbm(m_humidity, m_context.noiseSeed(DOMAIN_HUMID), 0.00072f, 3);

    m_detail.SetSeed(m_context.noiseSeed(DOMAIN_DETAIL));
    m_detail.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
    m_detail.SetFrequency(0.009f);
    m_detail.SetFractalType(FastNoiseLite::FractalType_Ridged);
    m_detail.SetFractalOctaves(3);
    m_detail.SetFractalGain(0.5f);
}

float HeightPipeline::clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

float HeightPipeline::smoothstep(float edge0, float edge1, float value) {
    float t = clamp01((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

float HeightPipeline::peaksAndValleys(float weirdness) {
    // The same triangular fold used by modern Minecraft terrain routing:
    // -1 forms valleys, +1 forms peak bands.
    return -(std::abs(std::abs(weirdness) - 2.0f / 3.0f) - 1.0f / 3.0f) * 3.0f;
}

float HeightPipeline::spline(float value, const float* xs, const float* ys, int count) {
    if (value <= xs[0]) return ys[0];
    if (value >= xs[count - 1]) return ys[count - 1];
    int upper = 1;
    while (upper < count && value > xs[upper]) ++upper;
    int lower = upper - 1;
    float t = (value - xs[lower]) / (xs[upper] - xs[lower]);
    t = t * t * (3.0f - 2.0f * t);
    return ys[lower] + (ys[upper] - ys[lower]) * t;
}

SurfaceColumn HeightPipeline::sampleColumn(int worldX, int worldZ) const {
    float x = static_cast<float>(worldX);
    float z = static_cast<float>(worldZ);
    m_warp.DomainWarp(x, z);

    ClimateSample climate;
    climate.continentalness = m_continental.GetNoise(x, z);
    climate.erosion = m_erosion.GetNoise(x + 19000.0f, z - 7000.0f);
    climate.weirdness = m_weirdness.GetNoise(x - 11000.0f, z + 23000.0f);
    climate.peaksValleys = peaksAndValleys(climate.weirdness);
    climate.temperature = m_temperature.GetNoise(x + 31000.0f, z + 17000.0f);
    climate.humidity = m_humidity.GetNoise(x - 29000.0f, z - 13000.0f);

    static constexpr float CONT_X[] = {-1.0f, -0.55f, -0.30f, -0.16f, -0.08f, 0.20f, 0.55f, 1.0f};
    static constexpr float CONT_Y[] = {15.0f, 20.0f, 29.0f, 37.0f, 41.0f, 48.0f, 57.0f, 64.0f};
    float base = spline(climate.continentalness, CONT_X, CONT_Y, 8);

    float inland = smoothstep(-0.12f, 0.32f, climate.continentalness);
    float lowErosion = 1.0f - smoothstep(-0.45f, 0.65f, climate.erosion);
    float peakBand = smoothstep(0.05f, 0.78f, climate.peaksValleys);
    float mountain = inland * lowErosion * peakBand;
    float rolling = inland * (climate.peaksValleys * 5.0f +
                              (0.25f - climate.erosion) * 4.0f);
    float detail = (m_detail.GetNoise(x, z) * 2.0f - 0.5f) * (1.5f + inland * 1.5f);

    float height = base + rolling + mountain * 42.0f + detail;

    // Valleys in inland terrain become continuous rivers. Smooth carving is a
    // point function, so it cannot form region/chunk seams.
    float valley = smoothstep(-0.58f, -0.88f, climate.peaksValleys);
    float riverClimate = 1.0f - smoothstep(0.50f, 0.82f, climate.erosion);
    float riverWeight = valley * inland * riverClimate;
    bool river = riverWeight > 0.56f && climate.continentalness > -0.13f;
    if (riverWeight > 0.0f) {
        float riverFloor = static_cast<float>(Config::SEA_LEVEL - 2);
        height = height + (riverFloor - height) * smoothstep(0.35f, 0.92f, riverWeight);
    }

    int finalHeight = static_cast<int>(std::round(std::max(
        1.0f, std::min(static_cast<float>(Config::TERRAIN_MAX_HEIGHT), height))));
    bool deepOcean = climate.continentalness < -0.46f;
    bool coast = !river && climate.continentalness >= -0.20f &&
                 climate.continentalness < -0.07f;

    SurfaceColumn result;
    result.height = finalHeight;
    result.waterLevel = deepOcean ? Config::SEA_LEVEL + 3 : Config::SEA_LEVEL;
    result.river = river;
    result.climate = climate;
    result.biome = selectBiome(climate, finalHeight, river, coast, deepOcean);
    return result;
}

Biome HeightPipeline::selectBiome(const ClimateSample& c, int height,
                                  bool river, bool coast, bool deepOcean) const {
    if (deepOcean) return Biome::DEEP_OCEAN;
    if (c.continentalness < -0.20f) return Biome::OCEAN;
    if (river) return Biome::RIVER;
    if (coast) {
        return (c.erosion < -0.18f || height > Config::SEA_LEVEL + 5)
            ? Biome::STONY_SHORE : Biome::BEACH;
    }

    if (height >= 88 || (height >= 73 && c.erosion < -0.30f))
        return Biome::MOUNTAINS;
    if (height >= 64 && c.peaksValleys > 0.20f)
        return c.humidity > 0.35f ? Biome::MEADOW : Biome::HILLS;

    if (c.temperature > 0.45f && c.humidity < -0.28f) {
        return c.erosion < -0.18f ? Biome::BADLANDS : Biome::DESERT;
    }
    if (c.temperature > 0.35f) {
        if (c.humidity > 0.38f) return Biome::JUNGLE;
        if (c.humidity < -0.10f) return Biome::SAVANNA;
    }
    if (c.temperature < -0.42f)
        return Biome::SNOW_TUNDRA;
    if (c.temperature < -0.18f)
        return Biome::TAIGA;
    if (height <= Config::SEA_LEVEL + 2 && c.humidity > 0.35f)
        return Biome::SWAMP;
    if (c.humidity > 0.32f)
        return c.weirdness > 0.18f ? Biome::BIRCH_FOREST : Biome::FOREST;
    return Biome::PLAINS;
}

void HeightPipeline::computePaddedRegion(
    int worldOriginX, int worldOriginZ, int regionSizeX, int regionSizeZ,
    int padding, RegionGenerationData::ColumnInfo* columnsOut)
{
    const int width = regionSizeX + padding * 2;
    const int depth = regionSizeZ + padding * 2;
    for (int lz = 0; lz < depth; ++lz) {
        for (int lx = 0; lx < width; ++lx) {
            SurfaceColumn sample = sampleColumn(
                worldOriginX + lx - padding, worldOriginZ + lz - padding);
            auto& out = columnsOut[static_cast<size_t>(lz) * width + lx];
            out.height = sample.height;
            out.biome = sample.biome;
            out.isRiver = sample.river;
            out.waterLevel = sample.waterLevel;
        }
    }
}

void HeightPipeline::computeChunkFromRegion(
    const RegionGenerationData& region, int chunkWorldX, int chunkWorldZ,
    int heightOut[16][16], Biome biomeOut[16][16], bool riverOut[16][16])
{
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            int lx = region.padding + chunkWorldX - region.worldOriginX + x;
            int lz = region.padding + chunkWorldZ - region.worldOriginZ + z;
            const auto& col = region.col(lx, lz);
            heightOut[x][z] = col.height;
            biomeOut[x][z] = col.biome;
            riverOut[x][z] = col.isRiver;
        }
    }
}

void HeightPipeline::compute(
    int chunkWorldX, int chunkWorldZ, int heightOut[16][16],
    Biome biomeOut[16][16], bool riverOut[16][16], const NeighborQuery&)
{
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            SurfaceColumn sample = sampleColumn(chunkWorldX + x, chunkWorldZ + z);
            heightOut[x][z] = sample.height;
            biomeOut[x][z] = sample.biome;
            riverOut[x][z] = sample.river;
        }
    }
}

float HeightPipeline::queryHeight(float worldX, float worldZ) const {
    return static_cast<float>(sampleColumn(
        static_cast<int>(std::floor(worldX)),
        static_cast<int>(std::floor(worldZ))).height);
}

HeightBiome HeightPipeline::queryHeightBiome(float worldX, float worldZ) const {
    SurfaceColumn sample = sampleColumn(
        static_cast<int>(std::floor(worldX)),
        static_cast<int>(std::floor(worldZ)));
    return {sample.height, sample.biome};
}
