#include "world/WorldGenerator.h"
#include "Config.h"
#include "world/RegionGenerator.h"
#include "world/SurfaceRules.h"
#include "world/WorldGenContext.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace {

constexpr uint64_t HEAVEN_SEED_DOMAIN = 0x484556454E5F5345ULL;
constexpr uint64_t HEAVEN_ISLAND_DOMAIN = 0x48454156454E4953ULL;
constexpr uint64_t HEAVEN_LANDMARK_DOMAIN = 0x4845565255494E53ULL;
constexpr uint64_t HEAVEN_DECOR_DOMAIN = 0x48455645434F4C4FULL;
constexpr uint64_t HEAVEN_LAYER1_DOMAIN = 0x4845564C41594531ULL;
constexpr uint64_t HEAVEN_LAYER2_DOMAIN = 0x4845564C41594532ULL;
constexpr uint64_t HEAVEN_LAYER4_DOMAIN = 0x4845564C41594534ULL;
constexpr uint64_t HEAVEN_LAYER5_DOMAIN = 0x4845564C41594535ULL;
constexpr uint64_t HEAVEN_GEODE_DOMAIN = 0x48455647454F4445ULL;
constexpr uint64_t HEAVEN_SPIRE_DOMAIN = 0x4845565350495245ULL;
constexpr int HEAVEN_LANDMARK_CELL = 192;
constexpr int HEAVEN_GEODE_CELL = 160;
constexpr int HEAVEN_SPIRE_CELL = 160;

// Shared field scales.  Every layer samples world coordinates directly, so
// boundaries cannot change output; the periods are far beyond the explored
// Heaven window and negative coordinates stay deterministic.
constexpr float HEAVEN_UNDERSIDE_SCALE = 0.008f;
constexpr float HEAVEN_SPIKE_SCALE = 0.018f;

float smoothstep(float edge0, float edge1, float value) {
    if (edge0 == edge1) return value < edge0 ? 0.0f : 1.0f;
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

int floorDiv(int value, int divisor) {
    const int quotient = value / divisor;
    return value % divisor < 0 ? quotient - 1 : quotient;
}

int heavenBiomeBand(int worldX, int worldZ, int layer) {
    // Broad, world-coordinate 256-cell bands guarantee that a normal
    // exploration window encounters every exclusive biome.  The per-layer
    // shift makes stacked islands sample different biomes while retaining
    // deterministic boundaries across chunks and negative coordinates.
    constexpr int cellSize = 256;
    int band = floorDiv(worldX + layer * 97, cellSize) +
               2 * floorDiv(worldZ + layer * 53, cellSize);
    band %= WorldGenerator::HEAVEN_BIOME_COUNT;
    return band < 0 ? band + WorldGenerator::HEAVEN_BIOME_COUNT : band;
}

Biome heavenCompatBiome(WorldGenerator::HeavenBiome biome) {
    // The shared Biome enum keeps its 30 overworld values; Heaven columns
    // only ever carry these aliases for weather/locate compatibility.  The
    // visible surface, decoration, and generation language come exclusively
    // from HeavenBiome.
    switch (biome) {
        case WorldGenerator::HeavenBiome::DawnMeadow: return Biome::PLAINS;
        case WorldGenerator::HeavenBiome::SkyrootGrove: return Biome::FOREST;
        case WorldGenerator::HeavenBiome::SunstoneHeights: return Biome::MOUNTAINS;
        case WorldGenerator::HeavenBiome::StarCrystalGarden: return Biome::FLOWER_FOREST;
        case WorldGenerator::HeavenBiome::CloudbloomFields: return Biome::MEADOW;
        case WorldGenerator::HeavenBiome::SkystoneBarrens: return Biome::STONY_SHORE;
        case WorldGenerator::HeavenBiome::GlimmerFen: return Biome::SWAMP;
        case WorldGenerator::HeavenBiome::MoonpearlTerrace: return Biome::MOUNTAINS;
    }
    return Biome::PLAINS;
}

struct HeavenLayerProfile {
    int layer = 0;
    uint64_t domain = 0;
    float maskScale = 0.0055f;
    float detailScale = 0.014f;
    float broadScale = 0.0f;   // ≤0 disables the third field
    float presentThreshold = 0.10f;
    int topMin = 92;
    int topMax = 120;
    float topBase = 106.0f;
    float topAmplitude = 14.0f;
    float topScale = 0.0045f;
    float reliefScale = 0.012f;
    float reliefAmplitude = 2.0f;
    int baseY = 80;            // hard bottom clamp, keeps layers separated
    int minDepth = 4;
    int maxDepth = 10;
    int maxSpikeDepth = 0;
    float warpScale = 0.0f;    // L3 keeps the warped macro field
};

constexpr std::array<HeavenLayerProfile, 5> HEAVEN_LAYERS = {{
    // L1 浮屿浅滩 — common low drift shoals
    {0, HEAVEN_LAYER1_DOMAIN, 0.0075f, 0.023f, 0.0f, 0.02f,
     92, 120, 106.0f, 14.0f, 0.0045f, 0.012f, 2.0f, 80, 4, 10, 0, 0.0f},
    // L2 云间群岛 — mid-level isles
    {1, HEAVEN_LAYER2_DOMAIN, 0.0068f, 0.021f, 0.0f, 0.06f,
     136, 164, 150.0f, 14.0f, 0.0045f, 0.011f, 2.0f, 124, 6, 14, 0, 0.0f},
    // L3 主岛群 — keeps the warped macro + detail + broad composition and
    // stays the spawn layer with trees, ruins, geode, and tower anchors.
    {2, HEAVEN_ISLAND_DOMAIN, 0.0055f, 0.014f, 0.0022f, 0.10f,
     184, 216, 200.0f, 16.0f, 0.0045f, 0.010f, 3.0f, 172, 6, 44, 8, 0.0028f},
    // L4 高空群岛 — sparse high isles (replaces the old satellite band)
    {3, HEAVEN_LAYER4_DOMAIN, 0.0065f, 0.021f, 0.0f, 0.16f,
     232, 264, 248.0f, 16.0f, 0.0045f, 0.010f, 2.0f, 220, 5, 12, 0, 0.0f},
    // L5 天顶孤岛 — rarest, near the build limit
    {4, HEAVEN_LAYER5_DOMAIN, 0.0062f, 0.022f, 0.0f, 0.28f,
     280, 308, 294.0f, 14.0f, 0.0045f, 0.009f, 2.0f, 268, 4, 9, 0, 0.0f},
}};

uint64_t dimensionSeed(uint64_t seed, DimensionId dimension) {
    return dimension == DimensionId::Heaven
        ? WorldGenContext(seed).derive(HEAVEN_SEED_DOMAIN) : seed;
}

bool inChunk(int worldX, int worldZ, int baseX, int baseZ) {
    return worldX >= baseX && worldX < baseX + Config::CHUNK_SIZE_X &&
           worldZ >= baseZ && worldZ < baseZ + Config::CHUNK_SIZE_Z;
}

int hashPercent(uint64_t seed, int x, int y, int z) {
    return static_cast<int>(WorldGenContext::hashPosition(seed, x, y, z) % 100u);
}

} // namespace

WorldGenerator::WorldGenerator(
    uint64_t seed, WorldType worldType, DimensionId dimension)
    : m_seed(dimensionSeed(seed, dimension))
    , m_worldType(worldType)
    , m_dimension(dimension)
    , m_noise(m_seed)
    , m_heightPipeline(m_noise, m_seed)
    , m_caveGenerator(m_noise, m_seed)
    , m_treeGenerator(m_seed)
    , m_oreGenerator(m_noise, m_seed)
    , m_structureGenerator(m_seed, m_heightPipeline)
{}

// ═══════════════════════════════════════════════════════════════════════════
// Height query (for spawn placement etc.)
// ═══════════════════════════════════════════════════════════════════════════

int WorldGenerator::getTerrainHeight(int worldX, int worldZ) const {
    if (isHeaven()) return sampleHeavenIsland(worldX, worldZ).top;
    if (m_worldType == WorldType::Superflat)
        return Config::WORLD_MIN_Y + 3;
    float wx = static_cast<float>(worldX);
    float wz = static_cast<float>(worldZ);
    float h = m_heightPipeline.queryHeight(wx, wz);
    return static_cast<int>(std::round(h));
}

HeightBiome WorldGenerator::queryHeightBiome(int worldX, int worldZ) const {
    if (isHeaven()) {
        const HeavenIslandColumn island = sampleHeavenIsland(worldX, worldZ);
        return {island.top, heavenCompatBiome(island.biome)};
    }
    if (m_worldType == WorldType::Superflat)
        return {Config::WORLD_MIN_Y + 3, Biome::PLAINS};
    return m_heightPipeline.queryHeightBiome(
        static_cast<float>(worldX), static_cast<float>(worldZ));
}

SurfaceColumn WorldGenerator::superflatColumn() {
    SurfaceColumn column;
    column.height = Config::WORLD_MIN_Y + 3;
    column.nominalHeight = column.height;
    column.waterLevel = Config::WORLD_MIN_Y - 1;
    column.densityMinY = Config::WORLD_MIN_Y;
    column.densityMaxY = column.height;
    column.biome = Biome::PLAINS;
    column.river = false;
    return column;
}

SurfaceColumn WorldGenerator::sampleTerrainColumn(int worldX, int worldZ) const {
    if (isHeaven()) {
        const HeavenIslandColumn island = sampleHeavenIsland(worldX, worldZ);
        SurfaceColumn column;
        column.height = island.top;
        column.nominalHeight = island.top;
        column.waterLevel = Config::WORLD_MIN_Y - 1;
        column.densityMinY = island.present ? island.bottom : Config::WORLD_MIN_Y;
        column.densityMaxY = island.top;
        column.biome = heavenCompatBiome(island.biome);
        column.river = false;
        return column;
    }
    return m_worldType == WorldType::Superflat
        ? superflatColumn() : m_heightPipeline.sampleColumn(worldX, worldZ);
}

std::vector<TreeGenerator::TreePlacement> WorldGenerator::sampleLodTrees(
    int worldOriginX, int worldOriginZ, int width, int depth) const {
    if (isHeaven() || m_worldType == WorldType::Superflat ||
        width <= 0 || depth <= 0) return {};

    std::vector<TreeGenerator::TreePlacement> result =
        m_treeGenerator.generateTreesForArea(
            worldOriginX, worldOriginZ, width, depth,
            [this](int worldX, int worldZ, int& height,
                   Biome& biome, bool& river) {
                const SurfaceColumn column = sampleTerrainColumn(worldX, worldZ);
                height = column.height;
                biome = column.biome;
                river = column.river;
            });
    result.erase(std::remove_if(result.begin(), result.end(),
        [&](const TreeGenerator::TreePlacement& placement) {
            return m_structureGenerator.reservationAt(
                worldOriginX + placement.localX,
                worldOriginZ + placement.localZ);
        }), result.end());
    return result;
}

WorldGenerator::HeavenBiome WorldGenerator::heavenBiomeAt(
    int worldX, int worldZ) const {
    return sampleHeavenIsland(worldX, worldZ).biome;
}

void WorldGenerator::populateSuperflat(Chunk& chunk) {
    const int bedrockY = Config::WORLD_MIN_Y;
    const int dirtTop = bedrockY + 2;
    const int grassY = bedrockY + 3;
    for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
            chunk.blockAt(x, bedrockY, z) = static_cast<uint8_t>(BlockId::BEDROCK);
            for (int y = bedrockY + 1; y <= dirtTop; ++y)
                chunk.blockAt(x, y, z) = static_cast<uint8_t>(BlockId::DIRT);
            chunk.blockAt(x, grassY, z) = static_cast<uint8_t>(BlockId::GRASS);
            chunk.setColumnMaxY(x, z, grassY);
        }
    }
    chunk.finishBulkBlockEdit();
}

WorldGenerator::HeavenIslandColumn WorldGenerator::sampleHeavenLayer(
    int worldX, int worldZ, int layer) const {
    const HeavenLayerProfile& profile = HEAVEN_LAYERS[
        static_cast<size_t>(std::clamp(layer, 0, HEAVEN_LAYER_COUNT - 1))];
    HeavenIslandColumn island;
    island.biome = static_cast<HeavenBiome>(
        heavenBiomeBand(worldX, worldZ, profile.layer));
    const float x = static_cast<float>(worldX);
    const float z = static_cast<float>(worldZ);
    const float layerOffset = static_cast<float>(profile.layer * 37);

    // Island mask: the main layer keeps the warped macro + detail + broad
    // composition that shapes its archipelagos; the other four layers use a
    // two-octave broad/detail pair with independent domains and thresholds.
    // Every field samples world coordinates directly, so boundaries and
    // negative coordinates stay deterministic.
    float field = 0.0f;
    if (profile.warpScale > 0.0f) {
        const float warpX = m_noise.noise2D(
            x * profile.warpScale + 31.0f, z * profile.warpScale - 47.0f);
        const float warpZ = m_noise.noise2D(
            x * profile.warpScale - 83.0f, z * profile.warpScale + 71.0f);
        const float macro = m_noise.octave2D(
            (x + warpX * 72.0f) * profile.maskScale + 113.0f,
            (z + warpZ * 72.0f) * profile.maskScale - 127.0f,
            2, 0.55f, 2.0f);
        const float detail = m_noise.noise2D(
            x * profile.detailScale + 173.0f,
            z * profile.detailScale - 229.0f);
        const float broad = m_noise.noise2D(
            x * profile.broadScale - 401.0f,
            z * profile.broadScale + 311.0f);
        field = macro * 0.76f + detail * 0.20f + broad * 0.10f;
    } else {
        const float domainOffset =
            static_cast<float>(profile.domain & 0xffu);
        const float broadF = m_noise.octave2D(
            x * profile.maskScale + 211.0f + domainOffset,
            z * profile.maskScale - 337.0f - domainOffset,
            2, 0.55f, 2.0f);
        const float detailF = m_noise.noise2D(
            x * profile.detailScale - 419.0f + domainOffset * 0.5f,
            z * profile.detailScale + 503.0f - domainOffset * 0.5f);
        field = broadF * 0.72f + detailF * 0.28f;
    }
    if (field <= profile.presentThreshold) {
        // Void columns still carry a biome for weather/decoration queries
        // but have no terrain height or density range.
        return island;
    }
    island.present = true;

    // The summit height is independent of the footprint.  Its two coherent
    // fields give wide plateaus with gentle slopes instead of a per-island
    // spherical cap, while rounding keeps adjacent walkable columns smooth.
    const float topBase = m_noise.octave2D(
        x * profile.topScale + 521.0f + layerOffset,
        z * profile.topScale - 607.0f - layerOffset,
        2, 0.5f, 2.0f);
    const float topRelief = m_noise.noise2D(
        x * profile.reliefScale - 733.0f + layerOffset,
        z * profile.reliefScale + 809.0f - layerOffset);
    island.top = std::clamp(static_cast<int>(std::lround(
        profile.topBase + topBase * profile.topAmplitude +
        topRelief * profile.reliefAmplitude)),
        profile.topMin, profile.topMax);

    // Interior depth is driven by mask density, not distance to an anchor.
    // The underside field varies the taper and, on the main layer, a sparse
    // spike field gives islands a broken, End-like lower silhouette.
    const float normalizedDensity = std::clamp(
        (field - profile.presentThreshold) /
            (1.0f - profile.presentThreshold),
        0.0f, 1.0f);
    const float interior = smoothstep(0.0f, 0.58f, normalizedDensity);
    const float undersideNoise = 0.5f + 0.5f * m_noise.noise2D(
        x * HEAVEN_UNDERSIDE_SCALE + 947.0f + layerOffset,
        z * HEAVEN_UNDERSIDE_SCALE - 1013.0f - layerOffset);
    const float depthValue = static_cast<float>(profile.minDepth) + interior *
        (static_cast<float>(profile.maxDepth - profile.minDepth) +
         undersideNoise * 4.0f);
    const int depth = std::clamp(static_cast<int>(std::lround(depthValue)),
                                 profile.minDepth, profile.maxDepth);
    int spikeDepth = 0;
    if (profile.maxSpikeDepth > 0) {
        const float spikeNoise = 0.5f + 0.5f * m_noise.noise2D(
            x * HEAVEN_SPIKE_SCALE - 1217.0f,
            z * HEAVEN_SPIKE_SCALE + 1289.0f);
        spikeDepth = spikeNoise > 0.72f
            ? std::min(profile.maxSpikeDepth, static_cast<int>(std::lround(
                (spikeNoise - 0.72f) * 55.0f))) : 0;
    }
    // The hard baseY clamp keeps every layer's block range clear of the band
    // below it, so stacked islands in one column keep an air gap.
    island.bottom = std::min(island.top, std::max(
        profile.baseY, island.top - depth - spikeDepth + 1));
    return island;
}

std::array<WorldGenerator::HeavenIslandColumn,
           WorldGenerator::HEAVEN_LAYER_COUNT>
WorldGenerator::sampleHeavenLayers(int worldX, int worldZ) const {
    std::array<HeavenIslandColumn, HEAVEN_LAYER_COUNT> layers;
    for (int layer = 0; layer < HEAVEN_LAYER_COUNT; ++layer)
        layers[static_cast<size_t>(layer)] =
            sampleHeavenLayer(worldX, worldZ, layer);
    return layers;
}

WorldGenerator::HeavenIslandColumn WorldGenerator::sampleHeavenIsland(
    int worldX, int worldZ) const {
    // The main layer (L3) drives spawn, trees, and landmark anchoring.
    return sampleHeavenLayer(worldX, worldZ, 2);
}

std::optional<LocatedStructure> WorldGenerator::heavenStructureForCell(
    StructureType type, int cellX, int cellZ) const {
    if (!isHeaven() || !isHeavenStructure(type)) return {};

    int cellSize = 0;
    int margin = 0;
    int span = 0;
    int chancePercent = 0;
    uint64_t domain = 0;
    int fallbackLayer = -1;
    int retrySalt = 0;
    switch (type) {
        case StructureType::XiguangRuin:
            cellSize = HEAVEN_LANDMARK_CELL;
            margin = 24;
            span = 144;
            chancePercent = 24;
            domain = HEAVEN_LANDMARK_DOMAIN;
            retrySalt = 2;
            break;
        case StructureType::StarCrystalGeode:
            cellSize = HEAVEN_GEODE_CELL;
            margin = 16;
            span = 128;
            chancePercent = 28;
            domain = HEAVEN_GEODE_DOMAIN;
            fallbackLayer = 1;
            retrySalt = 3;
            break;
        case StructureType::CloudspireTower:
            cellSize = HEAVEN_SPIRE_CELL;
            margin = 16;
            span = 128;
            chancePercent = 20;
            domain = HEAVEN_SPIRE_DOMAIN;
            fallbackLayer = 3;
            retrySalt = 4;
            break;
        default:
            return {};
    }

    const uint64_t cellSeed = WorldGenContext::hashPosition(
        m_seed ^ domain, cellX, 0, cellZ);
    if (cellSeed % 100u >= static_cast<uint64_t>(chancePercent)) return {};

    HeavenIslandColumn anchor;
    int anchorX = 0;
    int anchorZ = 0;
    for (int attempt = 0; attempt < 24; ++attempt) {
        const uint64_t candidateSeed = attempt == 0 ? cellSeed :
            WorldGenContext::hashPosition(cellSeed, attempt, retrySalt, -attempt);
        anchorX = cellX * cellSize + margin +
            static_cast<int>((candidateSeed >> 8) %
                             static_cast<uint64_t>(span));
        anchorZ = cellZ * cellSize + margin +
            static_cast<int>((candidateSeed >> 20) %
                             static_cast<uint64_t>(span));
        anchor = sampleHeavenIsland(anchorX, anchorZ);
        int chosenLayer = 2;

        bool eligible = false;
        if (type == StructureType::XiguangRuin) {
            eligible = anchor.present &&
                anchor.biome != HeavenBiome::SunstoneHeights &&
                anchor.biome != HeavenBiome::MoonpearlTerrace &&
                anchor.biome != HeavenBiome::SkystoneBarrens;
        } else {
            if (!anchor.present ||
                (type == StructureType::StarCrystalGeode &&
                 anchor.biome != HeavenBiome::StarCrystalGarden)) {
                anchor = sampleHeavenLayer(anchorX, anchorZ, fallbackLayer);
                chosenLayer = fallbackLayer;
            }
            eligible = anchor.present &&
                (type != StructureType::StarCrystalGeode ||
                 anchor.biome == HeavenBiome::StarCrystalGarden);
        }
        if (eligible) {
            const int radius = type == StructureType::XiguangRuin ? 6 : 4;
            int supported = 0;
            int sampled = 0;
            for (int dz=-radius; dz<=radius; dz+=2) {
                for (int dx=-radius; dx<=radius; dx+=2) {
                    ++sampled;
                    const HeavenIslandColumn column = sampleHeavenLayer(
                        anchorX+dx, anchorZ+dz, chosenLayer);
                    if (column.present && std::abs(column.top-anchor.top)<=4)
                        ++supported;
                }
            }
            const int required = type == StructureType::CloudspireTower
                ? sampled * 4 / 5 : sampled * 2 / 3;
            if (supported >= required &&
                (type != StructureType::CloudspireTower ||
                 anchor.top + 28 < Config::WORLD_MAX_Y))
                return LocatedStructure{anchorX, anchor.top, anchorZ, type};
        }
    }
    return {};
}

std::optional<LocatedStructure> WorldGenerator::locateNearestHeavenStructure(
    StructureType type, int worldX, int worldZ, int maximumDistance) const {
    if (!isHeaven() || !isHeavenStructure(type) || maximumDistance < 0)
        return {};

    const int cellSize = type == StructureType::XiguangRuin
        ? HEAVEN_LANDMARK_CELL : HEAVEN_GEODE_CELL;
    const int originCellX = floorDiv(worldX, cellSize);
    const int originCellZ = floorDiv(worldZ, cellSize);
    const int maximumRing = maximumDistance / cellSize + 2;
    const int64_t maximumDistanceSquared =
        static_cast<int64_t>(maximumDistance) * maximumDistance;
    int64_t bestDistanceSquared = maximumDistanceSquared + 1;
    std::optional<LocatedStructure> best;

    const auto inspect = [&](int offsetX, int offsetZ) {
        const auto candidate = heavenStructureForCell(
            type, originCellX + offsetX, originCellZ + offsetZ);
        if (!candidate) return;
        const int64_t dx = static_cast<int64_t>(candidate->worldX) - worldX;
        const int64_t dz = static_cast<int64_t>(candidate->worldZ) - worldZ;
        const int64_t distanceSquared = dx * dx + dz * dz;
        if (distanceSquared > maximumDistanceSquared ||
            distanceSquared > bestDistanceSquared)
            return;
        if (distanceSquared == bestDistanceSquared && best &&
            (candidate->worldX > best->worldX ||
             (candidate->worldX == best->worldX &&
              candidate->worldZ >= best->worldZ)))
            return;
        bestDistanceSquared = distanceSquared;
        best = candidate;
    };

    for (int ring = 0; ring <= maximumRing; ++ring) {
        if (ring == 0) {
            inspect(0, 0);
        } else {
            for (int x = -ring; x <= ring; ++x) {
                inspect(x, -ring);
                inspect(x, ring);
            }
            for (int z = -ring + 1; z < ring; ++z) {
                inspect(-ring, z);
                inspect(ring, z);
            }
        }
        if (best) {
            const int64_t unvisitedLowerBound =
                static_cast<int64_t>(ring) * cellSize + 1;
            if (unvisitedLowerBound * unvisitedLowerBound >
                bestDistanceSquared)
                break;
        }
    }
    return best;
}



void WorldGenerator::populateHeaven(
    Chunk& chunk, WorldGenerator& generator,
    const StructureSetter& structureSetter) {
    const int baseX = chunk.worldX();
    const int baseZ = chunk.worldZ();
    const auto setLocal = [&](int worldX, int worldY, int worldZ, BlockId id) {
        if (!inChunk(worldX, worldZ, baseX, baseZ) ||
            !Config::isValidWorldY(worldY)) return;
        const int localX = worldX - baseX;
        const int localZ = worldZ - baseZ;
        chunk.blockAt(localX, worldY, localZ) = static_cast<uint8_t>(id);
    };
    const auto setIfAir = [&](int worldX, int worldY, int worldZ, BlockId id) {
        if (!inChunk(worldX, worldZ, baseX, baseZ) ||
            !Config::isValidWorldY(worldY)) return;
        const int localX = worldX - baseX;
        const int localZ = worldZ - baseZ;
        if (chunk.blockAt(localX, worldY, localZ) ==
            static_cast<uint8_t>(BlockId::AIR))
            chunk.blockAt(localX, worldY, localZ) = static_cast<uint8_t>(id);
    };
    const auto buildStructure = [&](const StructurePlacement& placement) {
        StructureGenerator::build(placement,
            [&](int worldX,int worldY,int worldZ,BlockId id) {
                if (!inChunk(worldX,worldZ,baseX,baseZ) ||
                    !Config::isValidWorldY(worldY)) return;
                setLocal(worldX,worldY,worldZ,id);
                if ((id==BlockId::CHEST || id==BlockId::FURNACE) &&
                    structureSetter)
                    structureSetter(worldX,worldY,worldZ,id,
                        id==BlockId::CHEST
                            ? structureLootProfile(placement.type)
                            : StructureLootProfile::None,
                        WorldGenContext::hashPosition(
                            placement.variant,worldX,worldY,worldZ));
            });
    };
    const auto surfaceBlock = [](HeavenBiome biome) {
        switch (biome) {
            case HeavenBiome::SunstoneHeights:
            case HeavenBiome::MoonpearlTerrace: return BlockId::SUNSTONE;
            case HeavenBiome::StarCrystalGarden:
            case HeavenBiome::GlimmerFen: return BlockId::MOSS;
            case HeavenBiome::SkystoneBarrens: return BlockId::CLOUDSTONE;
            case HeavenBiome::DawnMeadow:
            case HeavenBiome::SkyrootGrove:
            case HeavenBiome::CloudbloomFields: return BlockId::AETHER_GRASS;
        }
        return BlockId::AETHER_GRASS;
    };
    const auto wantsSoil = [](HeavenBiome biome) {
        return biome == HeavenBiome::DawnMeadow ||
               biome == HeavenBiome::SkyrootGrove ||
               biome == HeavenBiome::CloudbloomFields;
    };
    const auto wantsVeins = [](HeavenBiome biome) {
        return biome == HeavenBiome::SunstoneHeights ||
               biome == HeavenBiome::MoonpearlTerrace;
    };
    const auto fillColumn = [&](int worldX, int worldZ,
                                const HeavenIslandColumn& island) {
        if (!island.present) return;
        for (int worldY = island.bottom; worldY <= island.top; ++worldY) {
            BlockId block = BlockId::CLOUDSTONE;
            if (worldY == island.top) {
                block = surfaceBlock(island.biome);
            } else if (wantsSoil(island.biome) && worldY >= island.top - 2) {
                block = BlockId::AETHER_SOIL;
            } else if (wantsVeins(island.biome) &&
                       hashPercent(generator.m_seed ^ HEAVEN_DECOR_DOMAIN,
                                   worldX, worldY, worldZ) < 13) {
                block = BlockId::SUNSTONE;
            }
            setLocal(worldX, worldY, worldZ, block);
        }
    };

    // Five altitude layers sample independently, so one column can hold up to
    // five stacked islands.  Their hard bottom clamps keep an air gap between
    // bands, and every layer carries its own exclusive biome.
    for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
            const int wx = baseX + x;
            const int wz = baseZ + z;
            const auto layers = generator.sampleHeavenLayers(wx, wz);
            for (const HeavenIslandColumn& island : layers)
                fillColumn(wx, wz, island);
        }
    }

    // Skyroot trees are selected in world coordinates on the main layer and
    // each chunk writes only the part of a tree it owns. This keeps region
    // and singleton paths independent of request order while still allowing
    // canopies to cross a chunk boundary.
    for (int worldX = baseX - 5; worldX <= baseX + 20; ++worldX) {
        for (int worldZ = baseZ - 5; worldZ <= baseZ + 20; ++worldZ) {
            const HeavenIslandColumn island =
                generator.sampleHeavenIsland(worldX, worldZ);
            if (!island.present ||
                island.biome != HeavenBiome::SkyrootGrove ||
                hashPercent(generator.m_seed ^ HEAVEN_DECOR_DOMAIN,
                            worldX, 17, worldZ) >= 10)
                continue;
            const int trunkHeight = 5 + hashPercent(
                generator.m_seed ^ HEAVEN_DECOR_DOMAIN, worldX, 23, worldZ) % 4;
            for (int y = island.top + 1; y < island.top + trunkHeight; ++y)
                setIfAir(worldX, y, worldZ, BlockId::SKYROOT_WOOD);
            const int canopyY = island.top + trunkHeight - 1;
            for (int dx = -2; dx <= 2; ++dx) {
                for (int dz = -2; dz <= 2; ++dz) {
                    const int distance = std::abs(dx) + std::abs(dz);
                    if (distance > 3 || (distance == 3 && (dx + dz) % 2 != 0))
                        continue;
                    setIfAir(worldX + dx, canopyY, worldZ + dz,
                             BlockId::SKYROOT_LEAVES);
                    if (distance <= 1)
                        setIfAir(worldX + dx, canopyY + 1, worldZ + dz,
                                 BlockId::SKYROOT_LEAVES);
                }
            }
        }
    }

    // Treeless biomes each carry a small library of coordinate-owned
    // micro-features (rings, patches, boulders, spires, fallen logs) so their
    // surfaces and skyline stay varied without a new simulation system.
    // Every feature uses an independent hash salt and writes only through
    // setLocal/setIfAir, which keeps region and singleton output identical
    // regardless of request order.  Minor layers use a lower feature budget;
    // zenith islets keep their lone-crystal language.
    for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
            const int wx = baseX + x;
            const int wz = baseZ + z;
            const auto layers = generator.sampleHeavenLayers(wx, wz);
            for (int layer = 0; layer < HEAVEN_LAYER_COUNT; ++layer) {
                const HeavenIslandColumn& island =
                    layers[static_cast<size_t>(layer)];
                if (!island.present || island.top + 1 >= Config::WORLD_MAX_Y)
                    continue;
                const int top = island.top;
                const int roll = hashPercent(
                    generator.m_seed ^ HEAVEN_DECOR_DOMAIN,
                    wx, 31 + layer * 7, wz);
                if (layer == HEAVEN_LAYER_COUNT - 1) {
                    // Zenith islets carry a lone star crystal so the top
                    // layer reads clearly from below.
                    if (roll < 60)
                        setIfAir(wx, top + 1, wz, BlockId::STAR_CRYSTAL);
                    continue;
                }
                // Full budget on the main layer, reduced on the drift layers.
                const float layerScale = layer == 2 ? 1.0f : 0.6f;
                const auto rollUnder = [&](int salt, int threshold) {
                    return hashPercent(generator.m_seed ^ HEAVEN_DECOR_DOMAIN,
                                       wx, salt + layer * 11, wz) <
                           static_cast<int>(threshold * layerScale);
                };
                switch (island.biome) {
                    case HeavenBiome::DawnMeadow:
                        if (rollUnder(41, 4)) {
                            // Starflower ring with a centre bloom.
                            setIfAir(wx, top + 1, wz, BlockId::STARFLOWER);
                            for (int dx = -2; dx <= 2; ++dx)
                                for (int dz = -2; dz <= 2; ++dz)
                                    if (std::max(std::abs(dx), std::abs(dz)) == 2)
                                        setIfAir(wx + dx, top + 1, wz + dz,
                                                 BlockId::STARFLOWER);
                        } else if (rollUnder(53, 9)) {
                            setLocal(wx, top, wz, BlockId::MOSS);
                            setLocal(wx + 1, top, wz, BlockId::MOSS);
                            setLocal(wx, top, wz + 1, BlockId::MOSS);
                            setLocal(wx + 1, top, wz + 1, BlockId::MOSS);
                        } else if (rollUnder(61, 2)) {
                            for (int i = 0; i < 3; ++i)
                                setIfAir(wx + i, top + 1, wz,
                                         BlockId::SKYROOT_WOOD);
                            setIfAir(wx + 1, top + 2, wz,
                                     BlockId::SKYROOT_LEAVES);
                        } else if (roll < static_cast<int>(20 * layerScale)) {
                            setIfAir(wx, top + 1, wz, BlockId::STARFLOWER);
                        }
                        break;
                    case HeavenBiome::SunstoneHeights:
                        if (rollUnder(41, 5)) {
                            const int height = 1 + hashPercent(
                                generator.m_seed ^ HEAVEN_DECOR_DOMAIN,
                                wx, 71 + layer * 11, wz) % 3;
                            for (int y = 1; y <= height; ++y)
                                setIfAir(wx, top + y, wz, BlockId::SUNSTONE);
                        } else if (rollUnder(53, 8)) {
                            for (int dx = 0; dx <= 1; ++dx)
                                for (int dz = 0; dz <= 1; ++dz)
                                    for (int y = 1; y <= 2; ++y)
                                        setIfAir(wx + dx, top + y, wz + dz,
                                                 BlockId::SUNSTONE);
                        } else if (rollUnder(61, 10)) {
                            setIfAir(wx, top + 1, wz, BlockId::STAR_CRYSTAL);
                        }
                        break;
                    case HeavenBiome::StarCrystalGarden:
                        if (rollUnder(41, 3)) {
                            const int height = 2 + hashPercent(
                                generator.m_seed ^ HEAVEN_DECOR_DOMAIN,
                                wx, 71 + layer * 11, wz) % 2;
                            for (int y = 1; y <= height; ++y)
                                setIfAir(wx, top + y, wz,
                                         BlockId::STAR_CRYSTAL);
                        } else if (rollUnder(53, 6)) {
                            setIfAir(wx, top + 1, wz, BlockId::MOSS);
                            setIfAir(wx + 1, top + 1, wz, BlockId::MOSS);
                            setIfAir(wx, top + 1, wz + 1, BlockId::MOSS);
                            setIfAir(wx + 1, top + 1, wz + 1, BlockId::MOSS);
                            setIfAir(wx, top + 2, wz, BlockId::MOSS);
                            setIfAir(wx + 1, top + 2, wz + 1, BlockId::MOSS);
                        } else if (rollUnder(61, 14)) {
                            setIfAir(wx, top + 1, wz, BlockId::STARFLOWER);
                            setIfAir(wx + 1, top + 1, wz, BlockId::STARFLOWER);
                            setIfAir(wx, top + 1, wz + 1, BlockId::STARFLOWER);
                        } else if (roll < static_cast<int>(9 * layerScale)) {
                            setIfAir(wx, top + 1, wz, BlockId::STAR_CRYSTAL);
                            if (roll < static_cast<int>(4 * layerScale)) {
                                setIfAir(wx + 1, top + 1, wz,
                                         BlockId::STAR_CRYSTAL);
                                setIfAir(wx, top + 1, wz + 1,
                                         BlockId::STAR_CRYSTAL);
                            }
                        } else if (roll < static_cast<int>(20 * layerScale)) {
                            setIfAir(wx, top + 1, wz, BlockId::STARFLOWER);
                        }
                        break;
                    case HeavenBiome::CloudbloomFields:
                        if (rollUnder(41, 5)) {
                            setIfAir(wx, top + 1, wz, BlockId::CLOUDSTONE);
                            setIfAir(wx + 1, top + 1, wz, BlockId::CLOUDSTONE);
                            setIfAir(wx, top + 1, wz + 1, BlockId::CLOUDSTONE);
                            setIfAir(wx + 1, top + 1, wz + 1, BlockId::CLOUDSTONE);
                        } else if (rollUnder(53, 8)) {
                            setLocal(wx, top, wz, BlockId::MOSS);
                            setLocal(wx + 1, top, wz, BlockId::MOSS);
                            setLocal(wx, top, wz + 1, BlockId::MOSS);
                            setLocal(wx + 1, top, wz + 1, BlockId::MOSS);
                        } else if (roll < static_cast<int>(22 * layerScale)) {
                            setIfAir(wx, top + 1, wz, BlockId::CLOUD_BLOOM);
                            if (rollUnder(61, 40))
                                setIfAir(wx + 1, top + 1, wz,
                                         BlockId::CLOUD_BLOOM);
                            if (rollUnder(71, 40))
                                setIfAir(wx, top + 1, wz + 1,
                                         BlockId::CLOUD_BLOOM);
                            if (rollUnder(81, 20)) {
                                setIfAir(wx + 1, top + 1, wz + 1,
                                         BlockId::CLOUD_BLOOM);
                                setIfAir(wx - 1, top + 1, wz,
                                         BlockId::CLOUD_BLOOM);
                            }
                        }
                        break;
                    case HeavenBiome::SkystoneBarrens:
                        if (rollUnder(41, 7)) {
                            const int height = 2 + hashPercent(
                                generator.m_seed ^ HEAVEN_DECOR_DOMAIN,
                                wx, 71 + layer * 11, wz) % 3;
                            for (int y = 1; y <= height; ++y)
                                setIfAir(wx, top + y, wz, BlockId::SUNSTONE);
                        } else if (rollUnder(53, 5)) {
                            setIfAir(wx, top + 1, wz, BlockId::CLOUDSTONE);
                            setIfAir(wx + 1, top + 1, wz, BlockId::CLOUDSTONE);
                            setIfAir(wx, top + 1, wz + 1, BlockId::CLOUDSTONE);
                            setIfAir(wx + 1, top + 1, wz + 1, BlockId::CLOUDSTONE);
                        } else if (rollUnder(61, 6)) {
                            setLocal(wx, top, wz, BlockId::AETHER_SOIL);
                            setLocal(wx + 1, top, wz, BlockId::AETHER_SOIL);
                            setLocal(wx, top, wz + 1, BlockId::AETHER_SOIL);
                            setLocal(wx + 1, top, wz + 1, BlockId::AETHER_SOIL);
                        } else if (roll < static_cast<int>(9 * layerScale)) {
                            setIfAir(wx, top + 1, wz, BlockId::STAR_CRYSTAL);
                        }
                        break;
                    case HeavenBiome::GlimmerFen:
                        if (rollUnder(41, 3)) {
                            setIfAir(wx, top + 1, wz, BlockId::GLOWSHROOM);
                            for (int dx = -2; dx <= 2; ++dx)
                                for (int dz = -2; dz <= 2; ++dz)
                                    if (std::max(std::abs(dx), std::abs(dz)) == 2)
                                        setIfAir(wx + dx, top + 1, wz + dz,
                                                 BlockId::GLOWSHROOM);
                        } else if (rollUnder(53, 6)) {
                            setIfAir(wx, top + 1, wz, BlockId::MOSS);
                            setIfAir(wx + 1, top + 1, wz, BlockId::MOSS);
                            setIfAir(wx, top + 1, wz + 1, BlockId::MOSS);
                            setIfAir(wx + 1, top + 1, wz + 1, BlockId::MOSS);
                            setIfAir(wx, top + 2, wz, BlockId::MOSS);
                            setIfAir(wx + 1, top + 2, wz + 1, BlockId::MOSS);
                        } else if (rollUnder(61, 12)) {
                            setIfAir(wx, top + 1, wz, BlockId::STAR_CRYSTAL);
                        } else if (roll < static_cast<int>(12 * layerScale)) {
                            setIfAir(wx, top + 1, wz, BlockId::GLOWSHROOM);
                            if (roll < static_cast<int>(5 * layerScale)) {
                                setIfAir(wx + 1, top + 1, wz,
                                         BlockId::GLOWSHROOM);
                                setIfAir(wx, top + 1, wz + 1,
                                         BlockId::GLOWSHROOM);
                                setIfAir(wx + 1, top + 1, wz + 1,
                                         BlockId::GLOWSHROOM);
                            }
                        }
                        break;
                    case HeavenBiome::MoonpearlTerrace:
                        if (rollUnder(41, 2)) {
                            setIfAir(wx, top + 1, wz, BlockId::STAR_CRYSTAL);
                            for (int dx = -2; dx <= 2; ++dx)
                                for (int dz = -2; dz <= 2; ++dz)
                                    if (std::max(std::abs(dx), std::abs(dz)) == 2)
                                        setIfAir(wx + dx, top + 1, wz + dz,
                                                 BlockId::SUNSTONE);
                        } else if (rollUnder(53, 8)) {
                            setIfAir(wx, top + 1, wz, BlockId::STARFLOWER);
                            setIfAir(wx + 1, top + 1, wz, BlockId::STARFLOWER);
                            setIfAir(wx, top + 1, wz + 1, BlockId::STARFLOWER);
                        } else if (rollUnder(61, 6)) {
                            setLocal(wx, top, wz, BlockId::AETHER_SOIL);
                            setLocal(wx + 1, top, wz, BlockId::AETHER_SOIL);
                            setLocal(wx, top, wz + 1, BlockId::AETHER_SOIL);
                            setLocal(wx + 1, top, wz + 1, BlockId::AETHER_SOIL);
                        } else if (roll < static_cast<int>(11 * layerScale)) {
                            setIfAir(wx, top + 1, wz, BlockId::STARFLOWER);
                        } else if (roll < static_cast<int>(16 * layerScale)) {
                            setIfAir(wx, top + 1, wz, BlockId::STAR_CRYSTAL);
                        }
                        break;
                    case HeavenBiome::SkyrootGrove:
                        break;
                }
            }
        }
    }

    // Rare coordinate-owned shrines provide a long-distance landmark. The
    // anchor is selected from a wide cell, but every piece is written through
    // setIfAir so the exact same structure appears regardless of chunk order.
    const int firstCellX = floorDiv(baseX - 9, HEAVEN_LANDMARK_CELL);
    const int lastCellX = floorDiv(baseX + 24, HEAVEN_LANDMARK_CELL);
    const int firstCellZ = floorDiv(baseZ - 9, HEAVEN_LANDMARK_CELL);
    const int lastCellZ = floorDiv(baseZ + 24, HEAVEN_LANDMARK_CELL);
    for (int cellX = firstCellX; cellX <= lastCellX; ++cellX) {
        for (int cellZ = firstCellZ; cellZ <= lastCellZ; ++cellZ) {
            const uint64_t cellSeed = WorldGenContext::hashPosition(
                generator.m_seed ^ HEAVEN_LANDMARK_DOMAIN, cellX, 0, cellZ);
            const auto placement = generator.heavenStructureForCell(
                StructureType::XiguangRuin, cellX, cellZ);
            if (!placement) continue;
            const int ax=placement->worldX, az=placement->worldZ;
            buildStructure({0,0,placement->baseY,
                StructureType::XiguangRuin,cellSeed,
                ax-9,ax+9,az-9,az+9});
        }
    }

    // Crystal geode: a floating moss ring with a stacked crystal core marks
    // Starcrystal Garden anchors on the low and main island layers.
    const int firstGeodeCellX = floorDiv(baseX - 5, HEAVEN_GEODE_CELL);
    const int lastGeodeCellX = floorDiv(baseX + 20, HEAVEN_GEODE_CELL);
    const int firstGeodeCellZ = floorDiv(baseZ - 5, HEAVEN_GEODE_CELL);
    const int lastGeodeCellZ = floorDiv(baseZ + 20, HEAVEN_GEODE_CELL);
    for (int cellX = firstGeodeCellX; cellX <= lastGeodeCellX; ++cellX) {
        for (int cellZ = firstGeodeCellZ; cellZ <= lastGeodeCellZ; ++cellZ) {
            const auto placement = generator.heavenStructureForCell(
                StructureType::StarCrystalGeode, cellX, cellZ);
            if (!placement) continue;
            const uint64_t cellSeed=WorldGenContext::hashPosition(
                generator.m_seed^HEAVEN_GEODE_DOMAIN,cellX,0,cellZ);
            const int ax=placement->worldX, az=placement->worldZ;
            buildStructure({0,0,placement->baseY,
                StructureType::StarCrystalGeode,cellSeed,
                ax-5,ax+5,az-5,az+5});
        }
    }

    // Cloudspire tower: a sunstone shaft with a cloudstone cap and crystal
    // finial marks main and high island anchors.  Any biome is eligible so
    // that a normal exploration window reliably discovers the tower; the
    // cap-over-shaft order keeps test probes distinct from the ruin and
    // geode patterns.
    const int firstSpireCellX = floorDiv(baseX - 6, HEAVEN_SPIRE_CELL);
    const int lastSpireCellX = floorDiv(baseX + 21, HEAVEN_SPIRE_CELL);
    const int firstSpireCellZ = floorDiv(baseZ - 6, HEAVEN_SPIRE_CELL);
    const int lastSpireCellZ = floorDiv(baseZ + 21, HEAVEN_SPIRE_CELL);
    for (int cellX = firstSpireCellX; cellX <= lastSpireCellX; ++cellX) {
        for (int cellZ = firstSpireCellZ; cellZ <= lastSpireCellZ; ++cellZ) {
            const uint64_t cellSeed = WorldGenContext::hashPosition(
                generator.m_seed ^ HEAVEN_SPIRE_DOMAIN, cellX, 0, cellZ);
            const auto placement = generator.heavenStructureForCell(
                StructureType::CloudspireTower, cellX, cellZ);
            if (!placement) continue;
            const int ax=placement->worldX, az=placement->worldZ;
            buildStructure({0,0,placement->baseY,
                StructureType::CloudspireTower,cellSeed,
                ax-6,ax+6,az-6,az+6});
        }
    }

    for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
            int maxY = Config::WORLD_MIN_Y - 1;
            for (int y = Config::WORLD_MAX_Y - 1;
                 y >= Config::WORLD_MIN_Y; --y) {
                if (chunk.blockAt(x, y, z) != static_cast<uint8_t>(BlockId::AIR)) {
                    maxY = y;
                    break;
                }
            }
            chunk.setColumnMaxY(x, z, maxY);
        }
    }
    chunk.finishBulkBlockEdit();
}

void WorldGenerator::generateRegion(
    int originCX, int originCZ, int regionSizeChunks, int padding,
    std::vector<Chunk*>& chunks,
    std::vector<RegionGenerationData::PendingBlock>& pendingOut) {
    if (isHeaven()) {
        pendingOut.clear();
        const StructureSetter structureSetter =
            [&](int wx,int wy,int wz,BlockId id,
                StructureLootProfile lootProfile,uint64_t lootSeed) {
                pendingOut.push_back({wx,wy,wz,id,true,true,
                                      lootProfile,lootSeed});
            };
        for (Chunk* chunk : chunks) {
            if (chunk == nullptr) continue;
            populateHeaven(*chunk, *this, structureSetter);
            chunk->generated = true;
            chunk->generationInProgress = false;
            chunk->markDirty();
        }
        return;
    }
    if (m_worldType == WorldType::Superflat) {
        for (Chunk* chunk : chunks) {
            if (chunk == nullptr) continue;
            populateSuperflat(*chunk);
            chunk->generated = true;
            chunk->generationInProgress = false;
            chunk->markDirty();
        }
        pendingOut.clear();
        return;
    }
    RegionGenerator regionGenerator(
        m_heightPipeline, m_caveGenerator, m_treeGenerator, m_oreGenerator, m_seed);
    regionGenerator.generateRegion(originCX, originCZ, regionSizeChunks, padding,
                                   chunks, pendingOut);
}

// ═══════════════════════════════════════════════════════════════════════════
// Main generation
// ═══════════════════════════════════════════════════════════════════════════

void WorldGenerator::generate(Chunk& chunk,
                               const NeighborQuery& neighborQuery,
                               const BlockSetter& blockSetter,
                               const StructureSetter& structureSetter) {
    if (isHeaven()) {
        populateHeaven(chunk, *this, structureSetter);
        return;
    }
    if (m_worldType == WorldType::Superflat) {
        populateSuperflat(chunk);
        return;
    }
    (void)neighborQuery;
    int wxBase = chunk.worldX();
    int wzBase = chunk.worldZ();

    // Phase 1: Height / Biome / River maps
    int   heightMap[16][16];
    Biome biomeMap[16][16];
    bool  riverMap[16][16];
    std::vector<RegionGenerationData::ColumnInfo> terrainColumns(16 * 16);
    m_heightPipeline.computePaddedRegion(
        wxBase, wzBase, 16, 16, 0, terrainColumns.data());

    // ── Phase 2: Fill blocks ────────────────────────────────────────────
    for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
            const int worldX = wxBase + x, worldZ = wzBase + z;
            const auto& column = terrainColumns[
                static_cast<size_t>(z) * Config::CHUNK_SIZE_X + x];
            int   height = column.height;
            Biome biome  = column.biome;
            heightMap[x][z] = height;
            biomeMap[x][z] = biome;
            riverMap[x][z] = column.isRiver;
            const BiomeProperties& bprops = getBiomeProps(biome);
            SurfaceRuleContext surfaceContext{
                biome, column.archetype, height, column.waterLevel,
                column.slope, column.localRelief,
                column.primaryArchetypeWeight, column.volcanicWeight,
                column.craterWeight, column.riverWeight, column.isRiver
            };
            surfaceContext.secondaryArchetype = column.secondaryArchetype;
            surfaceContext.secondaryArchetypeWeight = column.archetypeBlend;
            SurfaceProfile surface = SurfaceRules::profile(
                m_seed, worldX, worldZ, surfaceContext);

            SurfaceColumn terrainColumn;
            terrainColumn.height = column.height;
            terrainColumn.nominalHeight = column.nominalHeight;
            terrainColumn.mountainFactor = column.mountainFactor;
            terrainColumn.slope = column.slope;
            terrainColumn.localRelief = column.localRelief;
            terrainColumn.riverWeight = column.riverWeight;
            terrainColumn.densityWeight = column.densityWeight;
            terrainColumn.primaryArchetypeWeight =
                column.primaryArchetypeWeight;
            terrainColumn.volcanicWeight = column.volcanicWeight;
            terrainColumn.craterWeight = column.craterWeight;
            terrainColumn.densityMinY = column.densityMinY;
            terrainColumn.densityMaxY = column.densityMaxY;
            terrainColumn.archetype = column.archetype;
            terrainColumn.secondaryArchetype = column.secondaryArchetype;
            terrainColumn.archetypeBlend = column.archetypeBlend;
            terrainColumn.basin = column.basin;

            const int bedrockTop = Config::WORLD_MIN_Y + static_cast<int>(
                WorldGenContext::hashPosition(m_seed, worldX, 0, worldZ) % 5);
            for (int y = Config::WORLD_MIN_Y; y <= bedrockTop; ++y) {
                chunk.blockAt(x, y, z) = static_cast<uint8_t>(BlockId::BEDROCK);
            }

            for (int y = bedrockTop + 1; y <= height; ++y) {
                if (!m_heightPipeline.isTerrainSolid(worldX, y, worldZ, terrainColumn)) continue;
                const bool deepslate = y <= 0 || (y < Config::DEEPSLATE_DEPTH &&
                    WorldGenContext::hashPosition(m_seed, worldX, y, worldZ) %
                        Config::DEEPSLATE_DEPTH >= static_cast<uint64_t>(y));
                chunk.blockAt(x, y, z) = static_cast<uint8_t>(
                    deepslate ? BlockId::DEEPSLATE : BlockId::STONE);
            }
            for (int depth = 0; depth <= surface.depth; ++depth) {
                const int y = height - depth;
                const BlockId current = static_cast<BlockId>(
                    chunk.blockAt(x, y, z));
                if (current != BlockId::STONE && current != BlockId::DEEPSLATE) continue;
                chunk.blockAt(x, y, z) = static_cast<uint8_t>(
                    SurfaceRules::blockAtDepth(
                        m_seed, worldX, worldZ, depth, surfaceContext));
            }

            // Snow cover: if height >= biome snowLine, override surface
            if (height >= bprops.snowLine && bprops.snowLine < Config::SNOW_LINE_DISABLED) {
                chunk.blockAt(x, height, z) =
                    static_cast<uint8_t>(BlockId::SNOW);
            }

            // Water fill (oceans, lakes, rivers)
            int waterTop = column.waterLevel;
            if (height < waterTop) {
                for (int y = height + 1; y <= waterTop; ++y) {
                    if (y < Config::WORLD_MAX_Y) {
                        chunk.blockAt(x, y, z) =
                            static_cast<uint8_t>(BlockId::WATER);
                    }
                }
                // Ice: freeze surface water in cold biomes
                if ((biome == Biome::SNOW_TUNDRA || biome == Biome::TAIGA) &&
                    height + 1 <= Config::ICE_FREEZE_MAX_Y) {
                    chunk.blockAt(x, height + 1, z) =
                        static_cast<uint8_t>(BlockId::ICE);
                }
            }
            chunk.setColumnMaxY(x, z, std::max(height, waterTop));
        }
    }
    chunk.finishBulkBlockEdit();

    // ── Phase 3: Hybrid caves, liquids, then ores ───────────────────────
    std::vector<CaveColumnInfo> caveColumns(
        static_cast<size_t>(Config::CHUNK_SIZE_X) * Config::CHUNK_SIZE_Z);
    for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
        for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
            const auto& column = terrainColumns[
                static_cast<size_t>(z) * Config::CHUNK_SIZE_X + x];
            caveColumns[static_cast<size_t>(z) * Config::CHUNK_SIZE_X + x] = {
                heightMap[x][z], column.waterLevel,
                riverMap[x][z] || heightMap[x][z] < column.waterLevel
            };
        }
    }
    CaveVolume caveVolume = m_caveGenerator.generateVolume(
        wxBase, wzBase, Config::CHUNK_SIZE_X, Config::CHUNK_SIZE_Z, caveColumns);
    for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
            int wx = wxBase + x, wz = wzBase + z;
            for (int y = Config::CAVE_MIN_Y; y < Config::WORLD_MAX_Y; ++y) {
                if (y > heightMap[x][z] - Config::CAVE_DRY_ROOF) continue;
                CaveCell cell = caveVolume.get(wx, y, wz);
                if (cell == CaveCell::Solid) continue;
                BlockId current = chunk.getBlock(x, y, z);
                bool carveable = current == BlockId::STONE || current == BlockId::DEEPSLATE ||
                                 current == BlockId::DIRT || current == BlockId::SAND ||
                                 current == BlockId::GRASS || current == BlockId::SNOW;
                if (!carveable) continue;
                chunk.setBlock(x, y, z, cell == CaveCell::Water ? BlockId::WATER :
                    cell == CaveCell::Lava ? BlockId::LAVA : BlockId::AIR);
            }
            for (int y = Config::BEDROCK_LEVEL + 1; y < Config::WORLD_MAX_Y; ++y) {
                BlockId current = chunk.getBlock(x, y, z);
                if (current != BlockId::STONE && current != BlockId::DEEPSLATE) continue;
                BlockId ore = m_oreGenerator.getOre(static_cast<float>(wx) + 0.5f,
                    static_cast<float>(y) + 0.5f, static_cast<float>(wz) + 0.5f, current);
                if (ore != BlockId::AIR) chunk.setBlock(x, y, z, ore);
            }
            if (heightMap[x][z] + 1 < Config::WORLD_MAX_Y &&
                chunk.getBlock(x, heightMap[x][z], z) != BlockId::AIR &&
                chunk.getBlock(x, heightMap[x][z] + 1, z) == BlockId::AIR) {
                BlockId decoration = SurfaceRules::decoration(
                    m_seed, wx, wz, heightMap[x][z], biomeMap[x][z], riverMap[x][z]);
                if (decoration != BlockId::AIR) {
                    const int featureHeight = SurfaceRules::decorationHeight(
                        m_seed, wx, wz, heightMap[x][z], decoration);
                    for (int dy = 1; dy <= featureHeight; ++dy)
                        if (heightMap[x][z] + dy < Config::WORLD_MAX_Y)
                            chunk.setBlock(x, heightMap[x][z] + dy, z, decoration);
                    if (decoration == BlockId::SUNFLOWER_BOTTOM &&
                        heightMap[x][z] + 2 < Config::WORLD_MAX_Y)
                        chunk.setBlock(x, heightMap[x][z] + 2, z,
                                       BlockId::SUNFLOWER_TOP);
                }
            }
        }
    }

    // ── Phase 4: Trees ──────────────────────────────────────────────────
    auto placements = m_treeGenerator.generateTrees(
        wxBase, wzBase, heightMap, biomeMap, riverMap);

    for (const auto& tp : placements) {
        // Structure reservations suppress trees inside footprints.  The same
        // pure query runs in region generation, keeping both paths identical.
        if (m_structureGenerator.reservationAt(wxBase + tp.localX,
                                               wzBase + tp.localZ))
            continue;
        int blockX = tp.localX;
        int blockZ = tp.localZ;
        int baseY  = tp.baseY + 1;  // trunk starts one above ground

        placeTree(chunk, blockX, baseY, blockZ, tp.type, tp.trunkHeight,
                  blockSetter, wxBase, wzBase);
    }

    // ── Phase 4b: Structures ──────────────────────────────────────────────
    const std::vector<StructurePlacement> structures =
        m_structureGenerator.generateStructures(wxBase, wzBase);
    for (const StructurePlacement& placement : structures) {
        StructureGenerator::build(placement, [&](int worldX, int worldY,
                                                 int worldZ, BlockId id) {
            if (!Config::isValidWorldY(worldY)) return;
            const bool inChunk =
                worldX >= wxBase && worldX < wxBase + Config::CHUNK_SIZE_X &&
                worldZ >= wzBase && worldZ < wzBase + Config::CHUNK_SIZE_Z;
            if (inChunk) {
                chunk.setBlock(worldX - wxBase, worldY, worldZ - wzBase, id);
                // Chest/furnace blocks also need a runtime block entity.
                // Reuse the structure setter as the entity-registration
                // channel; the streamer keys it off the block id.
                if ((id == BlockId::CHEST || id == BlockId::FURNACE) &&
                    structureSetter)
                    structureSetter(worldX, worldY, worldZ, id,
                        id == BlockId::CHEST
                            ? structureLootProfile(placement.type)
                            : StructureLootProfile::None,
                        WorldGenContext::hashPosition(
                            placement.variant,worldX,worldY,worldZ));
            } else if (structureSetter) {
                structureSetter(worldX, worldY, worldZ, id,
                    id == BlockId::CHEST
                        ? structureLootProfile(placement.type)
                        : StructureLootProfile::None,
                    WorldGenContext::hashPosition(
                        placement.variant,worldX,worldY,worldZ));
            }
        },[&](int worldX,int worldZ) {
            return m_heightPipeline.sampleColumn(worldX,worldZ).height;
        });
    }

    // ── Phase 5: Recompute column max Y ─────────────────────────────────
    for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
            int maxY = Config::WORLD_MIN_Y - 1;
            for (int y = Config::WORLD_MAX_Y - 1; y >= Config::WORLD_MIN_Y; --y) {
                if (chunk.blockAt(x, y, z) != 0) {
                    maxY = y;
                    break;
                }
            }
            chunk.setColumnMaxY(x, z, maxY);
        }
    }

    chunk.markDirty();
}

// ═══════════════════════════════════════════════════════════════════════════
// Shared trunk placement (used by all tree types)
// ═══════════════════════════════════════════════════════════════════════════

void WorldGenerator::placeTrunk(Chunk& chunk, int x, int baseY, int z,
                                 int trunkHeight, TreeType type) {
    BlockId wood = BlockId::WOOD;
    if (type == TreeType::BIRCH) wood = BlockId::BIRCH_WOOD;
    else if (type == TreeType::SPRUCE) wood = BlockId::SPRUCE_WOOD;
    else if (type == TreeType::JUNGLE) wood = BlockId::JUNGLE_WOOD;
    else if (type == TreeType::ACACIA) wood = BlockId::ACACIA_WOOD;
    for (int y = baseY; y < baseY + trunkHeight; ++y) {
        if (x >= 0 && x < 16 && z >= 0 && z < 16 &&
            Config::isValidWorldY(y)) {
            BlockId cur = chunk.getBlock(x, y, z);
            if (cur != BlockId::WATER) {
                chunk.setBlock(x, y, z, wood);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Tree placement (per-tree-type)
// ═══════════════════════════════════════════════════════════════════════════

void WorldGenerator::placeTree(Chunk& chunk, int x, int baseY, int z,
                               TreeType type, int trunkHeight,
                               const BlockSetter& blockSetter,
                               int chunkWorldX, int chunkWorldZ) {
    auto setLeaf = [&](int lx, int ly, int lz, BlockId id) {
        if (id == BlockId::LEAVES) {
            if (type == TreeType::BIRCH) id = BlockId::BIRCH_LEAVES;
            else if (type == TreeType::SPRUCE) id = BlockId::SPRUCE_LEAVES;
            else if (type == TreeType::JUNGLE) id = BlockId::JUNGLE_LEAVES;
            else if (type == TreeType::ACACIA) id = BlockId::ACACIA_LEAVES;
        }
        if (!Config::isValidWorldY(ly)) return;
        if (lx >= 0 && lx < 16 && lz >= 0 && lz < 16) {
            BlockId cur = chunk.getBlock(lx, ly, lz);
            bool leaf = cur == BlockId::LEAVES || cur == BlockId::BIRCH_LEAVES ||
                        cur == BlockId::SPRUCE_LEAVES || cur == BlockId::JUNGLE_LEAVES ||
                        cur == BlockId::ACACIA_LEAVES;
            if (cur == BlockId::AIR || leaf || cur == BlockId::SNOW) {
                chunk.setBlock(lx, ly, lz, id);
            }
        } else if (blockSetter) {
            // Propagate leaf to neighbor chunk
            int worldX = chunkWorldX + lx;
            int worldZ = chunkWorldZ + lz;
            blockSetter(worldX, ly, worldZ, id);
        }
    };

    // Generate deterministic hash for tree variation
    int hash = (x * 7919 + z * 6287 + baseY * 3313) & 0x7FFFFFFF;

    switch (type) {
        case TreeType::OAK: {
            // Classic oak: trunk + spherical leaf canopy
            placeTrunk(chunk, x, baseY, z, trunkHeight, type);
            int leafBase = baseY + trunkHeight - 2;
            for (int ly = leafBase; ly < leafBase + 4; ++ly) {
                int radius = (ly < leafBase + 2) ? 2 : 1;
                for (int dx = -radius; dx <= radius; ++dx) {
                    for (int dz = -radius; dz <= radius; ++dz) {
                        if (std::abs(dx) == radius && std::abs(dz) == radius &&
                            (hash + dx * 7 + dz * 13) % 3 == 0) continue;
                        setLeaf(x + dx, ly, z + dz, BlockId::LEAVES);
                    }
                }
            }
            break;
        }

        case TreeType::BIRCH: {
            // Taller thin trunk, smaller leaf cap
            placeTrunk(chunk, x, baseY, z, trunkHeight, type);
            int leafBase = baseY + trunkHeight - 2;
            for (int ly = leafBase; ly < leafBase + 3; ++ly) {
                int radius = 1;
                for (int dx = -radius; dx <= radius; ++dx) {
                    for (int dz = -radius; dz <= radius; ++dz) {
                        setLeaf(x + dx, ly, z + dz, BlockId::LEAVES);
                    }
                }
            }
            // Top leaf
            setLeaf(x, leafBase + 3, z, BlockId::LEAVES);
            break;
        }

        case TreeType::SPRUCE: {
            // Tall conical spruce
            placeTrunk(chunk, x, baseY, z, trunkHeight, type);
            // Conical leaf layers: wider at bottom, narrow at top
            int leafBase = baseY + trunkHeight - 4;
            for (int ly = leafBase; ly < leafBase + 5; ++ly) {
                int layer = ly - leafBase;
                int radius = (layer < 2) ? 2 : (layer < 4) ? 1 : 0;
                for (int dx = -radius; dx <= radius; ++dx) {
                    for (int dz = -radius; dz <= radius; ++dz) {
                        setLeaf(x + dx, ly, z + dz, BlockId::LEAVES);
                    }
                }
            }
            // Top spike
            setLeaf(x, leafBase + 5, z, BlockId::LEAVES);
            break;
        }

        case TreeType::JUNGLE: {
            // Thick trunk, wide canopy
            placeTrunk(chunk, x, baseY, z, trunkHeight, type);
            int leafBase = baseY + trunkHeight - 3;
            for (int ly = leafBase; ly < leafBase + 5; ++ly) {
                int radius = (ly < leafBase + 2) ? 3 : 2;
                int r2 = radius * radius;
                for (int dx = -radius; dx <= radius; ++dx) {
                    for (int dz = -radius; dz <= radius; ++dz) {
                        int dist2 = dx * dx + dz * dz;
                        if (dist2 > r2) continue;
                        if (dist2 == r2 && (hash + dx * 17 + dz * 23) % 4 == 0) continue;
                        setLeaf(x + dx, ly, z + dz, BlockId::LEAVES);
                    }
                }
            }
            break;
        }

        case TreeType::ACACIA: {
            // Slanted trunk effect simplified: straight trunk + flat top
            placeTrunk(chunk, x, baseY, z, trunkHeight, type);
            // Flat canopy at top
            int leafY = baseY + trunkHeight - 1;
            for (int dx = -2; dx <= 2; ++dx) {
                for (int dz = -2; dz <= 2; ++dz) {
                    if (std::abs(dx) == 2 && std::abs(dz) == 2) continue;
                    setLeaf(x + dx, leafY, z + dz, BlockId::LEAVES);
                }
            }
            setLeaf(x, leafY + 1, z, BlockId::LEAVES);
            break;
        }

        case TreeType::SWAMP_OAK: {
            // Short trunk with wide low canopy, often in water
            placeTrunk(chunk, x, baseY, z, trunkHeight, type);
            int leafBase = baseY + trunkHeight - 1;
            for (int ly = leafBase; ly < leafBase + 3; ++ly) {
                int radius = 2;
                for (int dx = -radius; dx <= radius; ++dx) {
                    for (int dz = -radius; dz <= radius; ++dz) {
                        if (std::abs(dx) == radius && std::abs(dz) == radius &&
                            (hash + ly * 37) % 3 == 0) continue;
                        setLeaf(x + dx, ly, z + dz, BlockId::LEAVES);
                    }
                }
            }
            break;
        }

        case TreeType::CACTUS: {
            // Green cactus column (using LEAVES as stand-in for cactus block)
            for (int y = baseY; y < baseY + trunkHeight; ++y) {
                if (x >= 0 && x < 16 && z >= 0 && z < 16 &&
                    Config::isValidWorldY(y)) {
                    chunk.setBlock(x, y, z, BlockId::CACTUS_BLOCK);
                }
            }
            break;
        }

        case TreeType::NONE:
        default:
            break;
    }
}
