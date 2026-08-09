#include "world/HeightPipeline.h"
#include "world/Noise.h"
#include "world/TreeGenerator.h"
#include "world/WorldGenerator.h"
#include "world/RegionGenerator.h"
#include "world/Chunk.h"
#include "world/ChunkMesh.h"
#include "Config.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <set>
#include <tuple>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

using TreeKey = std::tuple<int, int, int, int, int>;
}

int main() {
    require(shouldRenderCubeFace(BlockId::STONE, BlockId::ICE) &&
            shouldRenderCubeFace(BlockId::DIRT, BlockId::LEAVES),
            "opaque terrain faces remain behind translucent solid blocks");
    require(!shouldRenderCubeFace(BlockId::ICE, BlockId::STONE) &&
            !shouldRenderCubeFace(BlockId::LEAVES, BlockId::LEAVES),
            "translucent interfaces avoid duplicate and internal faces");
    constexpr uint64_t seed = 1234567890ULL;
    Noise legacy(seed);
    HeightPipeline terrain(legacy, seed);

    // Point sampling is repeatable, bounded, and seed-sensitive.
    Noise otherLegacy(987654321ULL);
    HeightPipeline other(otherLegacy, 987654321ULL);
    bool seedDiffers = false;
    std::set<Biome> observedBiomes;
    int riverColumns = 0;
    int maxTerrainHeight = Config::WORLD_MIN_Y;
    int overhangColumns = 0;
    for (int z = -2048; z <= 2048; z += 8) {
        for (int x = -2048; x <= 2048; x += 8) {
            SurfaceColumn a = terrain.sampleColumn(x, z);
            SurfaceColumn b = terrain.sampleColumn(x, z);
            require(a.height == b.height && a.biome == b.biome &&
                    a.river == b.river, "same seed point sample changed");
            require(a.height >= Config::TERRAIN_MIN_HEIGHT &&
                    a.height <= Config::TERRAIN_MAX_HEIGHT,
                    "terrain height outside world");
            observedBiomes.insert(a.biome);
            maxTerrainHeight = std::max(maxTerrainHeight, a.height);
            if (a.mountainFactor > 0.55f) {
                for (int y = a.nominalHeight - 20; y <= a.height; ++y) {
                    if (terrain.isTerrainSolid(x, y, z, a) &&
                        !terrain.isTerrainSolid(x, y - 1, z, a)) {
                        ++overhangColumns;
                        break;
                    }
                }
            }
            if (a.river) ++riverColumns;
            SurfaceColumn changed = other.sampleColumn(x, z);
            if (a.height != changed.height || a.biome != changed.biome ||
                a.river != changed.river) seedDiffers = true;
        }
    }
    require(seedDiffers, "different seeds produced identical surface");
    require(observedBiomes.size() >= 6, "surface lacks biome diversity");
    require(riverColumns > 0, "surface router produced no rivers");
    require(maxTerrainHeight >= 160, "terrain router produced no tall mountains");
    require(overhangColumns > 0, "mountain density produced no overhangs");

    // Padded-region output and direct point queries are byte-for-byte equal,
    // including negative world coordinates.
    RegionGenerationData region;
    region.worldOriginX = -48;
    region.worldOriginZ = -32;
    region.regionSizeBlocks = 48;
    region.padding = 6;
    region.paddedWidth = 60;
    region.paddedDepth = 60;
    region.columns.resize(60 * 60);
    terrain.computePaddedRegion(region.worldOriginX, region.worldOriginZ,
                                48, 48, 6, region.columns.data());
    for (int lz = 0; lz < 60; ++lz) {
        for (int lx = 0; lx < 60; ++lx) {
            int wx = region.worldOriginX + lx - 6;
            int wz = region.worldOriginZ + lz - 6;
            SurfaceColumn direct = terrain.sampleColumn(wx, wz);
            const auto& cached = region.col(lx, lz);
            require(cached.height == direct.height &&
                    cached.biome == direct.biome &&
                    cached.isRiver == direct.river &&
                    cached.waterLevel == direct.waterLevel,
                    "region cache disagrees with world-coordinate sample");
        }
    }

    // Tree anchors are independent of whether an area is requested as one
    // region or as four singleton chunks.
    constexpr int originX = -16;
    constexpr int originZ = -16;
    constexpr int area = 32;
    std::vector<int> heights(area * area);
    std::vector<Biome> biomes(area * area);
    std::vector<uint8_t> rivers(area * area);
    for (int z = 0; z < area; ++z) {
        for (int x = 0; x < area; ++x) {
            auto col = terrain.sampleColumn(originX + x, originZ + z);
            size_t i = static_cast<size_t>(z) * area + x;
            heights[i] = col.height;
            biomes[i] = col.biome;
            rivers[i] = col.river ? 1 : 0;
        }
    }

    TreeGenerator trees(seed);
    std::vector<RegionGenerationData::TreePlacement> regionTrees;
    trees.generateTreesRegion(originX, originZ, area, area, heights.data(),
                              biomes.data(), rivers.data(), 0, regionTrees);
    std::set<TreeKey> regionSet;
    for (const auto& tree : regionTrees) {
        regionSet.emplace(originX + tree.localX, originZ + tree.localZ,
                          tree.baseY, tree.trunkHeight,
                          static_cast<int>(tree.type));
    }

    std::set<TreeKey> chunkSet;
    for (int cz = 0; cz < 2; ++cz) {
        for (int cx = 0; cx < 2; ++cx) {
            int h[16][16]{};
            Biome b[16][16]{};
            bool r[16][16]{};
            for (int z = 0; z < 16; ++z) {
                for (int x = 0; x < 16; ++x) {
                    size_t i = static_cast<size_t>(cz * 16 + z) * area +
                               static_cast<size_t>(cx * 16 + x);
                    h[x][z] = heights[i];
                    b[x][z] = biomes[i];
                    r[x][z] = rivers[i] != 0;
                }
            }
            int chunkX = originX + cx * 16;
            int chunkZ = originZ + cz * 16;
            for (const auto& tree : trees.generateTrees(chunkX, chunkZ, h, b, r)) {
                chunkSet.emplace(chunkX + tree.localX, chunkZ + tree.localZ,
                                 tree.baseY, tree.trunkHeight,
                                 static_cast<int>(tree.type));
            }
        }
    }
    require(regionSet == chunkSet, "region and singleton tree anchors differ");

    // Full generation equivalence: terrain, caves, ores, decorations and trees
    // must not depend on region-vs-singleton execution.
    WorldGenerator regionWorld(seed);
    {
        std::vector<CaveColumnInfo> smallColumns(16 * 16);
        std::vector<CaveColumnInfo> largeColumns(48 * 48);
        for (int z = 0; z < 48; ++z) for (int x = 0; x < 48; ++x) {
            const auto column = regionWorld.getHeightPipeline().sampleColumn(x, z);
            const CaveColumnInfo info{column.height, column.waterLevel,
                column.river || column.height < column.waterLevel};
            largeColumns[static_cast<size_t>(z) * 48 + x] = info;
            if (x < 16 && z < 16) smallColumns[static_cast<size_t>(z) * 16 + x] = info;
        }
        const auto smallCaves = regionWorld.getCaveGenerator().generateVolume(0,0,16,16,smallColumns);
        const auto largeCaves = regionWorld.getCaveGenerator().generateVolume(0,0,48,48,largeColumns);
        for (int z=0;z<16;++z) for(int x=0;x<16;++x)
            for(int y=Config::WORLD_MIN_Y;y<Config::WORLD_MAX_Y;++y)
                require(smallCaves.get(x,y,z)==largeCaves.get(x,y,z),
                        "cave request size changes overlapping output");
    }
    RegionGenerator regionGenerator(
        regionWorld.getHeightPipeline(), regionWorld.getCaveGenerator(),
        regionWorld.getTreeGenerator(), regionWorld.getOreGenerator(), seed);
    std::vector<std::unique_ptr<Chunk>> regionOwned;
    std::vector<Chunk*> regionChunks;
    for (int cz = 0; cz < 3; ++cz) {
        for (int cx = 0; cx < 3; ++cx) {
            regionOwned.push_back(std::make_unique<Chunk>(cx, cz));
            regionChunks.push_back(regionOwned.back().get());
        }
    }
    std::vector<RegionGenerationData::PendingBlock> regionPending;
    regionGenerator.generateRegion(0, 0, 3, Config::REGION_PADDING,
                                   regionChunks, regionPending);

    WorldGenerator singletonWorld(seed);
    std::vector<std::unique_ptr<Chunk>> singletonOwned;
    std::vector<RegionGenerationData::PendingBlock> singletonPending;
    for (int cz = 0; cz < 3; ++cz) {
        for (int cx = 0; cx < 3; ++cx) {
            singletonOwned.push_back(std::make_unique<Chunk>(cx, cz));
            Chunk& chunk = *singletonOwned.back();
            singletonWorld.generate(chunk, {}, [&](int wx, int wy, int wz, BlockId id) {
                singletonPending.push_back({wx, wy, wz, id});
            });
        }
    }
    for (const auto& block : singletonPending) {
        int cx = block.worldX >= 0 ? block.worldX / 16 : -1;
        int cz = block.worldZ >= 0 ? block.worldZ / 16 : -1;
        if (cx < 0 || cx >= 3 || cz < 0 || cz >= 3) continue;
        Chunk& chunk = *singletonOwned[static_cast<size_t>(cz) * 3 + cx];
        int lx = block.worldX - chunk.worldX();
        int lz = block.worldZ - chunk.worldZ();
        BlockId current = chunk.getBlock(lx, block.worldY, lz);
        bool leaf = current == BlockId::LEAVES ||
                    current == BlockId::BIRCH_LEAVES ||
                    current == BlockId::SPRUCE_LEAVES ||
                    current == BlockId::JUNGLE_LEAVES ||
                    current == BlockId::ACACIA_LEAVES;
        if (current == BlockId::AIR || current == BlockId::SNOW || leaf)
            chunk.setBlock(lx, block.worldY, lz, block.id);
    }
    for (size_t i = 0; i < regionChunks.size(); ++i) {
        for (int y = Config::WORLD_MIN_Y; y < Config::WORLD_MAX_Y; ++y) {
            for (int z = 0; z < 16; ++z) {
                for (int x = 0; x < 16; ++x) {
                    if (regionChunks[i]->getBlock(x, y, z) !=
                        singletonOwned[i]->getBlock(x, y, z)) {
                        std::cerr << "mismatch chunk=" << i << " x=" << x
                                  << " y=" << y << " z=" << z
                                  << " region=" << static_cast<int>(regionChunks[i]->getBlock(x,y,z))
                                  << " singleton=" << static_cast<int>(singletonOwned[i]->getBlock(x,y,z))
                                  << " regionMax=" << regionChunks[i]->getColumnMaxY(x,z)
                                  << " singletonMax=" << singletonOwned[i]->getColumnMaxY(x,z)
                                  << '\n';
                        require(false, "region and singleton full block output differ");
                    }
                }
            }
        }
    }

    // Ore remains a sparse replacement of the host rock. This guards against
    // threshold regressions that turn most underground stone into ore.
    std::array<size_t, 4> oreCounts{};
    size_t sampledHostBlocks = 0;
    OreGenerator& ores = singletonWorld.getOreGenerator();
    for (int z = -64; z < 64; ++z) {
        for (int x = -64; x < 64; ++x) {
            for (int y = 4; y < 122; ++y) {
                ++sampledHostBlocks;
                const BlockId ore = ores.getOre(
                    static_cast<float>(x) + 0.5f,
                    static_cast<float>(y) + 0.5f,
                    static_cast<float>(z) + 0.5f, BlockId::STONE);
                switch (ore) {
                    case BlockId::COAL_ORE: ++oreCounts[0]; break;
                    case BlockId::IRON_ORE: ++oreCounts[1]; break;
                    case BlockId::GOLD_ORE: ++oreCounts[2]; break;
                    case BlockId::DIAMOND_ORE: ++oreCounts[3]; break;
                    default: break;
                }
            }
        }
    }
    const size_t totalOres =
        oreCounts[0] + oreCounts[1] + oreCounts[2] + oreCounts[3];
    std::cout << "ore sample coal=" << oreCounts[0]
              << " iron=" << oreCounts[1]
              << " gold=" << oreCounts[2]
              << " diamond=" << oreCounts[3]
              << " total=" << totalOres << '/' << sampledHostBlocks << '\n';
    require(totalOres * 100 < sampledHostBlocks,
            "ore replaced at least one percent of sampled host rock");
    require(totalOres > 5781,
            "generation-v4 ore density did not exceed the v3 baseline");
    require(oreCounts[0] > 0 && oreCounts[1] > 0 &&
            oreCounts[2] > 0 && oreCounts[3] > 0,
            "sparse ore tuning removed an ore type from the sample");

    // CPU mesh classification: plants use opaque/cutout cross geometry while
    // water and leaves occupy the translucent draw range.
    std::vector<uint8_t> meshBlocks(Config::CHUNK_VOLUME, 0);
    auto meshIndex = [](int x, int y, int z) {
        return x + z * 16 + Config::worldYToStorageY(y) * 16 * 16;
    };
    meshBlocks[meshIndex(1, 42, 1)] = static_cast<uint8_t>(BlockId::TALL_GRASS);
    meshBlocks[meshIndex(2, 40, 2)] = static_cast<uint8_t>(BlockId::WATER);
    meshBlocks[meshIndex(3, 44, 3)] = static_cast<uint8_t>(BlockId::BIRCH_LEAVES);
    int maxY[16][16]{};
    ChunkMesh mesh;
    mesh.build(0, 0, meshBlocks.data(), maxY,
               [](int, int, int) { return BlockId::AIR; },
               [](int, int, int) -> LightSample { return {}; });
    require(mesh.opaqueIndexCount >= 24,
            "cross-shaped vegetation was not emitted double-sided");
    require(mesh.translucentIndexCount > 0,
            "translucent blocks were not assigned a separate index range");
    require(mesh.translucentIndexOffset == mesh.opaqueIndexCount,
            "mesh layer index ranges overlap");

    std::set<std::pair<int,int>> meshNeighbors;
    for (const auto& offset : ChunkMesh::NEIGHBOR_DEPENDENCY_OFFSETS)
        meshNeighbors.emplace(offset[0], offset[1]);
    require(meshNeighbors.size() == 8 && meshNeighbors.count({-1,-1}) == 1 &&
            meshNeighbors.count({1,1}) == 1 && meshNeighbors.count({0,0}) == 0,
            "late chunk arrival does not cover all mesh dependencies");

    // Isolated surfaces receive full AO. A classic two-side corner around an
    // exposed top face must darken the shared vertex to the minimum level,
    // while a covered column is marked as having no direct sky light.
    std::vector<uint8_t> aoBlocks(Config::CHUNK_VOLUME, 0);
    aoBlocks[meshIndex(5, 40, 5)] = static_cast<uint8_t>(BlockId::STONE);
    aoBlocks[meshIndex(6, 41, 5)] = static_cast<uint8_t>(BlockId::STONE);
    aoBlocks[meshIndex(5, 41, 6)] = static_cast<uint8_t>(BlockId::STONE);
    int aoMaxY[16][16]{};
    aoMaxY[5][5] = 60;
    ChunkMesh aoMesh;
    aoMesh.build(0, 0, aoBlocks.data(), aoMaxY,
        [&](int wx, int wy, int wz) {
            if (wx < 0 || wx >= 16 || !Config::isValidWorldY(wy) ||
                wz < 0 || wz >= 16) return BlockId::AIR;
            return static_cast<BlockId>(aoBlocks[meshIndex(wx, wy, wz)]);
        }, [](int, int, int) -> LightSample { return {}; });
    bool foundDarkCorner = false;
    bool foundCoveredSurface = false;
    for (const auto& vertex : aoMesh.vertices) {
        if (vertex.face == static_cast<float>(FaceDir::TOP) &&
            std::abs(vertex.py - 41.0f) < 0.01f &&
            vertex.px >= 5.0f && vertex.px <= 6.0f &&
            vertex.pz >= 5.0f && vertex.pz <= 6.0f) {
            foundDarkCorner = foundDarkCorner || vertex.ao < 0.01f;
            foundCoveredSurface = foundCoveredSurface || vertex.skyLight < 0.01f;
        }
    }
    require(foundDarkCorner, "voxel corner AO was not generated");
    require(foundCoveredSurface, "covered surface incorrectly received sky light");

    std::vector<uint8_t> litBlocks(Config::CHUNK_VOLUME,0);
    litBlocks[meshIndex(8,40,8)]=static_cast<uint8_t>(BlockId::STONE);
    ChunkMesh litMesh;
    litMesh.build(0,0,litBlocks.data(),maxY,
        [&](int wx,int wy,int wz){
            if(wx<0||wx>=16||wz<0||wz>=16||!Config::isValidWorldY(wy))
                return BlockId::AIR;
            return static_cast<BlockId>(litBlocks[meshIndex(wx,wy,wz)]);
        },[](int wx,int,int)->LightSample{
            return {static_cast<uint8_t>(wx<9?15:7),
                    static_cast<uint8_t>(wx<9?2:14)};
        });
    float minSky=1.0f,maxSky=0.0f,minBlock=1.0f,maxBlock=0.0f;
    for(const auto& vertex:litMesh.vertices){
        if(vertex.face!=static_cast<float>(FaceDir::TOP)||
           std::abs(vertex.py-41.0f)>0.01f)continue;
        minSky=std::min(minSky,vertex.skyLight);maxSky=std::max(maxSky,vertex.skyLight);
        minBlock=std::min(minBlock,vertex.blockLight);maxBlock=std::max(maxBlock,vertex.blockLight);
    }
    require(maxSky-minSky>0.1f&&maxBlock-minBlock>0.1f,
            "smooth mesh vertices did not preserve dual-channel gradients");

    // The async worker-to-render-thread handoff must carry all layer metadata,
    // not just the vertex/index arrays. Missing counts make otherwise valid
    // chunks invisible.
    ChunkMesh activeMesh;
    activeMesh.vertices.push_back({});
    activeMesh.indices.push_back(99);
    activeMesh.indexCount = 1;
    activeMesh.opaqueIndexCount = 1;
    ChunkMesh completedMesh;
    completedMesh.vertices = mesh.vertices;
    completedMesh.indices = mesh.indices;
    completedMesh.indexCount = mesh.indexCount;
    completedMesh.opaqueIndexCount = mesh.opaqueIndexCount;
    completedMesh.translucentIndexOffset = mesh.translucentIndexOffset;
    completedMesh.translucentIndexCount = mesh.translucentIndexCount;
    activeMesh.adoptCpuGeometry(completedMesh);
    require(activeMesh.indexCount == mesh.indexCount &&
            activeMesh.opaqueIndexCount == mesh.opaqueIndexCount &&
            activeMesh.translucentIndexOffset == mesh.translucentIndexOffset &&
            activeMesh.translucentIndexCount == mesh.translucentIndexCount,
            "async mesh handoff dropped render-layer metadata");

    std::cout << "biomes=" << observedBiomes.size()
              << " rivers=" << riverColumns
              << " trees=" << regionSet.size()
              << " ores=" << totalOres << '/' << sampledHostBlocks << '\n';
}
