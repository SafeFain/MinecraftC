#include "EntityAiTestRenderer.h"
#include "game/SaveStore.h"
#include "threading/ThreadPool.h"
#include "world/Chunk.h"
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

bool isLodTreeTrunk(BlockId block) {
    return block == BlockId::WOOD || block == BlockId::BIRCH_WOOD ||
           block == BlockId::SPRUCE_WOOD || block == BlockId::JUNGLE_WOOD ||
           block == BlockId::ACACIA_WOOD;
}

bool isHeavenSurfaceFeature(BlockId block) {
    return block == BlockId::SKYROOT_WOOD || block == BlockId::SKYROOT_LEAVES ||
           block == BlockId::STAR_CRYSTAL || block == BlockId::STARFLOWER ||
           block == BlockId::CLOUD_BLOOM || block == BlockId::GLOWSHROOM;
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
    for (const LodColumn& column : first.columns)
        require(!column.spans.empty() &&
                column.spans.front().bottom == Config::WORLD_MIN_Y,
                "approximate Overworld columns leave a floating ground slab");
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
    bool foundTreeTrunk = false;
    bool foundCompleteTreeColumn = false;
    bool foundSubCellWater = false;
    for (int tz = -3; tz <= 3 &&
         (!foundCompleteTreeColumn || !foundSubCellWater); ++tz) {
        for (int tx = -3; tx <= 3 &&
             (!foundCompleteTreeColumn || !foundSubCellWater); ++tx) {
            constexpr int level = 2;
            constexpr int cellSize = 1 << level;
            const LodTileKey key{tx, tz, level};
            const LodTileData tile = buildApproximateLodTile(normal, key);
            const int originX = tx * LodTileData::SIDE * cellSize;
            const int originZ = tz * LodTileData::SIDE * cellSize;
            for (int z = 0; z < LodTileData::SIDE; ++z) {
                for (int x = 0; x < LodTileData::SIDE; ++x) {
                    bool hasWater = false;
                    bool hasFoliage = false;
                    bool hasTrunk = false;
                    for (const LodSpan& span : tile.at(x, z).spans) {
                        foundTree = foundTree || isLodTreeBlock(span.block);
                        foundTreeTrunk = foundTreeTrunk || isLodTreeTrunk(span.block);
                        hasFoliage = hasFoliage || isLodTreeBlock(span.block);
                        hasTrunk = hasTrunk || isLodTreeTrunk(span.block);
                        hasWater = hasWater || isWater(span.block);
                    }
                    foundCompleteTreeColumn = foundCompleteTreeColumn ||
                        (hasFoliage && hasTrunk);
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
    require(foundTreeTrunk && foundCompleteTreeColumn,
            "fine approximate LOD retains visible tree trunks");
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
    LodColumn grounded;
    grounded.spans.push_back({Config::WORLD_MIN_Y, 100, BlockId::GRASS});
    LodColumn caveAndTree;
    caveAndTree.exact = true;
    caveAndTree.spans = {
        {Config::WORLD_MIN_Y, 25, BlockId::STONE},
        {40, 70, BlockId::STONE},
        {78, 98, BlockId::DIRT},
        {99, 100, BlockId::GRASS},
        {101, 102, BlockId::WOOD},
        {103, 104, BlockId::LEAVES},
        {105, 106, BlockId::LEAVES}
    };
    refineLodColumn(grounded, caveAndTree, 4);
    require(grounded.exact && grounded.spans.front().bottom ==
                Config::WORLD_MIN_Y && grounded.spans.front().top == 100 &&
                grounded.spans.front().block == BlockId::GRASS &&
                grounded.spans.back().top == 106,
            "coarse exact refinement removed continuous ground beneath caves");
    LodTileData cliff;
    cliff.at(7, 8).spans.push_back({Config::WORLD_MIN_Y, 55, BlockId::SAND});
    cliff.at(8, 8).spans.push_back({Config::WORLD_MIN_Y, 110, BlockId::STONE});
    const ChunkMesh cliffMesh = buildLodTileMesh(cliff, 4, 6);
    require(meshMinimumY(cliffMesh) == Config::WORLD_MIN_Y &&
                meshMaximumY(cliffMesh) == 111.0f,
            "large LOD height differences expose the bottom of a terrain slab");
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
    bool foundSkyrootWood = false;
    bool foundSkyrootLeaves = false;
    bool foundStarCrystal = false;
    bool foundStarflower = false;
    bool foundCloudBloom = false;
    bool foundGlowshroom = false;
    bool foundAetherSoil = false;
    for (int tz = -8; tz <= 8; ++tz) {
        for (int tx = -8; tx <= 8; ++tx) {
            const LodTileData tile = buildApproximateLodTile(heaven, {tx, tz, 1});
            for (const LodColumn& column : tile.columns) {
                maximumHeavenLayers = std::max(
                    maximumHeavenLayers, static_cast<int>(column.spans.size()));
                for (const LodSpan& span : column.spans) {
                    foundSkyrootWood = foundSkyrootWood ||
                        span.block == BlockId::SKYROOT_WOOD;
                    foundSkyrootLeaves = foundSkyrootLeaves ||
                        span.block == BlockId::SKYROOT_LEAVES;
                    foundStarCrystal = foundStarCrystal ||
                        span.block == BlockId::STAR_CRYSTAL;
                    foundStarflower = foundStarflower ||
                        span.block == BlockId::STARFLOWER;
                    foundCloudBloom = foundCloudBloom ||
                        span.block == BlockId::CLOUD_BLOOM;
                    foundGlowshroom = foundGlowshroom ||
                        span.block == BlockId::GLOWSHROOM;
                    foundAetherSoil = foundAetherSoil ||
                        span.block == BlockId::AETHER_SOIL;
                }
            }
        }
    }
    require(maximumHeavenLayers >= 2,
            "Heaven LOD preserves multiple independently sampled island layers");
    require(foundSkyrootWood && foundSkyrootLeaves && foundStarCrystal &&
                foundStarflower && foundCloudBloom && foundGlowshroom &&
                foundAetherSoil,
            "fine Heaven LOD omits representative surface blocks");
    const ChunkMesh heavenPlantMesh = singleBlockLodMesh(BlockId::STARFLOWER);
    LodTileData approximateHeavenPlant;
    approximateHeavenPlant.at(0, 0).spans.push_back(
        {200, 200, BlockId::STARFLOWER});
    require(!buildLodTileMesh(approximateHeavenPlant, 2, 24).empty() &&
                buildLodTileMesh(approximateHeavenPlant, 4, 24).empty(),
            "Heaven plants must remain visible only in the finest approximate LOD");
    require(!heavenPlantMesh.empty(),
            "exact Heaven plant geometry is missing");
    const LodTileData coarseHeaven = buildApproximateLodTile(heaven, {0, 0, 4});
    for (const LodColumn& column : coarseHeaven.columns)
        for (const LodSpan& span : column.spans)
            require(!isHeavenSurfaceFeature(span.block),
                    "coarse Heaven LOD magnifies a surface feature");

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
            "r4 LOD payload does not round-trip columns beyond 24 runs");
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
    const ChunkMesh jungleLeavesMesh = singleBlockLodMesh(BlockId::JUNGLE_LEAVES);
    require(crossMesh.opaqueIndexCount >= 24 &&
            std::abs(meshMaximumY(snowMesh) - meshMinimumY(snowMesh) - 0.125f) < 0.001f &&
            meshMaximumY(bedMesh) - meshMinimumY(bedMesh) <= 9.0f / 16.0f + 0.001f &&
            std::abs(meshMaximumY(slabMesh) - meshMinimumY(slabMesh) - 0.5f) < 0.001f &&
            stairMesh.vertices.size() > slabMesh.vertices.size() &&
            flowingMesh.translucentIndexCount > 0 &&
            meshMaximumY(flowingMesh) - meshMinimumY(flowingMesh) < 1.0f &&
            std::all_of(jungleLeavesMesh.vertices.begin(),
                        jungleLeavesMesh.vertices.end(),
                        [](const MeshVertex& vertex) {
                            return vertex.face >= 32.0f && vertex.face < 38.0f;
                        }),
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
    const ChunkMesh transitionEdgeMesh = buildLodTileMesh(
        edgeCenter, 1, 24, &edgeNeighbors, nullptr, true);
    require(transitionEdgeMesh.opaqueIndexCount > joinedEdgeMesh.opaqueIndexCount,
            "an exact tile has no solid wall at a precision transition");

    LodTileData flatEdge;
    flatEdge.at(15, 8).spans.push_back(
        {Config::WORLD_MIN_Y, 64, BlockId::STONE});
    LodNeighborEdges equalHeightEdges;
    equalHeightEdges[2][8].spans.push_back(
        {Config::WORLD_MIN_Y, 64, BlockId::STONE});
    const ChunkMesh ordinaryEdgeMesh = buildLodTileMesh(
        flatEdge, 4, 24, nullptr, &equalHeightEdges);
    const ChunkMesh sealedEdgeMesh = buildLodTileMesh(
        flatEdge, 4, 24, nullptr, &equalHeightEdges, true);
    require(sealedEdgeMesh.opaqueIndexCount > ordinaryEdgeMesh.opaqueIndexCount,
            "an approximate tile has no solid wall at a precision transition");

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
                LodTileData::SIDE * LodTileData::SIDE * 6 &&
            std::abs(meshMaximumY(oceanMesh) -
                (62.0f + fluidSurfaceHeight(BlockId::WATER) - 0.001f)) < 0.001f,
            "LOD oceans match exact fluid height and omit tile-boundary walls");
    LodNeighborEdges oceanEdges;
    for (auto& side : oceanEdges)
        for (LodColumn& column : side)
            column.spans.push_back({40, 62, BlockId::WATER});
    require(buildLodTileMesh(ocean, 8, 24, nullptr, &oceanEdges)
                .translucentIndexCount == oceanMesh.translucentIndexCount,
            "adjacent ocean tiles create a visible water boundary wall");
    oceanEdges[2][8].spans = {{Config::WORLD_MIN_Y, 50, BlockId::STONE}};
    require(buildLodTileMesh(ocean, 8, 24, nullptr, &oceanEdges)
                .translucentIndexCount > oceanMesh.translucentIndexCount,
            "shore tile boundary omits the exposed water side");

    LodTileData submergedBank;
    submergedBank.at(7, 8).spans.push_back(
        {Config::WORLD_MIN_Y, 64, BlockId::STONE});
    submergedBank.at(8, 8).spans.push_back(
        {Config::WORLD_MIN_Y, 58, BlockId::SAND});
    submergedBank.at(8, 8).spans.push_back({59, 62, BlockId::WATER});
    const ChunkMesh bankMesh = buildLodTileMesh(submergedBank, 4, 24);
    require(std::any_of(bankMesh.vertices.begin(), bankMesh.vertices.end(),
                        [](const MeshVertex& vertex) {
                            return vertex.face == static_cast<float>(FaceDir::RIGHT) &&
                                   vertex.px == 32.0f && vertex.py == 59.0f;
                        }),
            "translucent water hides the opaque bank side behind it");

    LodTileData shallowOcean;
    shallowOcean.at(8, 8).spans.push_back({58, 61, BlockId::SAND});
    shallowOcean.at(8, 8).spans.push_back({62, 62, BlockId::WATER});
    const ChunkMesh shallowOceanMesh = buildLodTileMesh(shallowOcean, 4, 24);
    const int waterTile = getAtlasTextureIndex(BlockTexture::Water);
    require(std::none_of(shallowOceanMesh.vertices.begin(),
                         shallowOceanMesh.vertices.end(),
                         [waterTile](const MeshVertex& vertex) {
                             return static_cast<int>(std::floor(vertex.tile)) == waterTile &&
                                    std::abs(vertex.face -
                                        static_cast<float>(FaceDir::BOTTOM)) < 0.01f;
                         }),
            "LOD water emits a coplanar bottom face over the seabed");

    LodTileData fluidBoundary;
    fluidBoundary.at(7, 8).spans.push_back({62, 62, BlockId::WATER});
    fluidBoundary.at(8, 8).spans.push_back({62, 62, BlockId::LAVA});
    const ChunkMesh fluidBoundaryMesh = buildLodTileMesh(fluidBoundary, 4, 24);
    const float boundaryX = 8.0f * 4.0f;
    require(std::any_of(fluidBoundaryMesh.vertices.begin(),
                        fluidBoundaryMesh.vertices.end(),
                        [boundaryX, waterTile](const MeshVertex& vertex) {
                            return vertex.px == boundaryX &&
                                static_cast<int>(std::floor(vertex.tile)) == waterTile &&
                                static_cast<int>(vertex.face) ==
                                    static_cast<int>(FaceDir::RIGHT);
                        }),
            "water/lava interface omits the water side face");

    LodTileData sealedWater;
    sealedWater.at(15, 8).spans.push_back({58, 62, BlockId::WATER});
    LodNeighborEdges matchingWaterEdges{};
    matchingWaterEdges[2][8].spans.push_back({58, 62, BlockId::WATER});
    const ChunkMesh sealedWaterMesh = buildLodTileMesh(
        sealedWater, 4, 24, nullptr, &matchingWaterEdges, true);
    require(std::any_of(sealedWaterMesh.vertices.begin(),
                        sealedWaterMesh.vertices.end(),
                        [](const MeshVertex& vertex) {
                            return vertex.px == 64.0f && vertex.face >= 64.0f;
                        }),
            "water tile edge lacks a precision-transition side skirt");

    LodTileData exactWater;
    for (LodColumn& column : exactWater.columns) column.exact = true;
    exactWater.at(15, 8).spans.push_back({58, 62, BlockId::WATER});
    LodExactNeighborTiles exactWaterNeighbors{};
    exactWaterNeighbors[4].emplace();
    exactWaterNeighbors[4]->at(0, 8).spans.push_back(
        {58, 62, BlockId::WATER});
    const ChunkMesh exactWaterMesh = buildLodTileMesh(
        exactWater, 1, 24, &exactWaterNeighbors, nullptr, true);
    require(std::any_of(exactWaterMesh.vertices.begin(),
                        exactWaterMesh.vertices.end(),
                        [](const MeshVertex& vertex) {
                            return vertex.px == 16.0f && vertex.face >= 64.0f;
                        }),
            "exact water tile edge lacks a precision-transition side skirt");

    LodTileData stackedIslands;
    for (int island = 0; island < 8; ++island)
        stackedIslands.at(8, 8).spans.push_back(
            {static_cast<int16_t>(island * 20),
             static_cast<int16_t>(island * 20 + 2), BlockId::STONE});
    const ChunkMesh stackedMesh = buildLodTileMesh(stackedIslands, 4, 6);
    require(meshMinimumY(stackedMesh) == 0.0f,
            "vertical span budget removes a lower floating island");

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
    std::vector<std::unique_ptr<Chunk>> readyChunks;
    std::vector<Chunk*> activeChunks;
    for (int z = -8; z <= 8; ++z) {
        for (int x = -8; x <= 8; ++x) {
            if (x * x + z * z > 64) continue;
            auto chunk = std::make_unique<Chunk>(x, z);
            chunk->lifecycle = Chunk::LifecycleState::Renderable;
            activeChunks.push_back(chunk.get());
            readyChunks.push_back(std::move(chunk));
        }
    }
    selection.update({0.5, 80.0, 0.5}, 8, activeChunks);
    require(selection.selectedMaximumDistance() == 128.0f *
                Config::CHUNK_SIZE_X,
            "LOD selection does not reach its configured outer distance");
    require(selection.selectedTileCountAtLevel(0) > 0 &&
                selection.selectedMinimumDistanceAtLevel(0) ==
                    6.0f * Config::CHUNK_SIZE_X,
            "LOD selection does not start two chunks inside real terrain");
    require(selection.selectedTileCount() < 1100,
            "LOD selection exceeds its bounded tile budget");
    // Level zero ends at 112 blocks and level one begins there. Both classify
    // complete 32-block tiles, including those at negative world coordinates.
    for (int coarseZ = -7; coarseZ <= 7; ++coarseZ) {
        for (int coarseX = -7; coarseX <= 7; ++coarseX) {
            const float coarseCenterX = (coarseX + 0.5f) * 32.0f;
            const float coarseCenterZ = (coarseZ + 0.5f) * 32.0f;
            const float coarseDistance = std::hypot(
                coarseCenterX - 8.0f, coarseCenterZ - 8.0f);
            if (coarseDistance < 112.0f) {
                for (int childZ = coarseZ * 2; childZ < coarseZ * 2 + 2; ++childZ) {
                    for (int childX = coarseX * 2; childX < coarseX * 2 + 2;
                         ++childX) {
                        const float fineDistance = std::hypot(
                            (childX + 0.5f) * 16.0f - 8.0f,
                            (childZ + 0.5f) * 16.0f - 8.0f);
                        if (fineDistance + 16.0f < 96.0f) continue;
                        require(selection.isTileSelected({childX, childZ, 0}),
                                "fine LOD tile missing at a precision boundary");
                    }
                }
            } else {
                const float parentCenterX =
                    (std::floor(coarseX / 2.0f) + 0.5f) * 64.0f;
                const float parentCenterZ =
                    (std::floor(coarseZ / 2.0f) + 0.5f) * 64.0f;
                if (std::hypot(parentCenterX - 8.0f,
                               parentCenterZ - 8.0f) >= 192.0f)
                    continue;
                require(selection.isTileSelected({coarseX, coarseZ, 1}),
                        "coarse LOD tile missing at a precision boundary");
            }
        }
    }

    const size_t readySelectionCount = selection.selectedTileCount();
    auto missing = std::find_if(activeChunks.begin(), activeChunks.end(),
        [](const Chunk* chunk) { return chunk->cx == -1 && chunk->cz == -1; });
    Chunk* pendingNearChunk = *missing;
    activeChunks.erase(missing);
    selection.update({0.5, 80.0, 0.5}, 8, activeChunks);
    require(selection.selectedMinimumDistanceAtLevel(0) == 0.0f &&
                selection.selectedTileCount() == readySelectionCount + 1,
            "an unallocated negative-coordinate near chunk has no LOD backing");
    pendingNearChunk->lifecycle = Chunk::LifecycleState::Requested;
    activeChunks.push_back(pendingNearChunk);
    selection.update({0.5, 80.0, 0.5}, 8, activeChunks);
    require(selection.selectedMinimumDistanceAtLevel(0) == 0.0f,
            "a non-renderable near chunk does not retain level-zero LOD backing");
    pendingNearChunk->lifecycle = Chunk::LifecycleState::Renderable;
    pendingNearChunk->getMesh().indexCount = 6;
    selection.update({0.5, 80.0, 0.5}, 8, activeChunks);
    require(selection.selectedMinimumDistanceAtLevel(0) == 0.0f,
            "LOD backing retires before the near mesh reaches the GPU");
    pendingNearChunk->getMesh().gpuReady = true;
    selection.update({0.5, 80.0, 0.5}, 8, activeChunks);
    require(selection.selectedMinimumDistanceAtLevel(0) ==
                6.0f * Config::CHUNK_SIZE_X,
            "level-zero LOD backing remains after the near chunk is renderable");

    selection.update({91.0, 180.0, -10.0}, 12, {});
    require(selection.selectedMinimumDistanceAtLevel(0) == 0.0f,
            "moving to an unallocated near region leaves the LOD inner hole");

    selection.update({0.5, 80.0, 0.5}, 2, {});
    require(selection.selectedMinimumDistanceAtLevel(0) == 0.0f,
            "minimum render distance does not permit a zero-distance LOD overlap");

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
    const auto tileDirectory = root / "lod" / "r4" / "d_0" / "tiles";
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

    const auto heavenRoot = root / "heaven";
    SaveStore heavenStore(heavenRoot);
    {
        ThreadPool pool(1);
        LodTerrainSystem heavenSystem;
        heavenSystem.setThreadPool(&pool);
        heavenSystem.setSaveStore(&heavenStore);
        heavenSystem.reset(&heaven);
        heavenSystem.configure({true, 32, LodAggressiveness::PowerSaver,
                                LodPrecision::Low});
        heavenSystem.update({0.5, 200.0, 0.5}, 8, {});
        for (int batch = 0; batch < 12; ++batch) {
            pool.waitIdle();
            heavenSystem.processCompleted(nullptr);
        }
        require(heavenSystem.residentTileCount() >= 8,
                "completed Heaven LOD tiles did not advance the request queue");
        require(heavenSystem.residentTileCountAtLevel(0) > 0 &&
                    heavenSystem.residentTileCountAtLevel(1) > 0 &&
                    heavenSystem.residentTileCountAtLevel(2) > 0,
                "dense inner Heaven rings starved outer LOD levels");
    }
    {
        SaveStore coverageStore(root / "coverage");
        ThreadPool pool(4);
        EntityAiTestRenderer renderer;
        LodTerrainSystem coverage;
        coverage.setThreadPool(&pool);
        coverage.setSaveStore(&coverageStore);
        coverage.reset(&normal);
        coverage.configure({true, 16, LodAggressiveness::Fast,
                            LodPrecision::Low});
        const glm::dvec3 position{-96.5, 80.0, -32.5};
        coverage.update(position, 8, {});
        require(!coverage.coverageReady(),
                "loading accepts an unpopulated LOD selection");
        for (int batch = 0; batch < 400 && !coverage.coverageReady(); ++batch) {
            pool.waitIdle();
            coverage.processCompleted(&renderer);
            coverage.update(position, 8, {});
        }
        require(coverage.coverageReady() && !coverage.submissions().empty(),
                "LOD loading never reaches GPU-ready coverage");
        for (const LodRenderSubmission& submission : coverage.submissions()) {
            const float grid = std::max(submission.worldOriginAndGrids.y,
                                        submission.worldOriginAndGrids.w);
            require(submission.mesh->gpuReady &&
                    submission.worldOriginAndGrids.x >= 0.0f &&
                    submission.worldOriginAndGrids.z >= 0.0f &&
                    (grid == 0.0f ||
                     (submission.worldOriginAndGrids.x < grid &&
                      submission.worldOriginAndGrids.z < grid)),
                    "negative-coordinate LOD submission lost its exact grid phase");
        }
        const LodRenderSubmission* parent = nullptr;
        for (const LodRenderSubmission& coarse : coverage.submissions()) {
            if (coarse.tileSize != 32) continue;
            const bool hasFineChild = std::any_of(
                coverage.submissions().begin(), coverage.submissions().end(),
                [&coarse](const LodRenderSubmission& fine) {
                    return fine.tileSize == 16 &&
                        fine.model[3].x >= coarse.model[3].x &&
                        fine.model[3].x < coarse.model[3].x + 32.0f &&
                        fine.model[3].z >= coarse.model[3].z &&
                        fine.model[3].z < coarse.model[3].z + 32.0f;
                });
            if (hasFineChild) { parent = &coarse; break; }
        }
        require(parent != nullptr,
                "test scene lacks adjacent fine/coarse LOD tiles");
        const float parentX = parent->model[3].x;
        const float parentZ = parent->model[3].z;
        const float parentMaximum = parent->maximumDistance;
        ChunkMesh* parentMesh = const_cast<ChunkMesh*>(parent->mesh);
        parentMesh->gpuReady = false;
        coverage.processCompleted(&renderer);
        require(std::any_of(coverage.submissions().begin(),
                            coverage.submissions().end(),
                            [parentX, parentZ, parentMaximum](
                                const LodRenderSubmission& fine) {
                                return fine.tileSize == 16 &&
                                    fine.model[3].x >= parentX &&
                                    fine.model[3].x < parentX + 32.0f &&
                                    fine.model[3].z >= parentZ &&
                                    fine.model[3].z < parentZ + 32.0f &&
                                    fine.maximumDistance == parentMaximum;
                            }),
                "fine LOD released a transition before its parent was GPU-ready");
        parentMesh->gpuReady = true;
        const auto fineChild = std::find_if(coverage.submissions().begin(),
            coverage.submissions().end(), [parentX, parentZ](
                const LodRenderSubmission& fine) {
                return fine.tileSize == 16 &&
                    fine.model[3].x >= parentX &&
                    fine.model[3].x < parentX + 32.0f &&
                    fine.model[3].z >= parentZ &&
                    fine.model[3].z < parentZ + 32.0f;
            });
        require(fineChild != coverage.submissions().end(),
                "test scene lost its fine transition child");
        ChunkMesh* fineMesh = const_cast<ChunkMesh*>(fineChild->mesh);
        fineMesh->gpuReady = false;
        coverage.processCompleted(&renderer);
        require(std::any_of(coverage.submissions().begin(),
                            coverage.submissions().end(),
                            [parentX, parentZ](const LodRenderSubmission& coarse) {
                                return coarse.tileSize == 32 &&
                                    coarse.model[3].x == parentX &&
                                    coarse.model[3].z == parentZ &&
                                    coarse.minimumDistance == 0.0f;
                            }),
                "coarse LOD did not cover a missing fine transition child");
        fineMesh->gpuReady = true;
        coverage.configure({true, 16, LodAggressiveness::Fast,
                            LodPrecision::Medium});
        require(!coverage.submissions().empty(),
                "precision edit discarded all GPU-ready LOD meshes");
    }
    {
        SaveStore epochStore(root / "epoch");
        ThreadPool pool(1);
        LodTerrainSystem exactSystem;
        exactSystem.setThreadPool(&pool);
        exactSystem.setSaveStore(&epochStore);
        exactSystem.reset(&normal);
        exactSystem.configure({true, 0, LodAggressiveness::PowerSaver,
                               LodPrecision::Low});
        Chunk live(0, 0);
        live.setBlock(8, 64, 8, BlockId::STONE);
        live.generated = true;
        live.lifecycle = Chunk::LifecycleState::Renderable;
        exactSystem.update({0.5, 80.0, 0.5}, 0, {&live});
        exactSystem.configure({true, 16, LodAggressiveness::PowerSaver,
                               LodPrecision::Low});
        pool.waitIdle();
        exactSystem.processCompleted(nullptr);
        require(exactSystem.hasExactChunk(0, 0),
                "a selection epoch change discarded completed exact terrain");
    }
    std::filesystem::remove_all(root);

    std::cout << "LOD terrain tests passed\n";
    return 0;
}
