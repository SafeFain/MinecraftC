#include "world/HeightPipeline.h"
#include "world/Noise.h"
#include "Config.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr uint64_t DOMAIN_WARP    = 0x574152505F585A31ULL;
constexpr uint64_t DOMAIN_CONT    = 0x434F4E54494E5431ULL;
constexpr uint64_t DOMAIN_EROSION = 0x45524F53494F4E31ULL;
constexpr uint64_t DOMAIN_WEIRD   = 0x57454952444E4553ULL;
constexpr uint64_t DOMAIN_TEMP    = 0x54454D5045524154ULL;
constexpr uint64_t DOMAIN_HUMID   = 0x48554D4944495459ULL;
constexpr uint64_t DOMAIN_DETAIL  = 0x44455441494C3031ULL;
constexpr uint64_t DOMAIN_RIDGES  = 0x5249444745533031ULL;
constexpr uint64_t DOMAIN_BASIN   = 0x424153494E475241ULL;
constexpr uint64_t DOMAIN_SURFACE_WARP = 0x5355524657415250ULL;
constexpr uint64_t DOMAIN_SURFACE_DENSITY = 0x5355524644454E53ULL;
constexpr uint64_t DOMAIN_MACRO = 0x4D4143524F43454CULL;
constexpr int MACRO_CELL_SIZE = 896;
constexpr int BASIN_CELL_SIZE = 320;

void configureFbm(FastNoiseLite& noise, int seed, float frequency, int octaves) {
    noise.SetSeed(seed);
    noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
    noise.SetFrequency(frequency);
    noise.SetFractalType(FastNoiseLite::FractalType_FBm);
    noise.SetFractalOctaves(octaves);
    noise.SetFractalLacunarity(2.0f);
    noise.SetFractalGain(0.5f);
}

int floorDiv(int value, int divisor) {
    int quotient = value / divisor;
    int remainder = value % divisor;
    return remainder < 0 ? quotient - 1 : quotient;
}
}

const char* terrainArchetypeName(TerrainArchetype archetype) {
    static constexpr const char* names[TERRAIN_ARCHETYPE_COUNT] = {
        "deep_ocean_trench", "island_arc", "coastal_cliffs",
        "rolling_lowlands", "broad_river_valley", "wetland_basin",
        "wooded_hills", "high_plateau", "dune_sea", "red_rock_canyon",
        "karst_towers", "alpine_range", "glacial_range",
        "volcanic_caldera", "windswept_ridges"
    };
    return names[static_cast<uint8_t>(archetype)];
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
    // Climate varies faster than continentalness so inland exploration crosses
    // several biomes without fragmenting the large-scale land/ocean layout.
    configureFbm(m_temperature, m_context.noiseSeed(DOMAIN_TEMP), 0.00110f, 3);
    configureFbm(m_humidity, m_context.noiseSeed(DOMAIN_HUMID), 0.00120f, 3);

    m_detail.SetSeed(m_context.noiseSeed(DOMAIN_DETAIL));
    m_detail.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
    m_detail.SetFrequency(0.009f);
    m_detail.SetFractalType(FastNoiseLite::FractalType_Ridged);
    m_detail.SetFractalOctaves(3);
    m_detail.SetFractalGain(0.5f);

    configureFbm(m_ridges, m_context.noiseSeed(DOMAIN_RIDGES), 0.0028f, 4);
    m_ridges.SetFractalType(FastNoiseLite::FractalType_Ridged);
    m_surfaceWarp.SetSeed(m_context.noiseSeed(DOMAIN_SURFACE_WARP));
    m_surfaceWarp.SetDomainWarpType(FastNoiseLite::DomainWarpType_OpenSimplex2Reduced);
    m_surfaceWarp.SetDomainWarpAmp(12.0f);
    m_surfaceWarp.SetFrequency(0.012f);
    m_surfaceWarp.SetFractalType(FastNoiseLite::FractalType_DomainWarpProgressive);
    m_surfaceWarp.SetFractalOctaves(2);
    configureFbm(m_surfaceDensity, m_context.noiseSeed(DOMAIN_SURFACE_DENSITY), 0.021f, 3);
}

BasinInfo HeightPipeline::sampleBasin(int worldX, int worldZ) const {
    struct BasinNode {
        float x = 0.0f;
        float z = 0.0f;
        float potential = 0.0f;
        int downstream = -1;
        uint16_t accumulation = 1;
    };
    struct BasinCache {
        const HeightPipeline* owner = nullptr;
        int centerX = std::numeric_limits<int>::max();
        int centerZ = std::numeric_limits<int>::max();
        std::array<BasinNode, 49> nodes{};
    };
    thread_local BasinCache cache;

    constexpr int side = 7;
    constexpr auto indexOf = [](int x, int z) { return z * 7 + x; };
    const int centerX = floorDiv(worldX, BASIN_CELL_SIZE);
    const int centerZ = floorDiv(worldZ, BASIN_CELL_SIZE);
    if (cache.owner != this || cache.centerX != centerX ||
        cache.centerZ != centerZ) {
        cache.owner = this;
        cache.centerX = centerX;
        cache.centerZ = centerZ;
        const uint64_t basinSeed = m_context.derive(DOMAIN_BASIN);
        for (int z = 0; z < side; ++z) {
            for (int x = 0; x < side; ++x) {
                BasinNode& node = cache.nodes[indexOf(x, z)];
                const int cellX = centerX + x - 3;
                const int cellZ = centerZ + z - 3;
                const uint64_t hash = WorldGenContext::hashPosition(
                    basinSeed, cellX, 0, cellZ);
                node.x = static_cast<float>(cellX * BASIN_CELL_SIZE + 48 +
                    static_cast<int>(hash % (BASIN_CELL_SIZE - 96)));
                node.z = static_cast<float>(cellZ * BASIN_CELL_SIZE + 48 +
                    static_cast<int>((hash >> 16) % (BASIN_CELL_SIZE - 96)));
                const float continental = m_continental.GetNoise(node.x, node.z);
                const float erosion = m_erosion.GetNoise(
                    node.x + 19000.0f, node.z - 7000.0f);
                const float jitter = static_cast<float>((hash >> 40) & 0xFFFFu) /
                                     65535.0f - 0.5f;
                node.potential = continental * 0.78f + erosion * 0.16f +
                                 jitter * 0.06f;
                node.downstream = -1;
                node.accumulation = 1;
            }
        }

        for (int z = 1; z < side - 1; ++z) {
            for (int x = 1; x < side - 1; ++x) {
                const int current = indexOf(x, z);
                float lowest = cache.nodes[current].potential - 0.004f;
                int downstream = -1;
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dz == 0) continue;
                        const int neighbor = indexOf(x + dx, z + dz);
                        if (cache.nodes[neighbor].potential < lowest) {
                            lowest = cache.nodes[neighbor].potential;
                            downstream = neighbor;
                        }
                    }
                }
                cache.nodes[current].downstream = downstream;
            }
        }

        std::array<int, 25> order{};
        int cursor = 0;
        for (int z = 1; z < side - 1; ++z)
            for (int x = 1; x < side - 1; ++x)
                order[cursor++] = indexOf(x, z);
        std::sort(order.begin(), order.end(), [&](int left, int right) {
            return cache.nodes[left].potential > cache.nodes[right].potential;
        });
        for (const int current : order) {
            const int downstream = cache.nodes[current].downstream;
            if (downstream < 0) continue;
            const int downstreamX = downstream % side;
            const int downstreamZ = downstream / side;
            if (downstreamX < 1 || downstreamX >= side - 1 ||
                downstreamZ < 1 || downstreamZ >= side - 1)
                continue;
            const unsigned total = cache.nodes[downstream].accumulation +
                                   cache.nodes[current].accumulation;
            cache.nodes[downstream].accumulation = static_cast<uint16_t>(
                std::min(total, 65535u));
        }
    }

    BasinInfo result;
    const float px = static_cast<float>(worldX);
    const float pz = static_cast<float>(worldZ);
    for (int z = 2; z <= 4; ++z) {
        for (int x = 2; x <= 4; ++x) {
            const BasinNode& source = cache.nodes[indexOf(x, z)];
            if (source.downstream < 0) {
                const float dx = px - source.x;
                const float dz = pz - source.z;
                const float radius = 20.0f +
                    std::sqrt(static_cast<float>(source.accumulation)) * 7.0f;
                const float distance = std::sqrt(dx * dx + dz * dz);
                const float weight = 1.0f - smoothstep(radius, radius + 24.0f,
                                                       distance);
                if (weight > result.lakeWeight) {
                    result.lakeWeight = weight;
                    result.inlandLake = weight > 0.42f;
                    result.upstreamSize = source.accumulation;
                }
                continue;
            }
            const BasinNode& target = cache.nodes[source.downstream];
            const float vx = target.x - source.x;
            const float vz = target.z - source.z;
            const float lengthSquared = vx * vx + vz * vz;
            const float t = clamp01(((px - source.x) * vx +
                                     (pz - source.z) * vz) / lengthSquared);
            const float nearestX = source.x + vx * t;
            const float nearestZ = source.z + vz * t;
            const float dx = px - nearestX;
            const float dz = pz - nearestZ;
            const float distance = std::sqrt(dx * dx + dz * dz);
            const float width = 2.5f +
                std::sqrt(static_cast<float>(source.accumulation)) * 2.2f;
            if (distance < result.channelDistance) {
                result.channelDistance = distance;
                result.channelWidth = width;
                result.upstreamSize = source.accumulation;
            }
        }
    }
    return result;
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

TerrainArchetype HeightPipeline::selectArchetype(
    uint64_t hash, const ClimateSample& climate) const {
    if (climate.continentalness < -0.48f)
        return hash % 4 == 0 ? TerrainArchetype::ISLAND_ARC
                             : TerrainArchetype::DEEP_OCEAN_TRENCH;
    if (climate.continentalness < -0.16f)
        return hash % 3 == 0 ? TerrainArchetype::COASTAL_CLIFFS
                             : TerrainArchetype::ISLAND_ARC;
    if (climate.continentalness < -0.04f && hash % 2 == 0)
        return TerrainArchetype::COASTAL_CLIFFS;

    static constexpr TerrainArchetype land[] = {
        TerrainArchetype::ROLLING_LOWLANDS,
        TerrainArchetype::BROAD_RIVER_VALLEY,
        TerrainArchetype::WETLAND_BASIN,
        TerrainArchetype::WOODED_HILLS,
        TerrainArchetype::HIGH_PLATEAU,
        TerrainArchetype::DUNE_SEA,
        TerrainArchetype::RED_ROCK_CANYON,
        TerrainArchetype::KARST_TOWERS,
        TerrainArchetype::ALPINE_RANGE,
        TerrainArchetype::GLACIAL_RANGE,
        TerrainArchetype::VOLCANIC_CALDERA,
        TerrainArchetype::WINDSWEPT_RIDGES
    };
    size_t index = static_cast<size_t>(hash % (sizeof(land) / sizeof(land[0])));
    if (climate.temperature < -0.42f && (hash & 3u) == 0)
        return TerrainArchetype::GLACIAL_RANGE;
    if (climate.temperature > 0.32f && climate.humidity < -0.16f &&
        (hash & 3u) == 1)
        return hash & 4u ? TerrainArchetype::DUNE_SEA
                         : TerrainArchetype::RED_ROCK_CANYON;
    if (climate.humidity > 0.30f && (hash & 7u) == 2)
        return TerrainArchetype::KARST_TOWERS;
    return land[index];
}

SurfaceColumn HeightPipeline::sampleBaseColumn(int worldX, int worldZ) const {
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
    static constexpr float CONT_Y[] = {12.0f, 24.0f, 42.0f, 57.0f, 64.0f, 76.0f, 94.0f, 112.0f};
    float base = spline(climate.continentalness, CONT_X, CONT_Y, 8);

    float inland = smoothstep(-0.12f, 0.32f, climate.continentalness);
    float lowErosion = 1.0f - smoothstep(-0.45f, 0.65f, climate.erosion);
    float peakBand = smoothstep(0.05f, 0.78f, climate.peaksValleys);
    float mountain = inland * lowErosion * peakBand;
    float ridge = std::max(0.0f, m_ridges.GetNoise(x, z));
    mountain = clamp01(mountain * 0.72f + inland * lowErosion * ridge * 0.55f);
    float rolling = inland * (climate.peaksValleys * 12.0f +
                              (0.20f - climate.erosion) * 9.0f);
    float detail = m_detail.GetNoise(x, z) * (2.5f + inland * 4.0f);

    float height = base + rolling * 0.55f + mountain * (38.0f + ridge * 42.0f) + detail;

    struct Candidate {
        float distanceSquared = 0.0f;
        float anchorX = 0.0f;
        float anchorZ = 0.0f;
        uint64_t hash = 0;
        TerrainArchetype type = TerrainArchetype::ROLLING_LOWLANDS;
    };
    struct MacroAnchor {
        float x = 0.0f;
        float z = 0.0f;
        uint64_t hash = 0;
    };
    struct MacroCache {
        const HeightPipeline* owner = nullptr;
        int cellX = std::numeric_limits<int>::max();
        int cellZ = std::numeric_limits<int>::max();
        std::array<MacroAnchor, 9> anchors{};
    };
    thread_local MacroCache macroCache;

    std::array<Candidate, 3> candidates{};
    for (Candidate& candidate : candidates)
        candidate.distanceSquared = std::numeric_limits<float>::max();
    const int cellX = floorDiv(worldX, MACRO_CELL_SIZE);
    const int cellZ = floorDiv(worldZ, MACRO_CELL_SIZE);
    const uint64_t macroSeed = m_context.derive(DOMAIN_MACRO);
    if (macroCache.owner != this || macroCache.cellX != cellX ||
        macroCache.cellZ != cellZ) {
        macroCache.owner = this;
        macroCache.cellX = cellX;
        macroCache.cellZ = cellZ;
        size_t index = 0;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int cx = cellX + dx;
                const int cz = cellZ + dz;
                const uint64_t hash = WorldGenContext::hashPosition(
                    macroSeed, cx, 0, cz);
                macroCache.anchors[index++] = {
                    static_cast<float>(cx * MACRO_CELL_SIZE + 112 +
                        static_cast<int>(hash % 672)),
                    static_cast<float>(cz * MACRO_CELL_SIZE + 112 +
                        static_cast<int>((hash >> 16) % 672)), hash};
            }
        }
    }
    for (const MacroAnchor& anchor : macroCache.anchors) {
            const float ddx = x - anchor.x;
            const float ddz = z - anchor.z;
            Candidate candidate{
                ddx * ddx + ddz * ddz, anchor.x, anchor.z, anchor.hash,
                selectArchetype(anchor.hash, climate)};
            if (candidate.distanceSquared < candidates[2].distanceSquared) {
                candidates[2] = candidate;
                if (candidates[2].distanceSquared < candidates[1].distanceSquared)
                    std::swap(candidates[2], candidates[1]);
                if (candidates[1].distanceSquared < candidates[0].distanceSquared)
                    std::swap(candidates[1], candidates[0]);
            }
    }

    std::array<float, 3> weights{};
    float weightSum = 0.0f;
    for (size_t i = 0; i < weights.size(); ++i) {
        const float normalized = std::sqrt(candidates[i].distanceSquared) /
                                 static_cast<float>(MACRO_CELL_SIZE);
        const float inverse = 1.0f / (0.10f + normalized * normalized);
        weights[i] = inverse * inverse;
        weightSum += weights[i];
    }
    for (float& weight : weights) weight /= weightSum;

    auto archetypeOffset = [&](const Candidate& candidate) {
        switch (candidate.type) {
            case TerrainArchetype::DEEP_OCEAN_TRENCH:
                return -18.0f - ridge * 18.0f;
            case TerrainArchetype::ISLAND_ARC:
            case TerrainArchetype::VOLCANIC_CALDERA: {
                const float nx = (x - candidate.anchorX) /
                                 static_cast<float>(MACRO_CELL_SIZE);
                const float nz = (z - candidate.anchorZ) /
                                 static_cast<float>(MACRO_CELL_SIZE);
                const float radial = std::sqrt(nx * nx + nz * nz);
                if (candidate.type == TerrainArchetype::ISLAND_ARC)
                    return std::max(0.0f, 1.0f - radial * 1.7f) * 42.0f;
                const float cone = smoothstep(0.88f, 0.10f, radial);
                const float crater = 1.0f - smoothstep(0.08f, 0.18f, radial);
                return cone * 154.0f - crater * 58.0f;
            }
            case TerrainArchetype::COASTAL_CLIFFS:
                return smoothstep(-0.24f, -0.02f, climate.continentalness) *
                       (22.0f + ridge * 34.0f);
            case TerrainArchetype::ROLLING_LOWLANDS:
                return climate.peaksValleys * 7.0f;
            case TerrainArchetype::BROAD_RIVER_VALLEY:
                return (72.0f - height) * 0.48f - ridge * 5.0f;
            case TerrainArchetype::WETLAND_BASIN:
                return (static_cast<float>(Config::SEA_LEVEL + 1) - height) * 0.70f;
            case TerrainArchetype::WOODED_HILLS:
                return 10.0f + ridge * 24.0f;
            case TerrainArchetype::HIGH_PLATEAU:
                return (112.0f + std::floor((detail + 8.0f) / 5.0f) * 1.75f -
                        height) * 0.68f;
            case TerrainArchetype::DUNE_SEA:
                return 8.0f + std::sin((x + z) * 0.018f) * 7.0f + detail * 0.4f;
            case TerrainArchetype::RED_ROCK_CANYON:
                return (118.0f - height) * 0.58f -
                    (1.0f - smoothstep(0.025f, 0.16f,
                     std::abs(climate.weirdness * 0.68f +
                              climate.erosion * 0.32f))) *
                    48.0f + std::floor((detail + 8.0f) / 5.0f) * 1.25f;
            case TerrainArchetype::KARST_TOWERS: {
                const float tower = smoothstep(0.34f, 0.82f, ridge);
                return 8.0f + tower * tower * 78.0f;
            }
            case TerrainArchetype::ALPINE_RANGE:
                return 26.0f + lowErosion * (62.0f + ridge * 88.0f);
            case TerrainArchetype::GLACIAL_RANGE:
                return 34.0f + lowErosion * (72.0f + ridge * 62.0f);
            case TerrainArchetype::WINDSWEPT_RIDGES:
                return 16.0f + ridge * 70.0f + climate.peaksValleys * 18.0f;
            case TerrainArchetype::COUNT:
                return 0.0f;
        }
        return 0.0f;
    };
    for (size_t i = 0; i < weights.size(); ++i)
        height += weights[i] * archetypeOffset(candidates[i]);

    // Jittered drainage nodes route to the lowest-potential neighbor. Their
    // accumulated upstream area controls channel width and naturally creates
    // tributaries, confluences and occasional endorheic lakes.
    const BasinInfo basin = sampleBasin(worldX, worldZ);
    const float channel = 1.0f - smoothstep(
        basin.channelWidth, basin.channelWidth + 14.0f,
        basin.channelDistance);
    float riverClimate = 1.0f - smoothstep(0.50f, 0.82f, climate.erosion);
    float riverWeight = std::max(channel, basin.lakeWeight * 0.88f) *
                        inland * riverClimate;
    const bool valleyArchetype = candidates[0].type == TerrainArchetype::BROAD_RIVER_VALLEY ||
                                 candidates[0].type == TerrainArchetype::WETLAND_BASIN;
    if (valleyArchetype) riverWeight = std::min(1.0f, riverWeight * 1.18f);
    bool river = riverWeight > 0.56f && climate.continentalness > -0.13f;
    if (riverWeight > 0.0f) {
        float riverFloor = static_cast<float>(Config::SEA_LEVEL - 2);
        height = height + (riverFloor - height) * smoothstep(0.35f, 0.92f, riverWeight);
    }

    int finalHeight = static_cast<int>(std::round(std::max(
        static_cast<float>(Config::TERRAIN_MIN_HEIGHT),
        std::min(static_cast<float>(Config::TERRAIN_MAX_HEIGHT), height))));
    bool deepOcean = climate.continentalness < -0.46f;
    bool coast = !river && climate.continentalness >= -0.20f &&
                 climate.continentalness < -0.07f;

    SurfaceColumn result;
    result.height = finalHeight;
    result.nominalHeight = finalHeight;
    result.waterLevel = deepOcean ? Config::SEA_LEVEL + 3 : Config::SEA_LEVEL;
    result.mountainFactor = mountain;
    const bool complexDensity =
        (candidates[0].type == TerrainArchetype::COASTAL_CLIFFS ||
         candidates[0].type == TerrainArchetype::KARST_TOWERS ||
         candidates[0].type == TerrainArchetype::VOLCANIC_CALDERA) &&
        ridge > 0.32f;
    if (complexDensity) result.mountainFactor = std::max(result.mountainFactor, 0.72f);
    result.slope = clamp01(std::abs(detail) / 9.0f +
                           result.mountainFactor * 0.65f + ridge * 0.25f);
    result.riverWeight = riverWeight;
    result.densityMinY = complexDensity ? finalHeight - 28 : finalHeight;
    result.densityMaxY = complexDensity ? finalHeight + 32 : finalHeight;
    result.archetype = candidates[0].type;
    result.secondaryArchetype = candidates[1].type;
    result.archetypeBlend = weights[1] / (weights[0] + weights[1]);
    result.basin = basin;
    result.river = river;
    result.climate = climate;
    result.biome = selectBiome(climate, finalHeight, river, coast, deepOcean,
                               result.archetype);
    return result;
}

bool HeightPipeline::isTerrainSolid(int worldX, int worldY, int worldZ,
                                    const SurfaceColumn& column) const {
    if (worldY <= column.densityMinY) return true;
    if (worldY > column.densityMaxY) return false;
    float overhangMask = smoothstep(0.42f, 0.78f, column.mountainFactor);
    if (column.archetype == TerrainArchetype::COASTAL_CLIFFS ||
        column.archetype == TerrainArchetype::KARST_TOWERS ||
        column.archetype == TerrainArchetype::VOLCANIC_CALDERA)
        overhangMask = std::max(overhangMask, 0.72f);
    if (overhangMask <= 0.0f) return worldY <= column.nominalHeight;
    float x = static_cast<float>(worldX);
    float y = static_cast<float>(worldY);
    float z = static_cast<float>(worldZ);
    m_surfaceWarp.DomainWarp(x, y, z);
    const float noise = m_surfaceDensity.GetNoise(x, y * 0.82f, z);
    const float vertical = static_cast<float>(column.nominalHeight - worldY);
    return vertical + noise * (6.0f + 13.0f * overhangMask) > 0.0f;
}

SurfaceColumn HeightPipeline::sampleColumn(int worldX, int worldZ) const {
    SurfaceColumn result = sampleBaseColumn(worldX, worldZ);
    const int scanTop = std::min(Config::TERRAIN_MAX_HEIGHT, result.densityMaxY);
    const int scanBottom = std::max(Config::WORLD_MIN_Y, result.densityMinY);
    result.height = result.nominalHeight;
    for (int y = scanTop; y >= scanBottom; --y) {
        if (isTerrainSolid(worldX, y, worldZ, result)) {
            result.height = y;
            break;
        }
    }
    result.biome = selectBiome(result.climate, result.height, result.river,
        result.climate.continentalness >= -0.20f &&
        result.climate.continentalness < -0.07f,
        result.climate.continentalness < -0.46f, result.archetype);
    return result;
}

Biome HeightPipeline::selectBiome(const ClimateSample& c, int height,
                                  bool river, bool coast, bool deepOcean,
                                  TerrainArchetype archetype) const {
    if (deepOcean && height <= Config::SEA_LEVEL) return Biome::DEEP_OCEAN;
    if (c.continentalness < -0.20f && height <= Config::SEA_LEVEL + 1)
        return Biome::OCEAN;
    if (river) return Biome::RIVER;
    if (archetype == TerrainArchetype::VOLCANIC_CALDERA)
        return coast ? Biome::BLACK_SAND_COAST : Biome::VOLCANIC_HIGHLANDS;
    if (archetype == TerrainArchetype::RED_ROCK_CANYON)
        return Biome::RED_CANYON;
    if (archetype == TerrainArchetype::GLACIAL_RANGE && height >= 108)
        return Biome::GLACIAL_PEAKS;
    if ((archetype == TerrainArchetype::ALPINE_RANGE ||
         archetype == TerrainArchetype::WINDSWEPT_RIDGES) && height >= 112)
        return c.temperature < 0.12f ? Biome::ALPINE_TUNDRA : Biome::ROCKY_STEPPE;
    if (archetype == TerrainArchetype::KARST_TOWERS)
        return c.humidity > 0.08f ? Biome::KARST_FOREST
                                 : Biome::LIMESTONE_HIGHLANDS;
    if ((archetype == TerrainArchetype::BROAD_RIVER_VALLEY ||
         archetype == TerrainArchetype::WETLAND_BASIN) && c.humidity > 0.02f)
        return Biome::LUSH_VALLEY;
    if (archetype == TerrainArchetype::WOODED_HILLS &&
        c.temperature > 0.12f && c.humidity < 0.05f)
        return Biome::DRY_WOODLAND;
    if (coast) {
        if (archetype == TerrainArchetype::COASTAL_CLIFFS &&
            c.temperature > 0.08f && c.weirdness > 0.0f)
            return Biome::BLACK_SAND_COAST;
        return (c.erosion < -0.18f || height > Config::SEA_LEVEL + 5)
            ? Biome::STONY_SHORE : Biome::BEACH;
    }

    if (height >= 145 || (height >= 115 && c.erosion < -0.30f))
        return Biome::MOUNTAINS;
    if (height >= 90 && c.peaksValleys > 0.20f)
        return c.humidity > 0.35f ? Biome::MEADOW : Biome::HILLS;

    if (c.temperature > 0.38f && c.humidity < -0.20f) {
        return c.erosion < -0.12f ? Biome::BADLANDS : Biome::DESERT;
    }
    if (c.temperature > 0.30f) {
        if (c.humidity > 0.30f) return Biome::JUNGLE;
        if (c.humidity < -0.10f) return Biome::SAVANNA;
    }
    if (c.temperature < -0.42f)
        return Biome::SNOW_TUNDRA;
    if (c.temperature < -0.18f)
        return Biome::TAIGA;
    if (height <= Config::SEA_LEVEL + 4 && c.humidity > 0.28f)
        return Biome::SWAMP;
    if (c.humidity > 0.32f) {
        if (c.weirdness < -0.28f) return Biome::FLOWER_FOREST;
        return c.weirdness > 0.18f ? Biome::BIRCH_FOREST : Biome::FOREST;
    }
    if (c.temperature > -0.15f && c.temperature < 0.38f &&
        c.humidity > -0.24f && c.weirdness > 0.26f)
        return Biome::SUNFLOWER_PLAINS;
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
            out.nominalHeight = sample.nominalHeight;
            out.mountainFactor = sample.mountainFactor;
            out.slope = sample.slope;
            out.riverWeight = sample.riverWeight;
            out.densityMinY = sample.densityMinY;
            out.densityMaxY = sample.densityMaxY;
            out.archetype = sample.archetype;
            out.secondaryArchetype = sample.secondaryArchetype;
            out.archetypeBlend = sample.archetypeBlend;
            out.basin = sample.basin;
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
