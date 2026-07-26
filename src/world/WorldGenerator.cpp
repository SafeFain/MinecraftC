#include "world/WorldGenerator.h"
#include "Config.h"
#include "world/SurfaceRules.h"
#include <cmath>
#include <algorithm>

WorldGenerator::WorldGenerator(uint64_t seed)
    : m_seed(seed)
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
    float wx = static_cast<float>(worldX);
    float wz = static_cast<float>(worldZ);
    float h = m_heightPipeline.queryHeight(wx, wz);
    return static_cast<int>(std::round(h));
}

HeightBiome WorldGenerator::queryHeightBiome(int worldX, int worldZ) const {
    return m_heightPipeline.queryHeightBiome(
        static_cast<float>(worldX), static_cast<float>(worldZ));
}

// ═══════════════════════════════════════════════════════════════════════════
// Main generation
// ═══════════════════════════════════════════════════════════════════════════

void WorldGenerator::generate(Chunk& chunk,
                               const NeighborQuery& neighborQuery,
                               const BlockSetter& blockSetter) {
    int wxBase = chunk.worldX();
    int wzBase = chunk.worldZ();

    // Phase 1: Height / Biome / River maps
    int   heightMap[16][16];
    Biome biomeMap[16][16];
    bool  riverMap[16][16];

    m_heightPipeline.compute(wxBase, wzBase, heightMap, biomeMap, riverMap, neighborQuery);

    // ── Phase 2: Fill blocks ────────────────────────────────────────────
    for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
            int   height = heightMap[x][z];
            Biome biome  = biomeMap[x][z];
            const BiomeProperties& bprops = getBiomeProps(biome);
            SurfaceProfile surface = SurfaceRules::profile(
                m_seed, wxBase + x, wzBase + z, biome);

            // Bedrock (y = 0 .. BEDROCK_LEVEL)
            for (int y = 0; y <= Config::BEDROCK_LEVEL; ++y) {
                chunk.setBlock(x, y, z, BlockId::BEDROCK);
            }

            // Stone (bedrock+1 .. height-4)
            int subsoilTop = height - surface.depth;
            int stoneStart = Config::BEDROCK_LEVEL + 1;
            int stoneEnd   = std::max(stoneStart, subsoilTop);

            for (int y = stoneStart; y < stoneEnd; ++y) {
                chunk.setBlock(x, y, z, BlockId::STONE);
            }

            // Deepslate: below DEEPSLATE_DEPTH, stone becomes deepslate
            for (int y = stoneStart; y <= Config::DEEPSLATE_DEPTH && y < stoneEnd; ++y) {
                chunk.setBlock(x, y, z, BlockId::DEEPSLATE);
            }

            // Subsoil (height-3 .. height-1)
            for (int y = stoneEnd; y < height; ++y) {
                chunk.setBlock(x, y, z, surface.under);
            }

            // Surface block (y = height)
            chunk.setBlock(x, height, z, surface.top);

            // Snow cover: if height >= biome snowLine, override surface
            if (height >= bprops.snowLine && bprops.snowLine < Config::SNOW_LINE_DISABLED) {
                chunk.setBlock(x, height, z, BlockId::SNOW);
            }

            // Water fill (oceans, lakes, rivers)
            int waterTop = bprops.waterLevel;
            if (height < waterTop) {
                for (int y = height + 1; y <= waterTop; ++y) {
                    if (y < Config::CHUNK_SIZE_Y) {
                        chunk.setBlock(x, y, z, BlockId::WATER);
                    }
                }
                // Ice: freeze surface water in cold biomes
                if ((biome == Biome::SNOW_TUNDRA || biome == Biome::TAIGA) &&
                    height + 1 <= Config::ICE_FREEZE_MAX_Y) {
                    chunk.setBlock(x, height + 1, z, BlockId::ICE);
                }
            }
        }
    }

    // ── Phase 3: Hybrid caves, liquids, then ores ───────────────────────
    std::vector<CaveColumnInfo> caveColumns(
        static_cast<size_t>(Config::CHUNK_SIZE_X) * Config::CHUNK_SIZE_Z);
    for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
        for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
            const auto& props = getBiomeProps(biomeMap[x][z]);
            caveColumns[static_cast<size_t>(z) * Config::CHUNK_SIZE_X + x] = {
                heightMap[x][z], props.waterLevel,
                riverMap[x][z] || heightMap[x][z] < props.waterLevel
            };
        }
    }
    CaveVolume caveVolume = m_caveGenerator.generateVolume(
        wxBase, wzBase, Config::CHUNK_SIZE_X, Config::CHUNK_SIZE_Z, caveColumns);
    for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
            int wx = wxBase + x, wz = wzBase + z;
            for (int y = Config::CAVE_MIN_Y; y < Config::CHUNK_SIZE_Y; ++y) {
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
            for (int y = Config::BEDROCK_LEVEL + 1; y < Config::CHUNK_SIZE_Y; ++y) {
                BlockId current = chunk.getBlock(x, y, z);
                if (current != BlockId::STONE && current != BlockId::DEEPSLATE) continue;
                BlockId ore = m_oreGenerator.getOre(static_cast<float>(wx) + 0.5f,
                    static_cast<float>(y) + 0.5f, static_cast<float>(wz) + 0.5f, current);
                if (ore != BlockId::AIR) chunk.setBlock(x, y, z, ore);
            }
            if (heightMap[x][z] + 1 < Config::CHUNK_SIZE_Y &&
                chunk.getBlock(x, heightMap[x][z], z) != BlockId::AIR &&
                chunk.getBlock(x, heightMap[x][z] + 1, z) == BlockId::AIR) {
                BlockId decoration = SurfaceRules::decoration(
                    m_seed, wx, wz, heightMap[x][z], biomeMap[x][z], riverMap[x][z]);
                if (decoration != BlockId::AIR)
                    chunk.setBlock(x, heightMap[x][z] + 1, z, decoration);
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
            int maxY = 0;
            for (int y = Config::CHUNK_SIZE_Y - 1; y >= 0; --y) {
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
            y >= 0 && y < Config::CHUNK_SIZE_Y) {
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
        if (ly < 0 || ly >= Config::CHUNK_SIZE_Y) return;
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
                    y >= 0 && y < Config::CHUNK_SIZE_Y) {
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
