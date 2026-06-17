#include "world/RegionGenerator.h"
#include "world/HeightPipeline.h"
#include "world/CaveGenerator.h"
#include "world/TreeGenerator.h"
#include "world/OreGenerator.h"
#include "world/BiomeMap.h"
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

    // Phase 1b: Pre-compute worm tunnel segments for region + padding
    precomputeCaves();

    // Phase 2: Region-wide tree placement
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
                if (ly < 0 || ly >= Config::CHUNK_SIZE_Y) return;

                int worldX = chunkWorldX + lx;
                int worldZ = chunkWorldZ + lz;

                // Compute which region-local chunk this leaf is in
                int leafCX = (worldX - m_regionData.worldOriginX) / Config::CHUNK_SIZE_X;
                int leafCZ = (worldZ - m_regionData.worldOriginZ) / Config::CHUNK_SIZE_Z;

                if (leafCX >= 0 && leafCX < regionSizeChunks &&
                    leafCZ >= 0 && leafCZ < regionSizeChunks) {
                    // Leaf is within the region — set directly
                    size_t leafChunkIdx = static_cast<size_t>(leafCZ) * static_cast<size_t>(regionSizeChunks) + static_cast<size_t>(leafCX);
                    Chunk& leafChunk = *chunks[leafChunkIdx];
                    int llx = worldX - leafChunk.worldX();
                    int llz = worldZ - leafChunk.worldZ();
                    if (llx >= 0 && llx < 16 && llz >= 0 && llz < 16) {
                        BlockId cur = leafChunk.getBlock(llx, ly, llz);
                        if (cur == BlockId::AIR || cur == BlockId::LEAVES ||
                            cur == BlockId::SNOW) {
                            leafChunk.setBlock(llx, ly, llz, id);
                        }
                    }
                } else {
                    // Leaf is outside the region — queue as pending
                    pendingOut.push_back({worldX, ly, worldZ, id});
                }
            };

            // Place trunk (only within the owning chunk)
            for (int y = baseY; y < baseY + tp.trunkHeight; ++y) {
                if (trunkLX >= 0 && trunkLX < 16 && trunkLZ >= 0 && trunkLZ < 16 &&
                    y >= 0 && y < Config::CHUNK_SIZE_Y) {
                    BlockId cur = chunk.getBlock(trunkLX, y, trunkLZ);
                    if (cur != BlockId::WATER) {
                        chunk.setBlock(trunkLX, y, trunkLZ, BlockId::WOOD);
                    }
                }
            }

            // Place canopy via setLeaf (handles cross-chunk within region)
            int hash = (tp.localX * 7919 + tp.localZ * 6287 + tp.baseY * 3313) & 0x7FFFFFFF;

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
                            y >= 0 && y < Config::CHUNK_SIZE_Y) {
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

    // Phase 4: Region-wide cave connectivity pass
    caveConnectivityPass(chunks);

    // Phase 5: Finalize all chunks
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
// Phase 1b: Pre-compute worm tunnel segments for this region
// ═══════════════════════════════════════════════════════════════════════════

void RegionGenerator::precomputeCaves() {
    int pad = m_regionData.padding;

    // Compute cave Y band from the region's column data
    int caveMinY = Config::CAVE_MIN_Y;
    int caveMaxY = Config::CHUNK_SIZE_Y - Config::CAVE_TOP_MARGIN;

    // Generate tunnels covering the padded region's world-coordinate extent
    int worldMinX = m_regionData.worldOriginX - pad;
    int worldMinZ = m_regionData.worldOriginZ - pad;
    int worldMaxX = m_regionData.worldOriginX + m_regionData.regionSizeBlocks + pad - 1;
    int worldMaxZ = m_regionData.worldOriginZ + m_regionData.regionSizeBlocks + pad - 1;

    m_regionData.tunnels = m_caveGenerator.generateTunnels(
        worldMinX, caveMinY, worldMinZ,
        worldMaxX, caveMaxY, worldMaxZ);

    // Build spatial index for O(1) tunnel segment lookup per block
    m_regionData.caveIndex.build(
        m_regionData.tunnels,
        static_cast<float>(worldMinX), static_cast<float>(caveMinY), static_cast<float>(worldMinZ),
        static_cast<float>(worldMaxX), static_cast<float>(caveMaxY), static_cast<float>(worldMaxZ));
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

            // Bedrock layer
            for (int y = 0; y <= Config::BEDROCK_LEVEL; ++y) {
                chunk.setBlock(x, y, z, BlockId::BEDROCK);
            }

            // Stone fill
            int subsoilTop = height - 3;
            int stoneStart = Config::BEDROCK_LEVEL + 1;
            int stoneEnd   = std::max(stoneStart, subsoilTop);
            for (int y = stoneStart; y < stoneEnd; ++y) {
                chunk.setBlock(x, y, z, BlockId::STONE);
            }

            // Deepslate replacement
            for (int y = stoneStart; y <= Config::DEEPSLATE_DEPTH && y < stoneEnd; ++y) {
                chunk.setBlock(x, y, z, BlockId::DEEPSLATE);
            }

            // Ore generation
            float wx = static_cast<float>(wxBase + x) + 0.5f;
            float wz = static_cast<float>(wzBase + z) + 0.5f;
            for (int y = stoneStart; y < stoneEnd; ++y) {
                BlockId existing = chunk.getBlock(x, y, z);
                BlockId ore = m_oreGenerator.getOre(wx, static_cast<float>(y) + 0.5f, wz, existing);
                if (ore != BlockId::AIR) {
                    chunk.setBlock(x, y, z, ore);
                }
            }

            // Lava pools at depth
            for (int y = Config::LAVA_POOL_MIN_Y; y <= Config::LAVA_POOL_MAX_Y && y < stoneEnd; ++y) {
                BlockId existing = chunk.getBlock(x, y, z);
                if (existing == BlockId::STONE || existing == BlockId::DEEPSLATE) {
                    uint64_t ph = static_cast<uint64_t>(x) * 6364136223846793005ULL
                                + static_cast<uint64_t>(y) * 1442695040888963407ULL
                                + static_cast<uint64_t>(z) * 3487395720957301753ULL + m_seed;
                    if (static_cast<float>(ph & 0xFFFF) / 65536.0f < Config::LAVA_POOL_CHANCE) {
                        chunk.setBlock(x, y, z, BlockId::LAVA);
                    }
                }
            }

            // Subsoil
            for (int y = stoneEnd; y < height; ++y) {
                chunk.setBlock(x, y, z, bprops.subsoilBlock);
            }

            // Surface block
            chunk.setBlock(x, height, z, bprops.surfaceBlock);

            // Snow cover
            if (height >= bprops.snowLine && bprops.snowLine < Config::SNOW_LINE_DISABLED) {
                chunk.setBlock(x, height, z, BlockId::SNOW);
            }

            // Water fill
            int waterTop = bprops.waterLevel;
            if (height < waterTop) {
                for (int y = height + 1; y <= waterTop; ++y) {
                    if (y < Config::CHUNK_SIZE_Y) {
                        chunk.setBlock(x, y, z, BlockId::WATER);
                    }
                }
                // Ice in cold biomes
                if ((biome == Biome::SNOW_TUNDRA || biome == Biome::TAIGA) &&
                    height + 1 <= Config::ICE_FREEZE_MAX_Y) {
                    chunk.setBlock(x, height + 1, z, BlockId::ICE);
                }
            }
        }
    }

    // Cave carving (per-column, deterministic from world coords)
    for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
            int regionLX = pad + localCX * Config::CHUNK_SIZE_X + x;
            int regionLZ = pad + localCZ * Config::CHUNK_SIZE_Z + z;
            int height = m_regionData.col(regionLX, regionLZ).height;

            int caveMinY = Config::CAVE_MIN_Y;
            int caveMaxY = std::min(height - Config::CAVE_CEILING_MARGIN,
                                    Config::CHUNK_SIZE_Y - Config::CAVE_TOP_MARGIN);
            if (caveMaxY <= caveMinY) continue;

            float wx = static_cast<float>(wxBase + x) + 0.5f;
            float wz = static_cast<float>(wzBase + z) + 0.5f;
            int carvedInColumn = 0;
            int maxCarveInColumn = (caveMaxY - caveMinY) * Config::CAVE_MAX_CARVE_RATIO / 100;

            for (int y = caveMinY; y < caveMaxY; ++y) {
                float wy = static_cast<float>(y) + 0.5f;
                if (m_caveGenerator.isInTunnel(wx, wy, wz, m_regionData.caveIndex, m_regionData.tunnels) ||
                    m_caveGenerator.isInRoom(wx, wy, wz, height)) {
                    BlockId existing = chunk.getBlock(x, y, z);
                    if (existing == BlockId::STONE ||
                        existing == BlockId::DIRT  ||
                        existing == BlockId::SAND  ||
                        existing == BlockId::DEEPSLATE) {
                        if (carvedInColumn < maxCarveInColumn) {
                            chunk.setBlock(x, y, z, BlockId::AIR);
                            ++carvedInColumn;
                        }
                    }
                }
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase 4: Region-wide cave connectivity pass
// ═══════════════════════════════════════════════════════════════════════════

void RegionGenerator::caveConnectivityPass(const std::vector<Chunk*>& chunks) {
    constexpr int kConnectPasses = 2;
    constexpr int kMinAirNeighbors = 3;
    int nChunks = m_regionSizeChunks;
    int pad = m_regionData.padding;

    // Region world-coordinate bounds for neighbor guards
    int regionMinX = m_regionData.worldOriginX;
    int regionMaxX = m_regionData.worldOriginX + m_regionData.regionSizeBlocks;
    int regionMinZ = m_regionData.worldOriginZ;
    int regionMaxZ = m_regionData.worldOriginZ + m_regionData.regionSizeBlocks;

    // Helper: get block at world coords within the region
    // Returns AIR if outside all region chunks
    auto getRegionBlock = [&](int worldX, int worldY, int worldZ) -> BlockId {
        if (worldY < 0 || worldY >= Config::CHUNK_SIZE_Y) return BlockId::AIR;

        // Determine which region-local chunk
        int lcx = (worldX - m_regionData.worldOriginX) / Config::CHUNK_SIZE_X;
        int lcz = (worldZ - m_regionData.worldOriginZ) / Config::CHUNK_SIZE_Z;
        if (lcx < 0 || lcx >= nChunks || lcz < 0 || lcz >= nChunks) return BlockId::AIR;

        size_t chunkIdx = static_cast<size_t>(lcz) * static_cast<size_t>(nChunks) + static_cast<size_t>(lcx);
        Chunk& c = *chunks[chunkIdx];
        int lx = worldX - c.worldX();
        int lz = worldZ - c.worldZ();
        if (lx < 0 || lx >= 16 || lz < 0 || lz >= 16) return BlockId::AIR;
        return c.getBlock(lx, worldY, lz);
    };

    for (int pass = 0; pass < kConnectPasses; ++pass) {
        for (int lcz = 0; lcz < nChunks; ++lcz) {
            for (int lcx = 0; lcx < nChunks; ++lcx) {
                size_t idx = static_cast<size_t>(lcz) * static_cast<size_t>(nChunks) + static_cast<size_t>(lcx);
                Chunk& chunk = *chunks[idx];
                int wxBase = chunk.worldX();
                int wzBase = chunk.worldZ();

                for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
                    for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
                        int regionLX = pad + lcx * Config::CHUNK_SIZE_X + x;
                        int regionLZ = pad + lcz * Config::CHUNK_SIZE_Z + z;
                        int height = m_regionData.col(regionLX, regionLZ).height;

                        int caveMinY = Config::CAVE_MIN_Y;
                        int caveMaxY = std::min(height - Config::CAVE_CEILING_MARGIN,
                                                Config::CHUNK_SIZE_Y - Config::CAVE_TOP_MARGIN);
                        if (caveMaxY <= caveMinY) continue;

                        int carvedInColumn = 0;
                        int maxCarveInColumn = (caveMaxY - caveMinY) * Config::CAVE_CONNECT_CARVE_RATIO / 100;

                        for (int y = caveMinY; y < caveMaxY; ++y) {
                            BlockId cur = chunk.getBlock(x, y, z);
                            if (cur != BlockId::STONE && cur != BlockId::DIRT &&
                                cur != BlockId::SAND && cur != BlockId::DEEPSLATE) {
                                continue;
                            }

                            // Count air neighbors on all 6 faces
                            // Horizontal neighbors: skip if outside region bounds (matches legacy path)
                            int airCount = 0;
                            int nxWorld = wxBase + x - 1;
                            if (nxWorld >= regionMinX && getRegionBlock(nxWorld, y, wzBase + z) == BlockId::AIR) ++airCount;
                            nxWorld = wxBase + x + 1;
                            if (nxWorld < regionMaxX && getRegionBlock(nxWorld, y, wzBase + z) == BlockId::AIR) ++airCount;
                            if (y > 0                    && chunk.getBlock(x, y - 1, z) == BlockId::AIR) ++airCount;
                            if (y < Config::CHUNK_SIZE_Y-1 && chunk.getBlock(x, y + 1, z) == BlockId::AIR) ++airCount;
                            int nzWorld = wzBase + z - 1;
                            if (nzWorld >= regionMinZ && getRegionBlock(wxBase + x, y, nzWorld) == BlockId::AIR) ++airCount;
                            nzWorld = wzBase + z + 1;
                            if (nzWorld < regionMaxZ && getRegionBlock(wxBase + x, y, nzWorld) == BlockId::AIR) ++airCount;

                            if (airCount >= kMinAirNeighbors && carvedInColumn < maxCarveInColumn) {
                                chunk.setBlock(x, y, z, BlockId::AIR);
                                ++carvedInColumn;
                            }
                        }
                    }
                }
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase 5: Finalize chunks (recompute maxY, set flags, mark dirty)
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
