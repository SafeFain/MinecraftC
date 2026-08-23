#include "world/WorldGenerator.h"
#include "Config.h"
#include "world/RegionGenerator.h"
#include "world/SurfaceRules.h"
#include "world/WorldGenContext.h"
#include <cmath>
#include <algorithm>

namespace {

constexpr int HEAVEN_ISLAND_MIN_Y = 72;
constexpr uint64_t HEAVEN_SEED_DOMAIN = 0x484556454E5F5345ULL;
constexpr uint64_t HEAVEN_ISLAND_DOMAIN = 0x48454156454E4953ULL;
constexpr uint64_t HEAVEN_SATELLITE_DOMAIN = 0x484556534154454CULL;
constexpr uint64_t HEAVEN_LANDMARK_DOMAIN = 0x4845565255494E53ULL;
constexpr uint64_t HEAVEN_ECOLOGY_DOMAIN = 0x48455645434F4C4FULL;
constexpr int HEAVEN_LANDMARK_CELL = 192;

// These fields deliberately use world coordinates directly.  The broad field
// makes archipelagos and voids, while the smaller field breaks coastlines into
// bays, fingers, and occasional bridges without introducing a repeating cell
// layout.  The noise period is far beyond the explored Heaven window.
constexpr float HEAVEN_MASK_SCALE = 0.0055f;
constexpr float HEAVEN_DETAIL_SCALE = 0.014f;
constexpr float HEAVEN_PRESENT_THRESHOLD = 0.10f;
constexpr float HEAVEN_TOP_SCALE = 0.0045f;
constexpr float HEAVEN_RELIEF_SCALE = 0.010f;
constexpr float HEAVEN_UNDERSIDE_SCALE = 0.008f;

float smoothstep(float edge0, float edge1, float value) {
    if (edge0 == edge1) return value < edge0 ? 0.0f : 1.0f;
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

int floorDiv(int value, int divisor) {
    const int quotient = value / divisor;
    return value % divisor < 0 ? quotient - 1 : quotient;
}

int heavenEcologyBand(int worldX, int worldZ) {
    // Broad, world-coordinate bands guarantee that a normal exploration
    // window encounters all four sanctuary ecologies while retaining
    // deterministic boundaries across chunks and negative coordinates.
    constexpr int cellSize = 256;
    int band = floorDiv(worldX, cellSize) + 2 * floorDiv(worldZ, cellSize);
    band %= 4;
    return band < 0 ? band + 4 : band;
}

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
        return {island.top, island.biome};
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
        column.biome = island.biome;
        column.river = false;
        return column;
    }
    return m_worldType == WorldType::Superflat
        ? superflatColumn() : m_heightPipeline.sampleColumn(worldX, worldZ);
}

WorldGenerator::HeavenEcology WorldGenerator::heavenEcologyAt(
    int worldX, int worldZ) const {
    return sampleHeavenIsland(worldX, worldZ).ecology;
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

WorldGenerator::HeavenIslandColumn WorldGenerator::sampleHeavenIsland(
    int worldX, int worldZ) const {
    const WorldGenContext context(m_seed);
    const uint64_t islandSeed = context.derive(HEAVEN_ISLAND_DOMAIN);
    const float x = static_cast<float>(worldX);
    const float z = static_cast<float>(worldZ);

    // A warped macro field creates irregular island groups.  A lower-amplitude
    // detail field cuts the coast into natural bays and peninsulas while the
    // threshold leaves broad voids between groups.
    const float warpX = m_noise.noise2D(
        x * 0.0028f + 31.0f, z * 0.0028f - 47.0f);
    const float warpZ = m_noise.noise2D(
        x * 0.0028f - 83.0f, z * 0.0028f + 71.0f);
    const float macro = m_noise.octave2D(
        (x + warpX * 72.0f) * HEAVEN_MASK_SCALE + 113.0f,
        (z + warpZ * 72.0f) * HEAVEN_MASK_SCALE - 127.0f,
        2, 0.55f, 2.0f);
    const float detail = m_noise.noise2D(
        x * HEAVEN_DETAIL_SCALE + 173.0f,
        z * HEAVEN_DETAIL_SCALE - 229.0f);
    const float broad = m_noise.noise2D(
        x * 0.0022f - 401.0f,
        z * 0.0022f + 311.0f);
    const float field = macro * 0.76f + detail * 0.20f + broad * 0.10f;
    if (field <= HEAVEN_PRESENT_THRESHOLD) {
        // Void columns still return a valid biome for weather/decoration
        // queries, but have no terrain height or density range.
        const int biome = static_cast<int>(WorldGenContext::hashPosition(
            islandSeed, floorDiv(worldX, 64), 0,
            floorDiv(worldZ, 64)) % BIOME_COUNT);
        const auto ecology = static_cast<HeavenEcology>(
            heavenEcologyBand(worldX, worldZ));
        return {false, static_cast<Biome>(biome),
                Config::WORLD_MIN_Y - 1, Config::WORLD_MIN_Y, 1.0f,
                ecology};
    }

    HeavenIslandColumn island;
    island.present = true;
    // Keep broad, deterministic ecological bands without invoking the full
    // overworld height pipeline for every Heaven column.  The two climate
    // fields form a 6×5 palette grid, so nearby columns share a biome while
    // exploration still encounters the complete existing biome table.
    const float temperature = 0.5f + 0.5f * m_noise.noise2D(
        x * 0.0012f + 1201.0f,
        z * 0.0012f - 1297.0f);
    const float humidity = 0.5f + 0.5f * m_noise.noise2D(
        x * 0.00135f - 1433.0f,
        z * 0.00135f + 1511.0f);
    const int temperatureBand = std::clamp(
        static_cast<int>(temperature * 6.0f), 0, 5);
    const int humidityBand = std::clamp(
        static_cast<int>(humidity * 5.0f), 0, 4);
    island.biome = static_cast<Biome>(
        (temperatureBand * 5 + humidityBand) % BIOME_COUNT);
    const int ecologyBand = heavenEcologyBand(worldX, worldZ);
    island.ecology = static_cast<HeavenEcology>(ecologyBand);

    // The summit height is independent of the footprint.  Its two coherent
    // fields give wide plateaus with gentle slopes instead of a per-island
    // spherical cap, while rounding keeps adjacent walkable columns smooth.
    const float topBase = m_noise.octave2D(
        x * HEAVEN_TOP_SCALE + 521.0f,
        z * HEAVEN_TOP_SCALE - 607.0f,
        2, 0.5f, 2.0f);
    const float topRelief = m_noise.noise2D(
        x * HEAVEN_RELIEF_SCALE - 733.0f,
        z * HEAVEN_RELIEF_SCALE + 809.0f);
    island.top = std::clamp(static_cast<int>(std::lround(
        154.0f + topBase * 20.0f + topRelief * 4.0f)), 104, 236);

    // Interior depth is driven by mask density, not distance to an anchor.
    // The underside field then varies the taper from column to column, giving
    // each island a broken, End-like lower silhouette without floating shards.
    const float normalizedDensity = std::clamp(
        (field - HEAVEN_PRESENT_THRESHOLD) /
            (1.0f - HEAVEN_PRESENT_THRESHOLD),
        0.0f, 1.0f);
    const float interior = smoothstep(0.0f, 0.58f, normalizedDensity);
    const float undersideNoise = 0.5f + 0.5f * m_noise.noise2D(
        x * HEAVEN_UNDERSIDE_SCALE + 947.0f,
        z * HEAVEN_UNDERSIDE_SCALE - 1013.0f);
    const float depthValue = 6.0f + interior *
        (20.0f + undersideNoise * 24.0f);
    const int depth = std::clamp(static_cast<int>(std::lround(depthValue)),
                                 6, 52);
    const float spikeNoise = 0.5f + 0.5f * m_noise.noise2D(
        x * 0.018f - 1217.0f, z * 0.018f + 1289.0f);
    const int spikeDepth = spikeNoise > 0.72f
        ? static_cast<int>(std::lround((spikeNoise - 0.72f) * 55.0f)) : 0;
    island.bottom = std::max(HEAVEN_ISLAND_MIN_Y,
                             island.top - depth - spikeDepth + 1);
    // Preserve the old pool predicate's meaning: low values are island
    // interiors, high values are coastlines.
    island.islandFactor = 1.0f - interior;
    return island;
}

WorldGenerator::HeavenIslandColumn WorldGenerator::sampleHeavenSatellite(
    int worldX, int worldZ) const {
    HeavenIslandColumn satellite;
    satellite.ecology = heavenEcologyAt(worldX, worldZ);
    const HeavenIslandColumn primary = sampleHeavenIsland(worldX, worldZ);
    if (primary.present) return satellite;

    const float x = static_cast<float>(worldX);
    const float z = static_cast<float>(worldZ);
    const float domainOffset = static_cast<float>(
        (HEAVEN_SATELLITE_DOMAIN >> 8) & 0xffu);
    const float broad = m_noise.octave2D(
        x * 0.0065f + 211.0f + domainOffset,
        z * 0.0065f - 337.0f - domainOffset, 2, 0.55f, 2.0f);
    const float detail = m_noise.noise2D(
        x * 0.021f - 419.0f + domainOffset * 0.5f,
        z * 0.021f + 503.0f - domainOffset * 0.5f);
    const float field = broad * 0.72f + detail * 0.28f;
    // The high-field tail is intentionally uncommon, but a threshold below
    // the signed-noise upper shoulder keeps a few satellites visible in a
    // normal first-kilometre exploration window for every seed.
    if (field < 0.24f) return satellite;

    satellite.present = true;
    satellite.satellite = true;
    satellite.ecology = field > 0.82f
        ? HeavenEcology::StarCrystalGarden : HeavenEcology::SunstoneHeights;
    const float heightNoise = 0.5f + 0.5f * m_noise.noise2D(
        x * 0.010f + 601.0f, z * 0.010f - 733.0f);
    satellite.satelliteTop = std::clamp(static_cast<int>(std::lround(
        242.0f + heightNoise * 34.0f)), 232, 286);
    const int thickness = 5 + static_cast<int>(std::lround(heightNoise * 13.0f));
    satellite.satelliteBottom = satellite.satelliteTop - thickness + 1;
    satellite.top = satellite.satelliteTop;
    satellite.bottom = satellite.satelliteBottom;
    satellite.islandFactor = 0.45f;
    return satellite;
}

void WorldGenerator::populateHeaven(
    Chunk& chunk, WorldGenerator& generator) {
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
    const auto surfaceBlock = [](HeavenEcology ecology) {
        switch (ecology) {
            case HeavenEcology::SunstoneHeights: return BlockId::SUNSTONE;
            case HeavenEcology::StarCrystalGarden: return BlockId::MOSS;
            case HeavenEcology::DawnMeadow:
            case HeavenEcology::SkyrootGrove: return BlockId::AETHER_GRASS;
        }
        return BlockId::AETHER_GRASS;
    };
    const auto fillColumn = [&](int worldX, int worldZ,
                                const HeavenIslandColumn& island) {
        if (!island.present) return;
        const bool satellite = island.satellite;
        const int bottom = satellite ? island.satelliteBottom : island.bottom;
        const int top = satellite ? island.satelliteTop : island.top;
        for (int worldY = bottom; worldY <= top; ++worldY) {
            BlockId block = BlockId::CLOUDSTONE;
            if (worldY == top) {
                block = surfaceBlock(island.ecology);
            } else if (!satellite && worldY >= top - 2 &&
                       island.ecology != HeavenEcology::SunstoneHeights) {
                block = BlockId::AETHER_SOIL;
            } else if (island.ecology == HeavenEcology::SunstoneHeights &&
                       hashPercent(generator.m_seed ^ HEAVEN_ECOLOGY_DOMAIN,
                                   worldX, worldY, worldZ) < 13) {
                block = BlockId::SUNSTONE;
            }
            setLocal(worldX, worldY, worldZ, block);
        }
    };

    for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
            const int wx = baseX + x;
            const int wz = baseZ + z;
            const HeavenIslandColumn island =
                generator.sampleHeavenIsland(wx, wz);
            fillColumn(wx, wz, island);
            fillColumn(wx, wz, generator.sampleHeavenSatellite(wx, wz));
        }
    }

    // Skyroot trees are selected in world coordinates and each chunk writes
    // only the part of a tree it owns. This keeps region and singleton paths
    // independent of request order while still allowing canopies to cross a
    // chunk boundary.
    for (int worldX = baseX - 5; worldX <= baseX + 20; ++worldX) {
        for (int worldZ = baseZ - 5; worldZ <= baseZ + 20; ++worldZ) {
            const HeavenIslandColumn island =
                generator.sampleHeavenIsland(worldX, worldZ);
            if (!island.present || island.ecology != HeavenEcology::SkyrootGrove ||
                hashPercent(generator.m_seed ^ HEAVEN_ECOLOGY_DOMAIN,
                            worldX, 17, worldZ) >= 10)
                continue;
            const int trunkHeight = 5 + hashPercent(
                generator.m_seed ^ HEAVEN_ECOLOGY_DOMAIN, worldX, 23, worldZ) % 4;
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

    // Glowing plants and crystals make the garden ecology readable from a
    // distance without introducing a new simulation system.
    for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
            const int wx = baseX + x;
            const int wz = baseZ + z;
            const HeavenIslandColumn island = generator.sampleHeavenIsland(wx, wz);
            if (!island.present || island.top + 1 >= Config::WORLD_MAX_Y) continue;
            const int roll = hashPercent(generator.m_seed ^ HEAVEN_ECOLOGY_DOMAIN,
                                          wx, 31, wz);
            if (island.ecology == HeavenEcology::StarCrystalGarden && roll < 9) {
                setIfAir(wx, island.top + 1, wz, BlockId::STAR_CRYSTAL);
                if (roll < 4) {
                    setIfAir(wx + 1, island.top + 1, wz, BlockId::STAR_CRYSTAL);
                    setIfAir(wx, island.top + 1, wz + 1, BlockId::STAR_CRYSTAL);
                }
            } else if ((island.ecology == HeavenEcology::DawnMeadow ||
                        island.ecology == HeavenEcology::StarCrystalGarden) &&
                       roll < 20) {
                setIfAir(wx, island.top + 1, wz, BlockId::STARFLOWER);
            }
        }
    }

    // Rare coordinate-owned shrines provide a long-distance landmark. The
    // anchor is selected from a wide cell, but every piece is written through
    // setIfAir so the exact same structure appears regardless of chunk order.
    const int firstCellX = floorDiv(baseX - 8, HEAVEN_LANDMARK_CELL);
    const int lastCellX = floorDiv(baseX + 23, HEAVEN_LANDMARK_CELL);
    const int firstCellZ = floorDiv(baseZ - 8, HEAVEN_LANDMARK_CELL);
    const int lastCellZ = floorDiv(baseZ + 23, HEAVEN_LANDMARK_CELL);
    for (int cellX = firstCellX; cellX <= lastCellX; ++cellX) {
        for (int cellZ = firstCellZ; cellZ <= lastCellZ; ++cellZ) {
            const uint64_t cellSeed = WorldGenContext::hashPosition(
                generator.m_seed ^ HEAVEN_LANDMARK_DOMAIN, cellX, 0, cellZ);
            if (cellSeed % 100u >= 24u) continue;
            const int anchorX = cellX * HEAVEN_LANDMARK_CELL +
                24 + static_cast<int>((cellSeed >> 8) % 144u);
            const int anchorZ = cellZ * HEAVEN_LANDMARK_CELL +
                24 + static_cast<int>((cellSeed >> 20) % 144u);
            HeavenIslandColumn anchor =
                generator.sampleHeavenIsland(anchorX, anchorZ);
            int resolvedAnchorX = anchorX;
            int resolvedAnchorZ = anchorZ;
            if (!anchor.present) {
                // A cell remains rare even when its first hashed point lands
                // in the void.  Try a bounded deterministic set of points so
                // the landmark is discoverable without making generation
                // order or neighboring chunks observable.
                for (int attempt = 1; attempt < 24 && !anchor.present; ++attempt) {
                    const uint64_t candidateSeed = WorldGenContext::hashPosition(
                        cellSeed, attempt, 2, -attempt);
                    resolvedAnchorX = cellX * HEAVEN_LANDMARK_CELL + 24 +
                        static_cast<int>((candidateSeed >> 8) % 144u);
                    resolvedAnchorZ = cellZ * HEAVEN_LANDMARK_CELL + 24 +
                        static_cast<int>((candidateSeed >> 20) % 144u);
                    anchor = generator.sampleHeavenIsland(
                        resolvedAnchorX, resolvedAnchorZ);
                }
            }
            if (!anchor.present)
                continue;
            for (int dx = -4; dx <= 4; ++dx) {
                for (int dz = -4; dz <= 4; ++dz) {
                    if (std::abs(dx) != 4 && std::abs(dz) != 4) continue;
                    const HeavenIslandColumn column =
                        generator.sampleHeavenIsland(resolvedAnchorX + dx,
                                                     resolvedAnchorZ + dz);
                    if (!column.present || std::abs(column.top - anchor.top) > 5)
                        continue;
                    const int height = 2 + static_cast<int>(
                        WorldGenContext::hashPosition(cellSeed, dx, 1, dz) % 3u);
                    for (int y = 1; y <= height; ++y)
                        setIfAir(resolvedAnchorX + dx, column.top + y,
                                 resolvedAnchorZ + dz,
                                 BlockId::SUNSTONE);
                }
            }
            setIfAir(resolvedAnchorX, anchor.top + 1, resolvedAnchorZ,
                     BlockId::SUNSTONE);
            setIfAir(resolvedAnchorX, anchor.top + 2, resolvedAnchorZ,
                     BlockId::STAR_CRYSTAL);
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
        for (Chunk* chunk : chunks) {
            if (chunk == nullptr) continue;
            populateHeaven(*chunk, *this);
            chunk->generated = true;
            chunk->generationInProgress = false;
            chunk->markDirty();
        }
        pendingOut.clear();
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
                               const BlockSetter& blockSetter) {
    if (isHeaven()) {
        populateHeaven(chunk, *this);
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
        int blockX = tp.localX;
        int blockZ = tp.localZ;
        int baseY  = tp.baseY + 1;  // trunk starts one above ground

        placeTree(chunk, blockX, baseY, blockZ, tp.type, tp.trunkHeight,
                  blockSetter, wxBase, wzBase);
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
