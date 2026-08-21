#include "world/HeightPipeline.h"
#include "world/Noise.h"
#include "Config.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>
#include <vector>

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
constexpr uint64_t DOMAIN_VOLCANO = 0x564F4C43414E4F38ULL;
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
        int cellX = 0;
        int cellZ = 0;
        float x = 0.0f;
        float z = 0.0f;
        float potential = 0.0f;
        float terrainLevel = static_cast<float>(Config::SEA_LEVEL);
        uint64_t hash = 0;
        int downstreamX = 0;
        int downstreamZ = 0;
        bool hasDownstream = false;
        uint16_t accumulation = 1;
    };
    struct BasinCache {
        const HeightPipeline* owner = nullptr;
        uint64_t seed = 0;
        int centerX = std::numeric_limits<int>::max();
        int centerZ = std::numeric_limits<int>::max();
        std::array<BasinNode, 49> nodes{};
    };
    thread_local std::array<BasinCache, 4> caches;
    thread_local size_t nextCache = 0;

    BasinCache* selectedCache = nullptr;
    for (BasinCache& candidate : caches) {
        if (candidate.owner == this && candidate.seed == m_context.seed()) {
            selectedCache = &candidate;
            break;
        }
    }
    if (!selectedCache) {
        for (BasinCache& candidate : caches) {
            if (!candidate.owner) {
                selectedCache = &candidate;
                break;
            }
        }
    }
    if (!selectedCache)
        selectedCache = &caches[nextCache++ % caches.size()];
    if (selectedCache->owner != this ||
        selectedCache->seed != m_context.seed()) {
        *selectedCache = BasinCache{};
        selectedCache->owner = this;
        selectedCache->seed = m_context.seed();
    }
    BasinCache& cache = *selectedCache;

    constexpr int side = 7;
    constexpr auto indexOf = [](int x, int z) { return z * 7 + x; };
    const int centerX = floorDiv(worldX, BASIN_CELL_SIZE);
    const int centerZ = floorDiv(worldZ, BASIN_CELL_SIZE);
    if (cache.owner != this || cache.centerX != centerX ||
        cache.centerZ != centerZ) {
        cache.owner = this;
        cache.seed = m_context.seed();
        cache.centerX = centerX;
        cache.centerZ = centerZ;
        const uint64_t basinSeed = m_context.derive(DOMAIN_BASIN);
        using Cell = std::pair<int, int>;
        std::map<Cell, BasinNode> nodeMemo;
        std::map<Cell, Cell> downstreamMemo;
        std::map<std::tuple<int, int, int>, uint16_t> accumulationMemo;

        auto nodeAt = [&](int cellX, int cellZ) {
            const Cell key{cellX, cellZ};
            const auto found = nodeMemo.find(key);
            if (found != nodeMemo.end()) return found->second;
            BasinNode node;
            node.cellX = cellX;
            node.cellZ = cellZ;
            node.hash = WorldGenContext::hashPosition(
                basinSeed, cellX, 0, cellZ);
            node.x = static_cast<float>(cellX * BASIN_CELL_SIZE + 48 +
                static_cast<int>(node.hash % (BASIN_CELL_SIZE - 96)));
            node.z = static_cast<float>(cellZ * BASIN_CELL_SIZE + 48 +
                static_cast<int>((node.hash >> 16) % (BASIN_CELL_SIZE - 96)));
            const ClimateSample climate = sampleClimate(node.x, node.z);
            static constexpr float CONT_X[] = {
                -1.0f, -0.55f, -0.30f, -0.16f,
                -0.08f, 0.20f, 0.55f, 1.0f};
            static constexpr float CONT_Y[] = {
                12.0f, 24.0f, 42.0f, 57.0f,
                64.0f, 76.0f, 94.0f, 112.0f};
            const float inland = smoothstep(
                -0.12f, 0.32f, climate.continentalness);
            const float lowErosion = 1.0f - smoothstep(
                -0.45f, 0.65f, climate.erosion);
            const float ridge = std::max(
                0.0f, m_ridges.GetNoise(node.x, node.z));
            node.terrainLevel = spline(climate.continentalness,
                CONT_X, CONT_Y, 8) +
                inland * climate.peaksValleys * 5.0f +
                inland * lowErosion * ridge * 18.0f;
            const float jitter = static_cast<float>(
                (node.hash >> 40) & 0xFFFFu) / 65535.0f - 0.5f;
            node.potential = node.terrainLevel + jitter * 3.0f;
            nodeMemo.emplace(key, node);
            return node;
        };

        auto downstreamOf = [&](int cellX, int cellZ) {
            const Cell key{cellX, cellZ};
            const auto found = downstreamMemo.find(key);
            if (found != downstreamMemo.end()) return found->second;
            const BasinNode source = nodeAt(cellX, cellZ);
            float lowest = source.potential - 0.004f;
            Cell downstream{cellX, cellZ};
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dz == 0) continue;
                    const BasinNode neighbor = nodeAt(cellX + dx, cellZ + dz);
                    if (neighbor.potential < lowest) {
                        lowest = neighbor.potential;
                        downstream = {cellX + dx, cellZ + dz};
                    }
                }
            }
            downstreamMemo.emplace(key, downstream);
            return downstream;
        };

        std::function<uint16_t(int, int, int)> accumulationAt;
        accumulationAt = [&](int cellX, int cellZ, int depth) -> uint16_t {
            const auto key = std::make_tuple(cellX, cellZ, depth);
            const auto found = accumulationMemo.find(key);
            if (found != accumulationMemo.end()) return found->second;
            unsigned total = 1;
            if (depth > 0) {
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dz == 0) continue;
                        const Cell downstream = downstreamOf(
                            cellX + dx, cellZ + dz);
                        if (downstream.first == cellX &&
                            downstream.second == cellZ) {
                            total += accumulationAt(
                                cellX + dx, cellZ + dz, depth - 1);
                        }
                    }
                }
            }
            const uint16_t result = static_cast<uint16_t>(
                std::min(total, 65535u));
            accumulationMemo.emplace(key, result);
            return result;
        };

        for (int z = 0; z < side; ++z) {
            for (int x = 0; x < side; ++x) {
                BasinNode node = nodeAt(centerX + x - 3,
                                        centerZ + z - 3);
                const Cell downstream = downstreamOf(node.cellX, node.cellZ);
                node.hasDownstream = downstream.first != node.cellX ||
                                     downstream.second != node.cellZ;
                node.downstreamX = downstream.first;
                node.downstreamZ = downstream.second;
                node.accumulation = accumulationAt(
                    node.cellX, node.cellZ, 3);
                cache.nodes[indexOf(x, z)] = node;
            }
        }
    }

    BasinInfo result;
    const float px = static_cast<float>(worldX);
    const float pz = static_cast<float>(worldZ);
    for (int z = 2; z <= 4; ++z) {
        for (int x = 2; x <= 4; ++x) {
            const BasinNode& source = cache.nodes[indexOf(x, z)];
            const bool stableLake = !source.hasDownstream ||
                                    source.hash % 29 == 0;
            if (stableLake) {
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
                    result.channelWaterY = static_cast<int>(std::round(
                        source.terrainLevel - 1.0f));
                    result.channelBedY = result.channelWaterY - 2;
                    result.valleyWidth = radius + 24.0f;
                }
                if (!source.hasDownstream) continue;
            }
            const int targetX = source.downstreamX - centerX + 3;
            const int targetZ = source.downstreamZ - centerZ + 3;
            if (targetX < 0 || targetX >= side ||
                targetZ < 0 || targetZ >= side)
                continue;
            const BasinNode& target = cache.nodes[indexOf(targetX, targetZ)];
            const float vx = target.x - source.x;
            const float vz = target.z - source.z;
            const float lengthSquared = vx * vx + vz * vz;
            const float t = clamp01(((px - source.x) * vx +
                                     (pz - source.z) * vz) / lengthSquared);
            const float length = std::sqrt(lengthSquared);
            const float invLength = length > 0.0f ? 1.0f / length : 0.0f;
            const float phase = static_cast<float>((source.hash >> 32) & 0xFFFFu) /
                                65535.0f * 6.28318530718f;
            const float meander = std::sin(t * 3.14159265359f) *
                std::sin(t * 12.56637061436f + phase) *
                std::min(26.0f, length * 0.08f);
            const float nearestX = source.x + vx * t - vz * invLength * meander;
            const float nearestZ = source.z + vz * t + vx * invLength * meander;
            const float dx = px - nearestX;
            const float dz = pz - nearestZ;
            const float distance = std::sqrt(dx * dx + dz * dz);
            const float width = 2.5f +
                std::sqrt(static_cast<float>(source.accumulation)) * 2.2f;
            if (distance < result.channelDistance) {
                result.channelDistance = distance;
                result.channelWidth = width;
                result.upstreamSize = source.accumulation;
                const float downstreamLevel = std::min(
                    source.terrainLevel, target.terrainLevel);
                result.channelWaterY = static_cast<int>(std::round(
                    source.terrainLevel +
                    (downstreamLevel - source.terrainLevel) * t - 1.0f));
                const int bedDepth = 2 + std::min(3, static_cast<int>(
                    std::sqrt(static_cast<float>(source.accumulation)) * 0.55f));
                result.channelBedY = result.channelWaterY - bedDepth;
                result.valleyWidth = std::max(24.0f,
                    width + 18.0f +
                    std::sqrt(static_cast<float>(source.accumulation)) * 4.0f);
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

ClimateSample HeightPipeline::sampleClimate(float worldX, float worldZ) const {
    float x = worldX;
    float z = worldZ;
    m_warp.DomainWarp(x, z);

    ClimateSample climate;
    climate.continentalness = m_continental.GetNoise(x, z);
    climate.erosion = m_erosion.GetNoise(x + 19000.0f, z - 7000.0f);
    climate.weirdness = m_weirdness.GetNoise(x - 11000.0f, z + 23000.0f);
    climate.peaksValleys = peaksAndValleys(climate.weirdness);
    climate.temperature = m_temperature.GetNoise(x + 31000.0f, z + 17000.0f);
    climate.humidity = m_humidity.GetNoise(x - 29000.0f, z - 13000.0f);
    return climate;
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

    if (climate.temperature < -0.34f && climate.erosion < 0.18f) {
        static constexpr TerrainArchetype cold[] = {
            TerrainArchetype::GLACIAL_RANGE,
            TerrainArchetype::ALPINE_RANGE,
            TerrainArchetype::WINDSWEPT_RIDGES};
        return cold[hash % 3];
    }
    if (climate.temperature > 0.26f && climate.humidity < -0.08f) {
        static constexpr TerrainArchetype dry[] = {
            TerrainArchetype::DUNE_SEA,
            TerrainArchetype::RED_ROCK_CANYON,
            TerrainArchetype::HIGH_PLATEAU};
        return dry[hash % 3];
    }
    if (climate.humidity > 0.22f) {
        static constexpr TerrainArchetype wet[] = {
            TerrainArchetype::WETLAND_BASIN,
            TerrainArchetype::BROAD_RIVER_VALLEY,
            TerrainArchetype::WOODED_HILLS,
            TerrainArchetype::KARST_TOWERS};
        return wet[hash % 4];
    }
    if (climate.erosion < -0.18f && climate.peaksValleys > 0.12f) {
        static constexpr TerrainArchetype rugged[] = {
            TerrainArchetype::ALPINE_RANGE,
            TerrainArchetype::WINDSWEPT_RIDGES,
            TerrainArchetype::HIGH_PLATEAU};
        return rugged[hash % 3];
    }
    static constexpr TerrainArchetype temperate[] = {
        TerrainArchetype::ROLLING_LOWLANDS,
        TerrainArchetype::BROAD_RIVER_VALLEY,
        TerrainArchetype::WOODED_HILLS,
        TerrainArchetype::HIGH_PLATEAU};
    return temperate[hash % 4];
}

SurfaceColumn HeightPipeline::sampleBaseColumn(int worldX, int worldZ) const {
    float x = static_cast<float>(worldX);
    float z = static_cast<float>(worldZ);
    m_warp.DomainWarp(x, z);
    const ClimateSample climate = sampleClimate(
        static_cast<float>(worldX), static_cast<float>(worldZ));

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
        TerrainArchetype type = TerrainArchetype::ROLLING_LOWLANDS;
    };
    struct MacroCache {
        const HeightPipeline* owner = nullptr;
        uint64_t seed = 0;
        int cellX = std::numeric_limits<int>::max();
        int cellZ = std::numeric_limits<int>::max();
        std::array<MacroAnchor, 9> anchors{};
    };
    thread_local std::array<MacroCache, 4> macroCaches;
    thread_local size_t nextMacroCache = 0;

    MacroCache* selectedMacroCache = nullptr;
    for (MacroCache& candidate : macroCaches) {
        if (candidate.owner == this && candidate.seed == m_context.seed()) {
            selectedMacroCache = &candidate;
            break;
        }
    }
    if (!selectedMacroCache) {
        for (MacroCache& candidate : macroCaches) {
            if (!candidate.owner) {
                selectedMacroCache = &candidate;
                break;
            }
        }
    }
    if (!selectedMacroCache)
        selectedMacroCache = &macroCaches[
            nextMacroCache++ % macroCaches.size()];
    if (selectedMacroCache->owner != this ||
        selectedMacroCache->seed != m_context.seed()) {
        *selectedMacroCache = MacroCache{};
        selectedMacroCache->owner = this;
        selectedMacroCache->seed = m_context.seed();
    }
    MacroCache& macroCache = *selectedMacroCache;

    std::array<Candidate, 4> candidates{};
    for (Candidate& candidate : candidates)
        candidate.distanceSquared = std::numeric_limits<float>::max();
    const int cellX = floorDiv(worldX, MACRO_CELL_SIZE);
    const int cellZ = floorDiv(worldZ, MACRO_CELL_SIZE);
    const uint64_t macroSeed = m_context.derive(DOMAIN_MACRO);
    if (macroCache.owner != this || macroCache.cellX != cellX ||
        macroCache.cellZ != cellZ) {
        macroCache.owner = this;
        macroCache.seed = m_context.seed();
        macroCache.cellX = cellX;
        macroCache.cellZ = cellZ;
        size_t index = 0;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int cx = cellX + dx;
                const int cz = cellZ + dz;
                const uint64_t hash = WorldGenContext::hashPosition(
                    macroSeed, cx, 0, cz);
                const float anchorX = static_cast<float>(
                    cx * MACRO_CELL_SIZE + 112 +
                    static_cast<int>(hash % 672));
                const float anchorZ = static_cast<float>(
                    cz * MACRO_CELL_SIZE + 112 +
                    static_cast<int>((hash >> 16) % 672));
                const ClimateSample anchorClimate = sampleClimate(
                    anchorX, anchorZ);
                macroCache.anchors[index++] = {
                    anchorX, anchorZ, hash,
                    selectArchetype(hash, anchorClimate)};
            }
        }
    }
    for (const MacroAnchor& anchor : macroCache.anchors) {
        const float ddx = static_cast<float>(worldX) - anchor.x;
        const float ddz = static_cast<float>(worldZ) - anchor.z;
        Candidate candidate{
            ddx * ddx + ddz * ddz, anchor.x, anchor.z, anchor.hash,
            anchor.type};
        if (candidate.distanceSquared >= candidates.back().distanceSquared)
            continue;
        candidates.back() = candidate;
        for (size_t i = candidates.size() - 1; i > 0; --i) {
            if (candidates[i].distanceSquared >=
                candidates[i - 1].distanceSquared)
                break;
            std::swap(candidates[i], candidates[i - 1]);
        }
    }

    // The nearest cell owns its interior. Only the compact band around a
    // Voronoi boundary mixes the neighboring immutable anchor types.
    constexpr float blendSupport = 384.0f; // about 192 blocks per side
    constexpr float junctionSupport = 96.0f;
    const float nearestDistance = std::sqrt(candidates[0].distanceSquared);
    std::array<float, 4> candidateDistances{
        nearestDistance, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> weights{1.0f, 0.0f, 0.0f, 0.0f};
    for (size_t i = 1; i < weights.size(); ++i) {
        const float distance = std::sqrt(candidates[i].distanceSquared);
        candidateDistances[i] = distance;
        const float support = 1.0f - clamp01(
            (distance - nearestDistance) / blendSupport);
        weights[i] = support * support;
    }

    // A four-anchor Voronoi junction has no continuous, permutation-symmetric
    // way to retain only three non-zero profiles. Keep the compact distance
    // support, but admit the fourth profile at these rare junctions so rank
    // exchanges cannot discard a still-significant terrain contribution.
    weights[3] *= 1.0f - smoothstep(
        0.0f, junctionSupport,
        candidateDistances[3] - candidateDistances[2]);
    float weightSum = 0.0f;
    for (float weight : weights) weightSum += weight;
    for (float& weight : weights) weight /= weightSum;

    // Archetype blending must be commutative. Some profiles are expressed as
    // a pull toward an absolute target (valleys, wetlands and plateaus), so
    // they must all read the same pre-blend height. Reading the accumulated
    // height here makes a Voronoi label swap change the result even when the
    // candidate weights are continuous.
    const float archetypeBaseHeight = height;
    auto archetypeOffset = [&](const Candidate& candidate) {
        switch (candidate.type) {
            case TerrainArchetype::DEEP_OCEAN_TRENCH:
                return -18.0f - ridge * 18.0f;
            case TerrainArchetype::ISLAND_ARC: {
                const float nx = (static_cast<float>(worldX) - candidate.anchorX) /
                                 static_cast<float>(MACRO_CELL_SIZE);
                const float nz = (static_cast<float>(worldZ) - candidate.anchorZ) /
                                 static_cast<float>(MACRO_CELL_SIZE);
                const float radial = std::sqrt(nx * nx + nz * nz);
                return std::max(0.0f, 1.0f - radial * 1.7f) * 42.0f;
            }
            case TerrainArchetype::VOLCANIC_CALDERA:
                return 0.0f;
            case TerrainArchetype::COASTAL_CLIFFS:
                return smoothstep(-0.24f, -0.02f, climate.continentalness) *
                       (22.0f + ridge * 34.0f);
            case TerrainArchetype::ROLLING_LOWLANDS:
                return climate.peaksValleys * 7.0f;
            case TerrainArchetype::BROAD_RIVER_VALLEY:
                return (72.0f - archetypeBaseHeight) * 0.48f - ridge * 5.0f;
            case TerrainArchetype::WETLAND_BASIN:
                return (static_cast<float>(Config::SEA_LEVEL + 1) -
                        archetypeBaseHeight) * 0.70f;
            case TerrainArchetype::WOODED_HILLS:
                return 10.0f + ridge * 24.0f;
            case TerrainArchetype::HIGH_PLATEAU:
                return (112.0f + std::floor((detail + 8.0f) / 5.0f) * 1.75f -
                        archetypeBaseHeight) * 0.68f;
            case TerrainArchetype::DUNE_SEA:
                return 8.0f + std::sin((x + z) * 0.018f) * 7.0f + detail * 0.4f;
            case TerrainArchetype::RED_ROCK_CANYON:
                return (118.0f - archetypeBaseHeight) * 0.58f -
                    (1.0f - smoothstep(0.025f, 0.16f,
                     std::abs(climate.weirdness * 0.68f +
                              climate.erosion * 0.32f))) *
                    48.0f + std::floor((detail + 8.0f) / 5.0f) * 1.25f;
            case TerrainArchetype::KARST_TOWERS: {
                const float tower = smoothstep(0.34f, 0.82f, ridge);
                return 8.0f + tower * tower * 78.0f;
            }
            case TerrainArchetype::ALPINE_RANGE:
                return 18.0f + lowErosion * (42.0f + ridge * 55.0f);
            case TerrainArchetype::GLACIAL_RANGE:
                return 10.0f + lowErosion * (14.0f + ridge * 20.0f);
            case TerrainArchetype::WINDSWEPT_RIDGES:
                return 12.0f + ridge * 48.0f + climate.peaksValleys * 12.0f;
            case TerrainArchetype::COUNT:
                return 0.0f;
        }
        return 0.0f;
    };
    std::array<float, 4> archetypeOffsets{};
    for (size_t i = 0; i < archetypeOffsets.size(); ++i) {
        if (weights[i] > 0.0f)
            archetypeOffsets[i] = archetypeOffset(candidates[i]);
    }
    float blendedArchetypeOffset = 0.0f;
    for (size_t i = 0; i < weights.size(); ++i)
        blendedArchetypeOffset += weights[i] * archetypeOffsets[i];
    height = archetypeBaseHeight + blendedArchetypeOffset;

    // Volcanism is a sparse finite-radius overlay, not a macro-cell type.
    float volcanicWeight = 0.0f;
    float craterWeight = 0.0f;
    const uint64_t volcanoSeed = m_context.derive(DOMAIN_VOLCANO);
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int vcx = cellX + dx;
            const int vcz = cellZ + dz;
            const uint64_t hash = WorldGenContext::hashPosition(
                volcanoSeed, vcx, 0, vcz);
            if (hash % 32 != 0) continue;
            const float anchorX = static_cast<float>(
                vcx * MACRO_CELL_SIZE + 112 +
                static_cast<int>((hash >> 8) % 672));
            const float anchorZ = static_cast<float>(
                vcz * MACRO_CELL_SIZE + 112 +
                static_cast<int>((hash >> 24) % 672));
            const ClimateSample anchorClimate = sampleClimate(anchorX, anchorZ);
            if (anchorClimate.continentalness < -0.04f ||
                anchorClimate.temperature < -0.42f)
                continue;

            const float radius = 320.0f +
                static_cast<float>((hash >> 40) % 161);
            const float craterRadius = 48.0f +
                static_cast<float>((hash >> 52) % 41);
            const float ddx = static_cast<float>(worldX) - anchorX;
            const float ddz = static_cast<float>(worldZ) - anchorZ;
            const float distance = std::sqrt(ddx * ddx + ddz * ddz);
            if (distance >= radius) continue;
            const float outer = 1.0f - smoothstep(radius * 0.68f,
                                                  radius, distance);
            const float radial = clamp01(distance / radius);
            const float cone = outer * std::pow(1.0f - radial, 0.72f);
            const float crater = 1.0f - smoothstep(
                craterRadius, craterRadius * 1.72f, distance);
            const float heightScale = 92.0f +
                static_cast<float>((hash >> 32) % 43);
            const float craterDepth = 30.0f +
                static_cast<float>((hash >> 20) % 19);
            height += cone * heightScale - crater * craterDepth * outer;
            volcanicWeight = std::max(volcanicWeight, outer);
            craterWeight = std::max(craterWeight, crater * outer);
        }
    }

    // Jittered drainage nodes route to the lowest-potential neighbor. Their
    // accumulated upstream area controls channel width and naturally creates
    // tributaries, confluences and occasional endorheic lakes.
    BasinInfo basin = sampleBasin(worldX, worldZ);
    const float channel = 1.0f - smoothstep(
        basin.channelWidth, basin.channelWidth + 6.0f,
        basin.channelDistance);
    const float valley = 1.0f - smoothstep(
        basin.channelWidth, std::max(basin.channelWidth + 1.0f,
                                     basin.valleyWidth),
        basin.channelDistance);
    float riverClimate = 1.0f - smoothstep(0.50f, 0.82f, climate.erosion);
    float riverWeight = std::max(channel, basin.lakeWeight * 0.88f) *
                        inland * riverClimate;
    float valleyArchetypeWeight = 0.0f;
    for (size_t i = 0; i < weights.size(); ++i) {
        if (candidates[i].type == TerrainArchetype::BROAD_RIVER_VALLEY ||
            candidates[i].type == TerrainArchetype::WETLAND_BASIN)
            valleyArchetypeWeight += weights[i];
    }
    riverWeight = std::min(
        1.0f, riverWeight * (1.0f + 0.18f * valleyArchetypeWeight));
    bool river = riverWeight > 0.56f && climate.continentalness > -0.13f;
    if (valley > 0.0f && basin.channelWaterY != 0) {
        const float preRiverHeight = height;
        const int maxIncision = mountain > 0.52f ? 24 : 12;
        const float localWater = std::max(
            preRiverHeight - static_cast<float>(maxIncision),
            std::min(preRiverHeight - 1.0f,
                     static_cast<float>(basin.channelWaterY)));
        const int bedDepth = std::max(2, std::min(5,
            basin.channelWaterY - basin.channelBedY));
        const float bankHeight = std::min(preRiverHeight, localWater + 2.0f);
        height += (bankHeight - height) * valley * valley;
        height += (localWater - static_cast<float>(bedDepth) - height) *
                  smoothstep(0.18f, 0.92f, channel);
        basin.channelWaterY = static_cast<int>(std::round(localWater));
        basin.channelBedY = basin.channelWaterY - bedDepth;
        basin.channelWeight = channel;
    }
    if (basin.lakeWeight > 0.0f && basin.channelWaterY != 0) {
        const float preLakeHeight = height;
        const float lakeWater = std::max(
            preLakeHeight - 12.0f,
            std::min(preLakeHeight - 1.0f,
                     static_cast<float>(basin.channelWaterY)));
        height += (lakeWater - 2.0f - height) *
                  smoothstep(0.16f, 0.92f, basin.lakeWeight);
        basin.channelWaterY = static_cast<int>(std::round(lakeWater));
        basin.channelBedY = basin.channelWaterY - 2;
        basin.channelWeight = std::max(
            basin.channelWeight, basin.lakeWeight);
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
    result.waterLevel = river && basin.channelWaterY != 0
        ? basin.channelWaterY
        : (deepOcean ? Config::SEA_LEVEL + 3 : Config::SEA_LEVEL);
    result.mountainFactor = mountain;
    float complexWeight = 0.0f;
    for (size_t i = 0; i < weights.size(); ++i) {
        if (candidates[i].type == TerrainArchetype::COASTAL_CLIFFS ||
            candidates[i].type == TerrainArchetype::KARST_TOWERS)
            complexWeight += weights[i];
    }
    result.densityWeight = clamp01(complexWeight *
        smoothstep(0.24f, 0.58f, ridge));
    result.densityWeight = std::max(result.densityWeight,
        volcanicWeight * smoothstep(0.12f, 0.48f, ridge));
    if (result.densityWeight > 0.0f)
        result.mountainFactor = std::max(result.mountainFactor,
                                         result.densityWeight * 0.72f);
    result.slope = 0.0f;
    result.localRelief = 0;
    result.riverWeight = riverWeight;
    result.densityMinY = finalHeight - static_cast<int>(std::round(
        28.0f * result.densityWeight));
    result.densityMaxY = finalHeight + static_cast<int>(std::round(
        32.0f * result.densityWeight));
    result.archetype = volcanicWeight > 0.55f
        ? TerrainArchetype::VOLCANIC_CALDERA : candidates[0].type;
    result.secondaryArchetype = candidates[1].type;
    result.archetypeBlend = weights[1] / (weights[0] + weights[1]);
    result.primaryArchetypeWeight = weights[0];
    result.volcanicWeight = volcanicWeight;
    result.craterWeight = craterWeight;
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
    const float overhangMask = column.densityWeight;
    if (overhangMask <= 0.0f) return worldY <= column.nominalHeight;
    float x = static_cast<float>(worldX);
    float y = static_cast<float>(worldY);
    float z = static_cast<float>(worldZ);
    m_surfaceWarp.DomainWarp(x, y, z);
    const float noise = m_surfaceDensity.GetNoise(x, y * 0.82f, z);
    const float vertical = static_cast<float>(column.nominalHeight - worldY);
    return vertical + noise * (4.0f + 15.0f * overhangMask) > 0.0f;
}

SurfaceColumn HeightPipeline::sampleShapedColumn(int worldX, int worldZ) const {
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

void HeightPipeline::applySlope(
    SurfaceColumn& center, const std::array<SurfaceColumn, 8>& neighbors) {
    int relief = 0;
    for (const SurfaceColumn& neighbor : neighbors)
        relief = std::max(relief, std::abs(neighbor.height - center.height));
    const float dx = static_cast<float>(neighbors[4].height -
                                        neighbors[3].height) * 0.5f;
    const float dz = static_cast<float>(neighbors[6].height -
                                        neighbors[1].height) * 0.5f;
    constexpr float halfPi = 1.57079632679f;
    center.localRelief = relief;
    center.slope = clamp01(std::atan(std::sqrt(dx * dx + dz * dz)) / halfPi);
}

SurfaceColumn HeightPipeline::sampleColumn(int worldX, int worldZ) const {
    struct PointCacheEntry {
        const HeightPipeline* owner = nullptr;
        uint64_t seed = 0;
        int x = 0;
        int z = 0;
        SurfaceColumn column;
    };
    thread_local std::array<PointCacheEntry, 8> pointCache;
    thread_local size_t nextPointCache = 0;
    for (const PointCacheEntry& entry : pointCache) {
        if (entry.owner == this && entry.seed == m_context.seed() &&
            entry.x == worldX && entry.z == worldZ)
            return entry.column;
    }

    static constexpr std::array<std::array<int, 2>, 8> offsets{{
        {{-1, -1}}, {{0, -1}}, {{1, -1}}, {{-1, 0}},
        {{1, 0}}, {{-1, 1}}, {{0, 1}}, {{1, 1}}
    }};
    SurfaceColumn center = sampleShapedColumn(worldX, worldZ);
    std::array<SurfaceColumn, 8> neighbors;
    for (size_t i = 0; i < offsets.size(); ++i) {
        neighbors[i] = sampleShapedColumn(
            worldX + offsets[i][0], worldZ + offsets[i][1]);
    }
    applySlope(center, neighbors);
    PointCacheEntry& entry = pointCache[nextPointCache++ % pointCache.size()];
    entry = {this, m_context.seed(), worldX, worldZ, center};
    return center;
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
    const int scratchWidth = width + 2;
    const int scratchDepth = depth + 2;
    std::vector<SurfaceColumn> scratch(
        static_cast<size_t>(scratchWidth) * scratchDepth);
    for (int lz = 0; lz < scratchDepth; ++lz) {
        for (int lx = 0; lx < scratchWidth; ++lx) {
            scratch[static_cast<size_t>(lz) * scratchWidth + lx] =
                sampleShapedColumn(worldOriginX + lx - padding - 1,
                                   worldOriginZ + lz - padding - 1);
        }
    }
    for (int lz = 0; lz < depth; ++lz) {
        for (int lx = 0; lx < width; ++lx) {
            SurfaceColumn sample = scratch[
                static_cast<size_t>(lz + 1) * scratchWidth + lx + 1];
            std::array<SurfaceColumn, 8> neighbors{{
                scratch[static_cast<size_t>(lz) * scratchWidth + lx],
                scratch[static_cast<size_t>(lz) * scratchWidth + lx + 1],
                scratch[static_cast<size_t>(lz) * scratchWidth + lx + 2],
                scratch[static_cast<size_t>(lz + 1) * scratchWidth + lx],
                scratch[static_cast<size_t>(lz + 1) * scratchWidth + lx + 2],
                scratch[static_cast<size_t>(lz + 2) * scratchWidth + lx],
                scratch[static_cast<size_t>(lz + 2) * scratchWidth + lx + 1],
                scratch[static_cast<size_t>(lz + 2) * scratchWidth + lx + 2]
            }};
            applySlope(sample, neighbors);
            auto& out = columnsOut[static_cast<size_t>(lz) * width + lx];
            out.height = sample.height;
            out.nominalHeight = sample.nominalHeight;
            out.mountainFactor = sample.mountainFactor;
            out.slope = sample.slope;
            out.localRelief = sample.localRelief;
            out.riverWeight = sample.riverWeight;
            out.densityWeight = sample.densityWeight;
            out.primaryArchetypeWeight = sample.primaryArchetypeWeight;
            out.volcanicWeight = sample.volcanicWeight;
            out.craterWeight = sample.craterWeight;
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
    std::array<RegionGenerationData::ColumnInfo, 16 * 16> columns;
    computePaddedRegion(chunkWorldX, chunkWorldZ, 16, 16, 0,
                        columns.data());
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            const auto& sample = columns[static_cast<size_t>(z) * 16 + x];
            heightOut[x][z] = sample.height;
            biomeOut[x][z] = sample.biome;
            riverOut[x][z] = sample.isRiver;
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
