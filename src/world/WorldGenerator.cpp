#include "world/WorldGenerator.h"
#include "Config.h"
#include "world/RegionGenerator.h"
#include "world/SurfaceRules.h"
#include <cmath>
#include <algorithm>

WorldGenerator::WorldGenerator(uint64_t seed, WorldType worldType)
    : m_seed(seed)
    , m_worldType(worldType)
    , m_noise(seed)
    , m_heightPipeline(m_noise, seed)
    , m_caveGenerator(m_noise, seed)
    , m_treeGenerator(seed)
    , m_oreGenerator(m_noise, seed)
{}

// ═══════════════════════════════════════════════════════════════════════════
// Height query (for spawn placement etc.)
// ═══════════════════════════════════════════════════════════════════════════

int WorldGenerator::getTerrainHeight(int worldX, int worldZ) const {
    if (m_worldType == WorldType::Superflat)
        return Config::WORLD_MIN_Y + 3;
    float wx = static_cast<float>(worldX);
    float wz = static_cast<float>(worldZ);
    float h = m_heightPipeline.queryHeight(wx, wz);
    return static_cast<int>(std::round(h));
}

HeightBiome WorldGenerator::queryHeightBiome(int worldX, int worldZ) const {
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
    return m_worldType == WorldType::Superflat
        ? superflatColumn() : m_heightPipeline.sampleColumn(worldX, worldZ);
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

void WorldGenerator::generateRegion(
    int originCX, int originCZ, int regionSizeChunks, int padding,
    std::vector<Chunk*>& chunks,
    std::vector<RegionGenerationData::PendingBlock>& pendingOut) {
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
