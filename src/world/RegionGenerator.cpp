#include "world/RegionGenerator.h"
#include "world/HeightPipeline.h"
#include "world/CaveGenerator.h"
#include "world/TreeGenerator.h"
#include "world/OreGenerator.h"
#include "world/BiomeMap.h"
#include "world/SurfaceRules.h"
#include "Config.h"

#include <cmath>
#include <algorithm>

RegionGenerator::RegionGenerator(HeightPipeline& hp, CaveGenerator& cg,
                                 TreeGenerator& tg, OreGenerator& og,
                                 uint64_t seed)
    : m_heightPipeline(hp)
    , m_caveGenerator(cg)
    , m_treeGenerator(tg)
    , m_oreGenerator(og)
    , m_seed(seed)
    , m_structureGenerator(seed, m_heightPipeline)
{}

// ═══════════════════════════════════════════════════════════════════════════
// Main entry point
// ═══════════════════════════════════════════════════════════════════════════

void RegionGenerator::generateRegion(
    int originCX, int originCZ,
    int regionSizeChunks, int padding,
    std::vector<Chunk*>& chunks,
    std::vector<RegionGenerationData::PendingBlock>& pendingOut)
{
    m_regionSizeChunks = regionSizeChunks;

    // Initialize region data
    m_regionData = RegionGenerationData{};
    m_regionData.regionOriginCX   = originCX;
    m_regionData.regionOriginCZ   = originCZ;
    m_regionData.worldOriginX     = originCX * Config::CHUNK_SIZE_X;
    m_regionData.worldOriginZ     = originCZ * Config::CHUNK_SIZE_Z;
    m_regionData.regionSizeChunks = regionSizeChunks;
    m_regionData.regionSizeBlocks = regionSizeChunks * Config::CHUNK_SIZE_X;
    m_regionData.padding          = padding;
    m_regionData.paddedWidth      = m_regionData.regionSizeBlocks + 2 * padding;
    m_regionData.paddedDepth      = m_regionData.regionSizeBlocks + 2 * padding;

    // Allocate column array
    m_regionData.columns.resize(
        static_cast<size_t>(m_regionData.paddedWidth) * static_cast<size_t>(m_regionData.paddedDepth));

    // Phase 1: Pre-compute height, biome, river for padded grid
    precomputeColumns();

    // Phase 1b: Pre-compute deterministic hybrid cave volume
    precomputeCaves();

    // Phase 1c: Region-wide structure placement (anchors in the region core)
    placeStructuresRegion();

    // Phase 2: Region-wide tree placement (structures suppress trees)
    placeTreesRegion();

    // Phase 3a: Block column population (no trees yet)
    for (int lcz = 0; lcz < regionSizeChunks; ++lcz) {
        for (int lcx = 0; lcx < regionSizeChunks; ++lcx) {
            size_t idx = static_cast<size_t>(lcz) * static_cast<size_t>(regionSizeChunks) + static_cast<size_t>(lcx);
            populateChunk(*chunks[idx], lcx, lcz);
        }
    }

    // Phase 3b: Place all trees (now that all chunks have their base blocks)
    for (const auto& tp : m_regionData.trees) {
        // Determine which chunk the trunk is in
        int trunkWorldX = m_regionData.worldOriginX + tp.localX;
        int trunkWorldZ = m_regionData.worldOriginZ + tp.localZ;
        int trunkCX = (trunkWorldX - m_regionData.worldOriginX) / Config::CHUNK_SIZE_X;
        int trunkCZ = (trunkWorldZ - m_regionData.worldOriginZ) / Config::CHUNK_SIZE_Z;
        int trunkLX = tp.localX - trunkCX * Config::CHUNK_SIZE_X;
        int trunkLZ = tp.localZ - trunkCZ * Config::CHUNK_SIZE_Z;
        int baseY   = tp.baseY + 1;  // trunk starts one above ground

        if (trunkCX >= 0 && trunkCX < regionSizeChunks &&
            trunkCZ >= 0 && trunkCZ < regionSizeChunks) {
            size_t chunkIdx = static_cast<size_t>(trunkCZ) * static_cast<size_t>(regionSizeChunks) + static_cast<size_t>(trunkCX);
            Chunk& chunk = *chunks[chunkIdx];
            int chunkWorldX = chunk.worldX();
            int chunkWorldZ = chunk.worldZ();

            // setLeaf: handles in-region and out-of-region placement
            auto setLeaf = [&](int lx, int ly, int lz, BlockId id) {
                if (!Config::isValidWorldY(ly)) return;
                if (id == BlockId::LEAVES) {
                    if (tp.type == TreeType::BIRCH) id = BlockId::BIRCH_LEAVES;
                    else if (tp.type == TreeType::SPRUCE) id = BlockId::SPRUCE_LEAVES;
                    else if (tp.type == TreeType::JUNGLE) id = BlockId::JUNGLE_LEAVES;
                    else if (tp.type == TreeType::ACACIA) id = BlockId::ACACIA_LEAVES;
                }

                int worldX = chunkWorldX + lx;
                int worldZ = chunkWorldZ + lz;

                // Compute which region-local chunk this leaf is in
                auto floorChunk = [](int value) {
                    return value >= 0 ? value / Config::CHUNK_SIZE_X
                                      : -((-value + Config::CHUNK_SIZE_X - 1) /
                                          Config::CHUNK_SIZE_X);
                };
                int leafCX = floorChunk(worldX - m_regionData.worldOriginX);
                int leafCZ = floorChunk(worldZ - m_regionData.worldOriginZ);

                if (leafCX >= 0 && leafCX < regionSizeChunks &&
                    leafCZ >= 0 && leafCZ < regionSizeChunks) {
                    // Leaf is within the region — set directly
                    size_t leafChunkIdx = static_cast<size_t>(leafCZ) * static_cast<size_t>(regionSizeChunks) + static_cast<size_t>(leafCX);
                    Chunk& leafChunk = *chunks[leafChunkIdx];
                    int llx = worldX - leafChunk.worldX();
                    int llz = worldZ - leafChunk.worldZ();
                    if (llx >= 0 && llx < 16 && llz >= 0 && llz < 16) {
                        BlockId cur = leafChunk.getBlock(llx, ly, llz);
                        bool leaf = cur == BlockId::LEAVES || cur == BlockId::BIRCH_LEAVES ||
                                    cur == BlockId::SPRUCE_LEAVES || cur == BlockId::JUNGLE_LEAVES ||
                                    cur == BlockId::ACACIA_LEAVES;
                        if (cur == BlockId::AIR || leaf || cur == BlockId::SNOW) {
                            leafChunk.setBlock(llx, ly, llz, id);
                        }
                    }
                } else {
                    // Leaf is outside the region — queue as pending
                    pendingOut.push_back({worldX, ly, worldZ, id});
                }
            };

            // Place trunk (only within the owning chunk)
            BlockId trunkBlock = BlockId::WOOD;
            if (tp.type == TreeType::BIRCH) trunkBlock = BlockId::BIRCH_WOOD;
            else if (tp.type == TreeType::SPRUCE) trunkBlock = BlockId::SPRUCE_WOOD;
            else if (tp.type == TreeType::JUNGLE) trunkBlock = BlockId::JUNGLE_WOOD;
            else if (tp.type == TreeType::ACACIA) trunkBlock = BlockId::ACACIA_WOOD;
            for (int y = baseY; y < baseY + tp.trunkHeight; ++y) {
                if (trunkLX >= 0 && trunkLX < 16 && trunkLZ >= 0 && trunkLZ < 16 &&
                    Config::isValidWorldY(y)) {
                    BlockId cur = chunk.getBlock(trunkLX, y, trunkLZ);
                    if (cur != BlockId::WATER) {
                        chunk.setBlock(trunkLX, y, trunkLZ, trunkBlock);
                    }
                }
            }

            // Place canopy via setLeaf (handles cross-chunk within region)
            int hash = (trunkLX * 7919 + trunkLZ * 6287 + baseY * 3313) & 0x7FFFFFFF;

            switch (tp.type) {
                case TreeType::OAK: {
                    int leafBase = baseY + tp.trunkHeight - 2;
                    for (int ly = leafBase; ly < leafBase + 4; ++ly) {
                        int radius = (ly < leafBase + 2) ? 2 : 1;
                        for (int dx = -radius; dx <= radius; ++dx) {
                            for (int dz = -radius; dz <= radius; ++dz) {
                                if (std::abs(dx) == radius && std::abs(dz) == radius &&
                                    (hash + dx * 7 + dz * 13) % 3 == 0) continue;
                                setLeaf(trunkLX + dx, ly, trunkLZ + dz, BlockId::LEAVES);
                            }
                        }
                    }
                    break;
                }
                case TreeType::BIRCH: {
                    int leafBase = baseY + tp.trunkHeight - 2;
                    for (int ly = leafBase; ly < leafBase + 3; ++ly) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            for (int dz = -1; dz <= 1; ++dz) {
                                setLeaf(trunkLX + dx, ly, trunkLZ + dz, BlockId::LEAVES);
                            }
                        }
                    }
                    setLeaf(trunkLX, leafBase + 3, trunkLZ, BlockId::LEAVES);
                    break;
                }
                case TreeType::SPRUCE: {
                    int leafBase = baseY + tp.trunkHeight - 4;
                    for (int ly = leafBase; ly < leafBase + 5; ++ly) {
                        int layer = ly - leafBase;
                        int radius = (layer < 2) ? 2 : (layer < 4) ? 1 : 0;
                        for (int dx = -radius; dx <= radius; ++dx) {
                            for (int dz = -radius; dz <= radius; ++dz) {
                                setLeaf(trunkLX + dx, ly, trunkLZ + dz, BlockId::LEAVES);
                            }
                        }
                    }
                    setLeaf(trunkLX, leafBase + 5, trunkLZ, BlockId::LEAVES);
                    break;
                }
                case TreeType::JUNGLE: {
                    int leafBase = baseY + tp.trunkHeight - 3;
                    for (int ly = leafBase; ly < leafBase + 5; ++ly) {
                        int radius = (ly < leafBase + 2) ? 3 : 2;
                        int r2 = radius * radius;
                        for (int dx = -radius; dx <= radius; ++dx) {
                            for (int dz = -radius; dz <= radius; ++dz) {
                                int dist2 = dx * dx + dz * dz;
                                if (dist2 > r2) continue;
                                if (dist2 == r2 && (hash + dx * 17 + dz * 23) % 4 == 0) continue;
                                setLeaf(trunkLX + dx, ly, trunkLZ + dz, BlockId::LEAVES);
                            }
                        }
                    }
                    break;
                }
                case TreeType::ACACIA: {
                    int leafY = baseY + tp.trunkHeight - 1;
                    for (int dx = -2; dx <= 2; ++dx) {
                        for (int dz = -2; dz <= 2; ++dz) {
                            if (std::abs(dx) == 2 && std::abs(dz) == 2) continue;
                            setLeaf(trunkLX + dx, leafY, trunkLZ + dz, BlockId::LEAVES);
                        }
                    }
                    setLeaf(trunkLX, leafY + 1, trunkLZ, BlockId::LEAVES);
                    break;
                }
                case TreeType::SWAMP_OAK: {
                    int leafBase = baseY + tp.trunkHeight - 1;
                    for (int ly = leafBase; ly < leafBase + 3; ++ly) {
                        for (int dx = -2; dx <= 2; ++dx) {
                            for (int dz = -2; dz <= 2; ++dz) {
                                if (std::abs(dx) == 2 && std::abs(dz) == 2 &&
                                    (hash + ly * 37) % 3 == 0) continue;
                                setLeaf(trunkLX + dx, ly, trunkLZ + dz, BlockId::LEAVES);
                            }
                        }
                    }
                    break;
                }
                case TreeType::CACTUS: {
                    for (int y = baseY; y < baseY + tp.trunkHeight; ++y) {
                        if (trunkLX >= 0 && trunkLX < 16 && trunkLZ >= 0 && trunkLZ < 16 &&
                            Config::isValidWorldY(y)) {
                            chunk.setBlock(trunkLX, y, trunkLZ, BlockId::CACTUS_BLOCK);
                        }
                    }
                    break;
                }
                case TreeType::NONE:
                default:
                    break;
            }
        }
    }

    // Phase 3c: Place all structures (now that terrain and trees exist)
    placeStructures(chunks, pendingOut);

    // Phase 4: Finalize all chunks
    finalizeChunks(chunks);
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase 1: Pre-compute column data for padded region
// ═══════════════════════════════════════════════════════════════════════════

void RegionGenerator::precomputeColumns() {
    m_heightPipeline.computePaddedRegion(
        m_regionData.worldOriginX, m_regionData.worldOriginZ,
        m_regionData.regionSizeBlocks, m_regionData.regionSizeBlocks,
        m_regionData.padding,
        m_regionData.columns.data());
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase 1b: Pre-compute hybrid cave volume for this region
// ═══════════════════════════════════════════════════════════════════════════

void RegionGenerator::precomputeCaves() {
    int size = m_regionData.regionSizeBlocks;
    int pad = m_regionData.padding;
    std::vector<CaveColumnInfo> columns(static_cast<size_t>(size) * size);
    for (int z = 0; z < size; ++z) {
        for (int x = 0; x < size; ++x) {
            const auto& col = m_regionData.col(pad + x, pad + z);
            columns[static_cast<size_t>(z) * size + x] = {
                col.height, col.waterLevel,
                col.isRiver || col.height < col.waterLevel
            };
        }
    }
    m_regionData.caves = m_caveGenerator.generateVolume(
        m_regionData.worldOriginX, m_regionData.worldOriginZ, size, size, columns);
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase 1c: Region-wide structure placement
// ═══════════════════════════════════════════════════════════════════════════

void RegionGenerator::placeStructuresRegion() {
    m_structureGenerator.generateStructuresRegion(
        m_regionData.worldOriginX, m_regionData.worldOriginZ,
        m_regionData.regionSizeBlocks, m_regionData.regionSizeBlocks,
        m_structures);
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase 2: Region-wide tree placement
// ═══════════════════════════════════════════════════════════════════════════

void RegionGenerator::placeTreesRegion()
{
    int regionBlocks = m_regionData.regionSizeBlocks;
    int pad = m_regionData.padding;

    // Build flat arrays for the region core (regionSizeBlocks × regionSizeBlocks)
    // from the pre-computed padded column grid
    std::vector<int>     heightFlat(static_cast<size_t>(regionBlocks) * static_cast<size_t>(regionBlocks));
    std::vector<Biome>   biomeFlat(static_cast<size_t>(regionBlocks) * static_cast<size_t>(regionBlocks));
    std::vector<uint8_t> riverFlat(static_cast<size_t>(regionBlocks) * static_cast<size_t>(regionBlocks));

    for (int lz = 0; lz < regionBlocks; ++lz) {
        for (int lx = 0; lx < regionBlocks; ++lx) {
            const auto& col = m_regionData.col(pad + lx, pad + lz);
            size_t idx = static_cast<size_t>(lz) * static_cast<size_t>(regionBlocks) + static_cast<size_t>(lx);
            heightFlat[idx] = col.height;
            biomeFlat[idx]  = col.biome;
            riverFlat[idx]  = col.isRiver ? 1 : 0;
        }
    }

    m_treeGenerator.generateTreesRegion(
        m_regionData.worldOriginX, m_regionData.worldOriginZ,
        regionBlocks, regionBlocks,
        heightFlat.data(), biomeFlat.data(), riverFlat.data(),
        pad,
        m_regionData.trees);

    // Structure reservations suppress trees inside footprints.  The query is
    // a pure world-coordinate function, so region and singleton generation
    // filter the same tree anchors identically.
    m_regionData.trees.erase(std::remove_if(
        m_regionData.trees.begin(), m_regionData.trees.end(),
        [&](const RegionGenerationData::TreePlacement& tree) {
            return m_structureGenerator.reservationAt(
                m_regionData.worldOriginX + tree.localX,
                m_regionData.worldOriginZ + tree.localZ);
        }),
        m_regionData.trees.end());
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase 3a: Populate a single chunk's block columns
// ═══════════════════════════════════════════════════════════════════════════

void RegionGenerator::populateChunk(Chunk& chunk, int localCX, int localCZ) {
    int wxBase = chunk.worldX();
    int wzBase = chunk.worldZ();
    int pad = m_regionData.padding;

    for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
            // Look up pre-computed column info from region data
            int regionLX = pad + localCX * Config::CHUNK_SIZE_X + x;
            int regionLZ = pad + localCZ * Config::CHUNK_SIZE_Z + z;
            const auto& col = m_regionData.col(regionLX, regionLZ);

            int   height = col.height;
            Biome biome  = col.biome;
            const BiomeProperties& bprops = getBiomeProps(biome);
            SurfaceRuleContext surfaceContext{
                biome, col.archetype, height, col.waterLevel, col.slope,
                col.localRelief, col.primaryArchetypeWeight,
                col.volcanicWeight, col.craterWeight, col.riverWeight,
                col.isRiver
            };
            surfaceContext.secondaryArchetype = col.secondaryArchetype;
            surfaceContext.secondaryArchetypeWeight = col.archetypeBlend;
            SurfaceProfile surface = SurfaceRules::profile(
                m_seed, wxBase + x, wzBase + z, surfaceContext);

            const int worldX = wxBase + x, worldZ = wzBase + z;
            SurfaceColumn terrainColumn;
            terrainColumn.height = col.height;
            terrainColumn.nominalHeight = col.nominalHeight;
            terrainColumn.mountainFactor = col.mountainFactor;
            terrainColumn.slope = col.slope;
            terrainColumn.localRelief = col.localRelief;
            terrainColumn.riverWeight = col.riverWeight;
            terrainColumn.densityWeight = col.densityWeight;
            terrainColumn.primaryArchetypeWeight = col.primaryArchetypeWeight;
            terrainColumn.volcanicWeight = col.volcanicWeight;
            terrainColumn.craterWeight = col.craterWeight;
            terrainColumn.densityMinY = col.densityMinY;
            terrainColumn.densityMaxY = col.densityMaxY;
            terrainColumn.archetype = col.archetype;
            terrainColumn.secondaryArchetype = col.secondaryArchetype;
            terrainColumn.archetypeBlend = col.archetypeBlend;
            terrainColumn.basin = col.basin;
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

            // Snow cover
            if (height >= bprops.snowLine && bprops.snowLine < Config::SNOW_LINE_DISABLED) {
                chunk.blockAt(x, height, z) =
                    static_cast<uint8_t>(BlockId::SNOW);
            }

            // Water fill
            int waterTop = col.waterLevel;
            if (height < waterTop) {
                for (int y = height + 1; y <= waterTop; ++y) {
                    if (y < Config::WORLD_MAX_Y) {
                        chunk.blockAt(x, y, z) =
                            static_cast<uint8_t>(BlockId::WATER);
                    }
                }
                // Ice in cold biomes
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

    // Apply the precomputed cave classification after terrain fill.
    for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
            int wx = wxBase + x, wz = wzBase + z;
            for (int y = Config::CAVE_MIN_Y; y < Config::WORLD_MAX_Y; ++y) {
                const auto& surfaceColumn = m_regionData.col(
                    pad + localCX * Config::CHUNK_SIZE_X + x,
                    pad + localCZ * Config::CHUNK_SIZE_Z + z);
                if (y > surfaceColumn.height - Config::CAVE_DRY_ROOF) continue;
                CaveCell cell = m_regionData.caves.get(wx, y, wz);
                if (cell == CaveCell::Solid) continue;
                BlockId existing = chunk.getBlock(x, y, z);
                bool carveable = existing == BlockId::STONE || existing == BlockId::DEEPSLATE ||
                                 existing == BlockId::DIRT || existing == BlockId::SAND ||
                                 existing == BlockId::GRASS || existing == BlockId::SNOW;
                if (!carveable) continue;
                BlockId replacement = cell == CaveCell::Water ? BlockId::WATER :
                                      cell == CaveCell::Lava ? BlockId::LAVA : BlockId::AIR;
                chunk.setBlock(x, y, z, replacement);
            }

            // Ores only replace rock that remains after carving.
            for (int y = Config::BEDROCK_LEVEL + 1; y < Config::WORLD_MAX_Y; ++y) {
                BlockId existing = chunk.getBlock(x, y, z);
                if (existing != BlockId::STONE && existing != BlockId::DEEPSLATE) continue;
                BlockId ore = m_oreGenerator.getOre(static_cast<float>(wx) + 0.5f,
                    static_cast<float>(y) + 0.5f, static_cast<float>(wz) + 0.5f, existing);
                if (ore != BlockId::AIR) chunk.setBlock(x, y, z, ore);
            }

            const auto& decoCol = m_regionData.col(
                pad + localCX * Config::CHUNK_SIZE_X + x,
                pad + localCZ * Config::CHUNK_SIZE_Z + z);
            if (decoCol.height + 1 < Config::WORLD_MAX_Y &&
                chunk.getBlock(x, decoCol.height, z) != BlockId::AIR &&
                chunk.getBlock(x, decoCol.height + 1, z) == BlockId::AIR) {
                BlockId decoration = SurfaceRules::decoration(
                    m_seed, wx, wz, decoCol.height, decoCol.biome, decoCol.isRiver);
                if (decoration != BlockId::AIR) {
                    const int featureHeight = SurfaceRules::decorationHeight(
                        m_seed, wx, wz, decoCol.height, decoration);
                    for (int dy = 1; dy <= featureHeight; ++dy)
                        if (decoCol.height + dy < Config::WORLD_MAX_Y)
                            chunk.setBlock(x, decoCol.height + dy, z, decoration);
                    if (decoration == BlockId::SUNFLOWER_BOTTOM &&
                        decoCol.height + 2 < Config::WORLD_MAX_Y)
                        chunk.setBlock(x, decoCol.height + 2, z,
                                       BlockId::SUNFLOWER_TOP);
                }
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase 3c: Place structure blocks across the whole region
// ═══════════════════════════════════════════════════════════════════════════

void RegionGenerator::placeStructures(
    std::vector<Chunk*>& chunks,
    std::vector<RegionGenerationData::PendingBlock>& pendingOut) {
    const int regionSize = m_regionSizeChunks;
    const auto floorChunk = [](int value) {
        return value >= 0 ? value / Config::CHUNK_SIZE_X
                          : -((-value + Config::CHUNK_SIZE_X - 1) /
                              Config::CHUNK_SIZE_X);
    };
    const auto needsEntity = [](BlockId id) {
        return id == BlockId::CHEST || id == BlockId::FURNACE;
    };
    const auto write = [&](int worldX, int worldY, int worldZ, BlockId id) {
        if (!Config::isValidWorldY(worldY)) return;
        const int localX = worldX - m_regionData.worldOriginX;
        const int localZ = worldZ - m_regionData.worldOriginZ;
        const int chunkCX = floorChunk(localX);
        const int chunkCZ = floorChunk(localZ);
        if (chunkCX >= 0 && chunkCX < regionSize &&
            chunkCZ >= 0 && chunkCZ < regionSize) {
            Chunk* target = chunks[static_cast<size_t>(chunkCZ) *
                                   static_cast<size_t>(regionSize) + chunkCX];
            if (target == nullptr) return;
            const int inX = worldX - target->worldX();
            const int inZ = worldZ - target->worldZ();
            if (inX >= 0 && inX < Config::CHUNK_SIZE_X &&
                inZ >= 0 && inZ < Config::CHUNK_SIZE_Z)
                target->setBlock(inX, worldY, inZ, id);
            // Work blocks need a runtime block entity.  Route them through
            // the pending channel as an entity registration request even
            // though the block itself was already written above.
            if (needsEntity(id))
                pendingOut.push_back({worldX, worldY, worldZ, id, true, true});
        } else {
            pendingOut.push_back(
                {worldX, worldY, worldZ, id, true, needsEntity(id)});
        }
    };
    for (const StructurePlacement& placement : m_structures)
        StructureGenerator::build(placement, write);
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase 4: Finalize chunks (recompute maxY, set flags, mark dirty)
// ═══════════════════════════════════════════════════════════════════════════

void RegionGenerator::finalizeChunks(std::vector<Chunk*>& chunks) {
    for (auto* chunk : chunks) {
        // columnMaxY is already correctly maintained by Chunk::setBlock()
        // during all prior generation phases (populate, trees, cave carving).
        // No rescan needed — save ~295K reads per region.
        chunk->generated = true;
        chunk->generationInProgress = false;
        chunk->markDirty();
    }
}
