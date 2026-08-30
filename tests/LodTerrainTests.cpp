#include "game/SaveStore.h"
#include "threading/ThreadPool.h"
#include "world/LodSettings.h"
#include "world/LodTerrainSystem.h"
#include "world/WorldGenerator.h"

#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

bool sameTile(const LodTileData& a, const LodTileData& b) {
    for (size_t i = 0; i < a.columns.size(); ++i) {
        if (a.columns[i].exact != b.columns[i].exact ||
            a.columns[i].spans.size() != b.columns[i].spans.size()) return false;
        for (size_t j = 0; j < a.columns[i].spans.size(); ++j) {
            const LodSpan& x = a.columns[i].spans[j];
            const LodSpan& y = b.columns[i].spans[j];
            if (x.bottom != y.bottom || x.top != y.top || x.block != y.block)
                return false;
        }
    }
    return true;
}

bool isLodTreeBlock(BlockId block) {
    return block == BlockId::LEAVES || block == BlockId::BIRCH_LEAVES ||
           block == BlockId::SPRUCE_LEAVES || block == BlockId::JUNGLE_LEAVES ||
           block == BlockId::ACACIA_LEAVES || block == BlockId::CACTUS_BLOCK;
}

int blockIndex(int x, int y, int z) {
    return x + z * Config::CHUNK_SIZE_X +
        Config::worldYToStorageY(y) * Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z;
}

ChunkMesh singleBlockLodMesh(BlockId block) {
    std::vector<uint8_t> blocks(Config::CHUNK_VOLUME, 0);
    blocks[blockIndex(8, 64, 8)] = static_cast<uint8_t>(block);
    return buildLodTileMesh(extractExactLodChunk(blocks), 1, 24);
}

float meshMinimumY(const ChunkMesh& mesh) {
    float value = 10000.0f;
    for (const MeshVertex& vertex : mesh.vertices) value = std::min(value, vertex.py);
    return value;
}

float meshMaximumY(const ChunkMesh& mesh) {
    float value = -10000.0f;
    for (const MeshVertex& vertex : mesh.vertices) value = std::max(value, vertex.py);
    return value;
}
}

int main() {
    WorldGenerator normal(123456789ULL, WorldType::Normal, DimensionId::Overworld);
    const LodTileKey negative{-3, 2, 2};
    const LodTileData first = buildApproximateLodTile(normal, negative);
    const ChunkMesh compactApproximate = buildLodTileMesh(first, 4, 24);
    require(compactApproximate.uploadBytes() < 160u * 1024u,
            "ordinary LOD tiles retain enough GPU budget for outer rings");
    const LodTileData repeated = buildApproximateLodTile(normal, negative);
    require(sameTile(first, repeated),
            "LOD approximation is deterministic at negative coordinates");
    WorldGenerator other(987654321ULL, WorldType::Normal, DimensionId::Overworld);
    require(!sameTile(first, buildApproximateLodTile(other, negative)),
            "LOD approximation varies with the world seed");

    bool foundTree = false;
    bool foundSubCellWater = false;
    for (int tz = -3; tz <= 3 && (!foundTree || !foundSubCellWater); ++tz) {
        for (int tx = -3; tx <= 3 && (!foundTree || !foundSubCellWater); ++tx) {
            constexpr int level = 2;
            constexpr int cellSize = 1 << level;
            const LodTileKey key{tx, tz, level};
            const LodTileData tile = buildApproximateLodTile(normal, key);
            const int originX = tx * LodTileData::SIDE * cellSize;
            const int originZ = tz * LodTileData::SIDE * cellSize;
            for (int z = 0; z < LodTileData::SIDE; ++z) {
                for (int x = 0; x < LodTileData::SIDE; ++x) {
                    bool hasWater = false;
                    for (const LodSpan& span : tile.at(x, z).spans) {
                        foundTree = foundTree || isLodTreeBlock(span.block);
                        hasWater = hasWater || isWater(span.block);
                    }
                    const SurfaceColumn center = normal.sampleTerrainColumn(
                        originX + x * cellSize + cellSize / 2,
                        originZ + z * cellSize + cellSize / 2);
                    if (hasWater && center.waterLevel <= center.height)
                        foundSubCellWater = true;
                }
            }
        }
    }
    require(foundTree,
            "fine approximate LOD retains deterministic tree silhouettes");
    require(foundSubCellWater,
            "fine approximate LOD retains water missed by its center sample");
    LodColumn recoveredWater;
    recoveredWater.spans.push_back({58, 61, BlockId::SAND});
    recoveredWater.spans.push_back({62, 62, BlockId::WATER});
    LodColumn dryExact;
    dryExact.exact = true;
    dryExact.spans.push_back({58, 62, BlockId::SAND});
    refineLodColumn(recoveredWater, dryExact, 4);
    require(!recoveredWater.exact && recoveredWater.spans.size() == 2 &&
            recoveredWater.spans.back().block == BlockId::WATER,
            "exact center refinement preserves recovered sub-cell water");
    refineLodColumn(recoveredWater, dryExact, 1);
    require(recoveredWater.exact && recoveredWater.spans.size() == 1 &&
            recoveredWater.spans.front().block == BlockId::SAND,
            "one-block LOD accepts exact dry terrain refinement");
    const LodTileData coarseTrees = buildApproximateLodTile(normal, {0, 0, 4});
    for (const LodColumn& column : coarseTrees.columns) {
        for (const LodSpan& span : column.spans)
            require(!isLodTreeBlock(span.block),
                    "coarse LOD does not magnify individual trees into giant cubes");
    }

    WorldGenerator flat(5, WorldType::Superflat, DimensionId::Overworld);
    const LodTileData flatTile = buildApproximateLodTile(flat, {0, 0, 1});
    const int flatTop = flatTile.columns.front().spans.front().top;
    for (const LodColumn& column : flatTile.columns)
        require(!column.spans.empty() && column.spans.front().top == flatTop,
                "superflat LOD retains a constant surface");

    WorldGenerator heaven(77, WorldType::Normal, DimensionId::Heaven);
    int maximumHeavenLayers = 0;
    for (int tz = -4; tz <= 4; ++tz) {
        for (int tx = -4; tx <= 4; ++tx) {
            const LodTileData tile = buildApproximateLodTile(heaven, {tx, tz, 1});
            for (const LodColumn& column : tile.columns)
                maximumHeavenLayers = std::max(
                    maximumHeavenLayers, static_cast<int>(column.spans.size()));
        }
    }
    require(maximumHeavenLayers >= 2 && maximumHeavenLayers <= 5,
            "Heaven LOD preserves multiple independently sampled island layers");

    std::vector<uint8_t> blocks(Config::CHUNK_VOLUME, 0);
    for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
        for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
            for (int y = 0; y <= 10; ++y)
                blocks[blockIndex(x, y, z)] = static_cast<uint8_t>(BlockId::STONE);
        }
    }
    for (int y = 20; y <= 23; ++y)
        blocks[blockIndex(0, y, 0)] = static_cast<uint8_t>(BlockId::WOOD);
    blocks[blockIndex(0, 30, 0)] = static_cast<uint8_t>(BlockId::WATER);
    const LodTileData exact = extractExactLodChunk(blocks);
    require(exact.at(0, 0).exact && exact.at(0, 0).spans.size() == 3 &&
            exact.at(0, 0).spans[1].bottom == 20 &&
            exact.at(0, 0).spans[2].block == BlockId::WATER,
            "exact LOD extraction retains separated solid and translucent spans");
    const ChunkMesh exactMesh = buildLodTileMesh(exact, 1, 24);
    require(exactMesh.opaqueIndexCount > 0 &&
            exactMesh.translucentIndexCount > 0 &&
            exactMesh.translucentIndexOffset == exactMesh.opaqueIndexCount,
            "LOD mesh hands off opaque and translucent index ranges");
    require(exactMesh.vertices.size() < exactMesh.indices.size(),
            "LOD faces share vertices instead of duplicating triangle corners");

    std::vector<uint8_t> adjacentTypes(Config::CHUNK_VOLUME, 0);
    adjacentTypes[blockIndex(1, 20, 1)] = static_cast<uint8_t>(BlockId::STONE);
    adjacentTypes[blockIndex(1, 21, 1)] =
        static_cast<uint8_t>(BlockId::CRAFTING_TABLE);
    adjacentTypes[blockIndex(1, 22, 1)] = static_cast<uint8_t>(BlockId::WHEAT_7);
    adjacentTypes[blockIndex(1, 23, 1)] = static_cast<uint8_t>(BlockId::GLASS);
    adjacentTypes[blockIndex(1, 24, 1)] = static_cast<uint8_t>(BlockId::WATER);
    const LodTileData adjacentExact = extractExactLodChunk(adjacentTypes);
    const LodColumn& adjacentColumn = adjacentExact.at(1, 1);
    require(adjacentColumn.spans.size() == 5 &&
            adjacentColumn.spans[0].block == BlockId::STONE &&
            adjacentColumn.spans[1].block == BlockId::CRAFTING_TABLE &&
            adjacentColumn.spans[2].block == BlockId::WHEAT_7 &&
            adjacentColumn.spans[3].block == BlockId::GLASS &&
            adjacentColumn.spans[4].block == BlockId::WATER,
            "exact LOD merges adjacent block IDs that share a render layer");

    std::vector<uint8_t> allBlocks(Config::CHUNK_VOLUME, 0);
    int allBlockY = Config::WORLD_MIN_Y;
    for (int raw = 1; raw < static_cast<int>(BlockId::COUNT); ++raw) {
        allBlocks[blockIndex(8, allBlockY, 8)] = static_cast<uint8_t>(raw);
        allBlockY += 2;
    }
    const LodTileData allBlockTile = extractExactLodChunk(allBlocks);
    const LodColumn& allBlockColumn = allBlockTile.at(8, 8);
    require(allBlockColumn.spans.size() == static_cast<size_t>(
                static_cast<int>(BlockId::COUNT) - 1),
            "exact LOD did not retain every non-air BlockId");
    for (int raw = 1; raw < static_cast<int>(BlockId::COUNT); ++raw)
        require(allBlockColumn.spans[static_cast<size_t>(raw - 1)].block ==
                    static_cast<BlockId>(raw),
                "exact LOD changed a retained BlockId");
    const std::vector<uint8_t> allBlockPayload =
        encodeLodTilePayload(allBlockTile);
    LodTileData decodedAllBlocks;
    require(decodeLodTilePayload(allBlockPayload, decodedAllBlocks) &&
            sameTile(allBlockTile, decodedAllBlocks) &&
            decodedAllBlocks.at(8, 8).spans.size() > 24,
            "r3 LOD payload does not round-trip columns beyond 24 runs");
    const ChunkMesh allBlockMesh = buildLodTileMesh(decodedAllBlocks, 1, 24);
    require(allBlockMesh.opaqueIndexCount > 0 &&
            allBlockMesh.translucentIndexCount > 0 &&
            allBlockMesh.shadowCasterIndexCount == 0,
            "all-block exact LOD lacks a visible material layer or retained shadows");
    for (int raw = 1; raw < static_cast<int>(BlockId::COUNT); ++raw) {
        const BlockId id = static_cast<BlockId>(raw);
        bool foundTexture = false;
        for (const MeshVertex& vertex : allBlockMesh.vertices) {
            const int tile = static_cast<int>(std::floor(vertex.tile));
            for (int face = 0; face < FACE_COUNT; ++face)
                foundTexture = foundTexture || tile == getAtlasTextureIndex(
                    getFaceTexture(id, static_cast<FaceDir>(face)));
        }
        require(foundTexture, "an exact LOD BlockId emitted no registered material");
    }

    const ChunkMesh crossMesh = singleBlockLodMesh(BlockId::DANDELION);
    const ChunkMesh snowMesh = singleBlockLodMesh(BlockId::SNOW_LAYER);
    const ChunkMesh bedMesh = singleBlockLodMesh(BlockId::WHITE_BED);
    const ChunkMesh slabMesh = singleBlockLodMesh(BlockId::PLANKS_SLAB_BOTTOM);
    const ChunkMesh stairMesh = singleBlockLodMesh(BlockId::PLANKS_STAIRS_BOTTOM_NORTH);
    const ChunkMesh flowingMesh = singleBlockLodMesh(BlockId::FLOWING_WATER_7);
    require(crossMesh.opaqueIndexCount >= 24 &&
            std::abs(meshMaximumY(snowMesh) - meshMinimumY(snowMesh) - 0.125f) < 0.001f &&
            meshMaximumY(bedMesh) - meshMinimumY(bedMesh) <= 9.0f / 16.0f + 0.001f &&
            std::abs(meshMaximumY(slabMesh) - meshMinimumY(slabMesh) - 0.5f) < 0.001f &&
            stairMesh.vertices.size() > slabMesh.vertices.size() &&
            flowingMesh.translucentIndexCount > 0 &&
            meshMaximumY(flowingMesh) - meshMinimumY(flowingMesh) < 1.0f,
            "exact LOD does not preserve registered special block geometry");

    LodTileData coarseDecoration;
    coarseDecoration.at(0, 0).spans.push_back({64, 64, BlockId::TORCH});
    require(buildLodTileMesh(coarseDecoration, 4, 24).empty(),
            "coarse LOD magnifies a one-block decoration across its full cell");

    std::vector<uint8_t> edgeCenterBlocks(Config::CHUNK_VOLUME, 0);
    std::vector<uint8_t> edgeEastBlocks(Config::CHUNK_VOLUME, 0);
    edgeCenterBlocks[blockIndex(15, 64, 8)] = static_cast<uint8_t>(BlockId::STONE);
    edgeEastBlocks[blockIndex(0, 64, 8)] = static_cast<uint8_t>(BlockId::STONE);
    const LodTileData edgeCenter = extractExactLodChunk(edgeCenterBlocks);
    const LodTileData edgeEast = extractExactLodChunk(edgeEastBlocks);
    LodExactNeighborTiles edgeNeighbors;
    for (size_t i = 0; i < ChunkMesh::NEIGHBOR_DEPENDENCY_OFFSETS.size(); ++i) {
        const auto& offset = ChunkMesh::NEIGHBOR_DEPENDENCY_OFFSETS[i];
        if (offset[0] == 1 && offset[1] == 0) edgeNeighbors[i] = edgeEast;
    }
    const ChunkMesh openEdgeMesh = buildLodTileMesh(edgeCenter, 1, 24);
    const ChunkMesh joinedEdgeMesh = buildLodTileMesh(
        edgeCenter, 1, 24, &edgeNeighbors);
    require(openEdgeMesh.opaqueIndexCount == joinedEdgeMesh.opaqueIndexCount + 6,
            "neighboring exact LOD tiles retain their shared solid face");

    edgeCenterBlocks.assign(Config::CHUNK_VOLUME, 0);
    edgeCenterBlocks[blockIndex(15, 64, 8)] = static_cast<uint8_t>(BlockId::WATER);
    const LodTileData edgeWater = extractExactLodChunk(edgeCenterBlocks);
    const ChunkMesh safeWaterEdge = buildLodTileMesh(edgeWater, 1, 24);
    require(safeWaterEdge.translucentIndexCount < 36,
            "missing exact neighbor creates a full fluid wall at a tile edge");

    edgeCenterBlocks.assign(Config::CHUNK_VOLUME, 0);
    edgeEastBlocks.assign(Config::CHUNK_VOLUME, 0);
    edgeCenterBlocks[blockIndex(15, 64, 8)] =
        static_cast<uint8_t>(BlockId::WHITE_BED_FOOT_EAST);
    edgeEastBlocks[blockIndex(0, 64, 8)] =
        static_cast<uint8_t>(BlockId::WHITE_BED_HEAD_EAST);
    const LodTileData edgeBed = extractExactLodChunk(edgeCenterBlocks);
    edgeNeighbors = {};
    const LodTileData edgeBedPartner = extractExactLodChunk(edgeEastBlocks);
    for (size_t i = 0; i < ChunkMesh::NEIGHBOR_DEPENDENCY_OFFSETS.size(); ++i) {
        const auto& offset = ChunkMesh::NEIGHBOR_DEPENDENCY_OFFSETS[i];
        if (offset[0] == 1 && offset[1] == 0) edgeNeighbors[i] = edgeBedPartner;
    }
    require(buildLodTileMesh(edgeBed, 1, 24, &edgeNeighbors).opaqueIndexCount <
                buildLodTileMesh(edgeBed, 1, 24).opaqueIndexCount,
            "cross-tile exact bed does not suppress its paired seam");

    LodTileData ocean;
    for (LodColumn& column : ocean.columns)
        column.spans.push_back({40, 62, BlockId::WATER});
    const ChunkMesh oceanMesh = buildLodTileMesh(ocean, 8, 24);
    require(oceanMesh.opaqueIndexCount == 0 &&
            oceanMesh.translucentIndexCount ==
                LodTileData::SIDE * LodTileData::SIDE * 6,
            "LOD oceans omit artificial full-depth walls at tile boundaries");

    require(lodHorizontalQuality(LodPrecision::Low) == 64 &&
            lodHorizontalQuality(LodPrecision::Ultra) == 144 &&
            lodVerticalSpanLimit(LodPrecision::Medium) >= 5,
            "precision presets map to fixed horizontal and vertical quality");
    require(lodWorkBudget(LodAggressiveness::PowerSaver).maxInFlight == 1 &&
            lodWorkBudget(LodAggressiveness::Extreme).maxInFlight == 8 &&
            lodWorkBudget(LodAggressiveness::Balanced).completionMs == 1.5,
            "aggressiveness presets map to bounded work budgets");

    LodTerrainSystem selection;
    selection.reset(&normal);
    selection.configure({true, 128, LodAggressiveness::Balanced,
                         LodPrecision::Medium});
    selection.update({0.5, 80.0, 0.5}, 8, {});
    require(selection.selectedMaximumDistance() == 128.0f *
                Config::CHUNK_SIZE_X &&
            selection.selectedTileCountAtLevel(0) == 0 &&
            selection.selectedTileCount() < 800,
            "LOD selection spends its distance budget on a forced fine ring");

    const auto root = std::filesystem::temp_directory_path() /
                      "minecraftc-lod-terrain-tests";
    std::filesystem::remove_all(root);
    SaveStore store(root);
    {
        ThreadPool pool(2);
        LodTerrainSystem system;
        system.setThreadPool(&pool);
        system.setSaveStore(&store);
        system.reset(&normal);
        system.configure({true, 32, LodAggressiveness::PowerSaver,
                          LodPrecision::Low});
        system.update({0.5, 80.0, 0.5}, 8, {});
        pool.waitIdle();
        system.processCompleted(nullptr);
        require(system.residentCpuBytes() > 0,
                "asynchronous LOD completion publishes bounded CPU data");
    }
    bool cacheFound = false;
    const auto tileDirectory = root / "lod" / "r3" / "d_0" / "tiles";
    for (const auto& entry : std::filesystem::directory_iterator(tileDirectory)) {
        cacheFound = entry.is_regular_file();
        if (cacheFound) {
            std::ofstream corrupt(entry.path(), std::ios::binary | std::ios::trunc);
            corrupt << "bad";
            break;
        }
    }
    require(cacheFound, "LOD tiles persist in the per-world derived cache");
    {
        ThreadPool pool(1);
        LodTerrainSystem recovered;
        recovered.setThreadPool(&pool);
        recovered.setSaveStore(&store);
        recovered.reset(&normal);
        recovered.configure({true, 32, LodAggressiveness::PowerSaver,
                             LodPrecision::Low});
        recovered.update({0.5, 80.0, 0.5}, 8, {});
        pool.waitIdle();
        recovered.processCompleted(nullptr);
        require(recovered.residentCpuBytes() > 0,
                "corrupt LOD cache data is ignored and regenerated");
    }
    std::filesystem::remove_all(root);

    std::cout << "LOD terrain tests passed\n";
    return 0;
}
