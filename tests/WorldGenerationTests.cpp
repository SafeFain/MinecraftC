#include "world/HeightPipeline.h"
#include "world/Noise.h"
#include "world/TreeGenerator.h"
#include "world/WorldGenerator.h"
#include "world/RegionGenerator.h"
#include "world/SurfaceRules.h"
#include "world/Chunk.h"
#include "world/ChunkMesh.h"
#include "world/BiomeLocator.h"
#include "Config.h"

#include <cstdlib>
#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
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
    const auto nearest = locateNearestBiome(glm::ivec2(0, 0), Biome::PLAINS,
        [](int x, int z) { return x >= 48 && z >= -16 && z <= 16
            ? Biome::PLAINS : Biome::FOREST; }, 128, 32);
    require(nearest && nearest->x == 48 && nearest->y == 0,
            "biome locator did not refine the nearest sampled biome");

    require(shouldRenderCubeFace(BlockId::STONE, BlockId::ICE) &&
            shouldRenderCubeFace(BlockId::DIRT, BlockId::LEAVES),
            "opaque terrain faces remain behind non-opaque solid blocks");
    require(!shouldRenderCubeFace(BlockId::ICE, BlockId::STONE) &&
            !shouldRenderCubeFace(BlockId::LEAVES, BlockId::LEAVES),
            "non-opaque interfaces avoid duplicate and internal faces");
    constexpr uint64_t seed = 1234567890ULL;
    Noise legacy(seed);
    HeightPipeline terrain(legacy, seed);

    // v7 selected macro-anchor types with query-point climate and produced a
    // 70-block wall between these two adjacent user-reported columns.
    constexpr uint64_t faultSeed = 8804448447376879172ULL;
    Noise faultLegacy(faultSeed);
    HeightPipeline faultTerrain(faultLegacy, faultSeed);
    const SurfaceColumn faultLeft = faultTerrain.sampleColumn(198, 456);
    const SurfaceColumn faultRight = faultTerrain.sampleColumn(199, 456);
    require(std::abs(faultLeft.height - faultRight.height) <= 8,
            "user seed retained the v7 macro-archetype height wall");
    require(faultLeft.localRelief <= 8 && faultRight.localRelief <= 8,
            "user seed reports excessive true local relief after routing");

    // At this v8 Voronoi boundary the primary and secondary archetypes swap
    // order. The blended height must remain continuous and independent of
    // which candidate is evaluated first.
    constexpr uint64_t orderingFaultSeed = 2031523183801237062ULL;
    Noise orderingLegacy(orderingFaultSeed);
    HeightPipeline orderingTerrain(orderingLegacy, orderingFaultSeed);
    const SurfaceColumn orderingBefore = orderingTerrain.sampleColumn(68, -49);
    const SurfaceColumn orderingAfter = orderingTerrain.sampleColumn(68, -48);
    require(orderingBefore.archetype != orderingAfter.archetype,
            "archetype-order regression did not cross the expected boundary");
    require(orderingBefore.secondaryArchetype == orderingAfter.archetype,
            "archetype-order regression lost the swapped candidate");
    require(std::abs(orderingBefore.height - orderingAfter.height) <= 4,
            "archetype-order swap produced a local height wall");
    require(orderingBefore.localRelief <= 4 && orderingAfter.localRelief <= 4,
            "archetype-order swap produced excessive local relief");

    constexpr int orderingWindowSize = 256;
    constexpr int orderingWindowX = -60;
    constexpr int orderingWindowZ = -177;
    std::vector<RegionGenerationData::ColumnInfo> orderingWindow(
        orderingWindowSize * orderingWindowSize);
    orderingTerrain.computePaddedRegion(
        orderingWindowX, orderingWindowZ, orderingWindowSize,
        orderingWindowSize, 0, orderingWindow.data());
    size_t orderingOrdinaryEdges = 0;
    for (int z = 0; z < orderingWindowSize; ++z) {
        for (int x = 0; x < orderingWindowSize; ++x) {
            const auto& center = orderingWindow[
                static_cast<size_t>(z) * orderingWindowSize + x];
            if (x + 1 < orderingWindowSize) {
                const auto& east = orderingWindow[
                    static_cast<size_t>(z) * orderingWindowSize + x + 1];
                if (center.densityWeight < 0.05f &&
                    east.densityWeight < 0.05f) {
                    require(std::abs(center.height - east.height) <= 8,
                            "ordering-fault window retained an ordinary wall");
                    ++orderingOrdinaryEdges;
                }
            }
            if (z + 1 < orderingWindowSize) {
                const auto& south = orderingWindow[
                    static_cast<size_t>(z + 1) * orderingWindowSize + x];
                if (center.densityWeight < 0.05f &&
                    south.densityWeight < 0.05f) {
                    require(std::abs(center.height - south.height) <= 8,
                            "ordering-fault window retained an ordinary wall");
                    ++orderingOrdinaryEdges;
                }
            }
        }
    }
    require(orderingOrdinaryEdges > 0,
            "ordering-fault window contained no ordinary terrain edges");

    // Four macro anchors meet near this user-reported column. Truncating the
    // blend to three candidates discarded a still-significant coastal-cliff
    // contribution when the candidate ranks changed, producing straight
    // radial height and density seams.
    constexpr uint64_t junctionFaultSeed = 7803446839731492329ULL;
    Noise junctionLegacy(junctionFaultSeed);
    HeightPipeline junctionTerrain(junctionLegacy, junctionFaultSeed);
    const SurfaceColumn junctionWest = junctionTerrain.sampleColumn(-121, -541);
    const SurfaceColumn junctionCenter = junctionTerrain.sampleColumn(-120, -541);
    const SurfaceColumn junctionEast = junctionTerrain.sampleColumn(-119, -541);
    const SurfaceColumn junctionNorth = junctionTerrain.sampleColumn(-120, -542);
    const SurfaceColumn junctionSouth = junctionTerrain.sampleColumn(-120, -540);
    require(junctionWest.secondaryArchetype !=
                junctionCenter.secondaryArchetype &&
            junctionCenter.secondaryArchetype !=
                junctionSouth.secondaryArchetype,
            "multi-anchor regression no longer crosses the expected ranks");
    const std::array<SurfaceColumn, 5> junctionColumns{{
        junctionWest, junctionCenter, junctionEast,
        junctionNorth, junctionSouth
    }};
    for (const SurfaceColumn& column : junctionColumns)
        require(column.localRelief <= 8,
                "multi-anchor junction retained excessive local relief");
    auto requireJunctionEdge = [](const SurfaceColumn& first,
                                  const SurfaceColumn& second) {
        require(std::abs(first.height - second.height) <= 8,
                "multi-anchor junction retained a height seam");
        require(std::abs(first.nominalHeight - second.nominalHeight) <= 8,
                "multi-anchor junction retained a nominal-height seam");
        require(std::abs(first.densityWeight - second.densityWeight) <= 0.08f,
                "multi-anchor junction retained a density-weight seam");
    };
    requireJunctionEdge(junctionWest, junctionCenter);
    requireJunctionEdge(junctionCenter, junctionEast);
    requireJunctionEdge(junctionNorth, junctionCenter);
    requireJunctionEdge(junctionCenter, junctionSouth);

    constexpr int junctionWindowSize = 256;
    constexpr int junctionWindowX = -248;
    constexpr int junctionWindowZ = -669;
    std::vector<RegionGenerationData::ColumnInfo> junctionWindow(
        junctionWindowSize * junctionWindowSize);
    junctionTerrain.computePaddedRegion(
        junctionWindowX, junctionWindowZ, junctionWindowSize,
        junctionWindowSize, 0, junctionWindow.data());
    int maxJunctionNominalDelta = 0;
    int maxJunctionNominalX = 0;
    int maxJunctionNominalZ = 0;
    float maxJunctionDensityDelta = 0.0f;
    int maxJunctionDensityX = 0;
    int maxJunctionDensityZ = 0;
    auto recordSmoothMacroEdge = [&](const auto& first, const auto& second,
                                     int worldX, int worldZ) {
        if (first.volcanicWeight >= 0.05f ||
            second.volcanicWeight >= 0.05f ||
            first.riverWeight >= 0.16f || second.riverWeight >= 0.16f)
            return;
        const int nominalDelta = std::abs(
            first.nominalHeight - second.nominalHeight);
        if (nominalDelta > maxJunctionNominalDelta) {
            maxJunctionNominalDelta = nominalDelta;
            maxJunctionNominalX = worldX;
            maxJunctionNominalZ = worldZ;
        }
        const float densityDelta = std::abs(
            first.densityWeight - second.densityWeight);
        if (densityDelta > maxJunctionDensityDelta) {
            maxJunctionDensityDelta = densityDelta;
            maxJunctionDensityX = worldX;
            maxJunctionDensityZ = worldZ;
        }
    };
    for (int z = 0; z < junctionWindowSize; ++z) {
        for (int x = 0; x < junctionWindowSize; ++x) {
            const auto& center = junctionWindow[
                static_cast<size_t>(z) * junctionWindowSize + x];
            if (x + 1 < junctionWindowSize) {
                const auto& east = junctionWindow[
                    static_cast<size_t>(z) * junctionWindowSize + x + 1];
                recordSmoothMacroEdge(
                    center, east, junctionWindowX + x,
                    junctionWindowZ + z);
            }
            if (z + 1 < junctionWindowSize) {
                const auto& south = junctionWindow[
                    static_cast<size_t>(z + 1) * junctionWindowSize + x];
                recordSmoothMacroEdge(
                    center, south, junctionWindowX + x,
                    junctionWindowZ + z);
            }
        }
    }
    if (maxJunctionNominalDelta > 12) {
        std::cerr << "multi-anchor max nominal delta "
                  << maxJunctionNominalDelta << " near ("
                  << maxJunctionNominalX << ',' << maxJunctionNominalZ
                  << ")\n";
    }
    require(maxJunctionNominalDelta <= 12,
            "multi-anchor window retained a nominal-height wall");
    if (maxJunctionDensityDelta > 0.12f) {
        std::cerr << "multi-anchor max density delta "
                  << maxJunctionDensityDelta << " near ("
                  << maxJunctionDensityX << ',' << maxJunctionDensityZ
                  << ")\n";
    }
    require(maxJunctionDensityDelta <= 0.12f,
            "multi-anchor window retained a density-weight wall");

    // The four principal cross-sections through the junction must remain
    // walkable even though the diagnostic archetype labels change along them.
    constexpr int junctionCenterIndex = 128;
    constexpr int junctionSectionRadius = 32;
    auto junctionAt = [&](int x, int z) -> const auto& {
        return junctionWindow[
            static_cast<size_t>(z) * junctionWindowSize + x];
    };
    for (int offset = -junctionSectionRadius;
         offset < junctionSectionRadius; ++offset) {
        const int a = junctionCenterIndex + offset;
        const int b = a + 1;
        require(std::abs(junctionAt(a, junctionCenterIndex).height -
                         junctionAt(b, junctionCenterIndex).height) <= 8,
                "multi-anchor east-west section retained a wall");
        require(std::abs(junctionAt(junctionCenterIndex, a).height -
                         junctionAt(junctionCenterIndex, b).height) <= 8,
                "multi-anchor north-south section retained a wall");
        require(std::abs(junctionAt(a, a).height -
                         junctionAt(b, b).height) <= 8,
                "multi-anchor northwest-southeast section retained a wall");
        require(std::abs(junctionAt(a, 2 * junctionCenterIndex - a).height -
                         junctionAt(b, 2 * junctionCenterIndex - b).height) <= 8,
                "multi-anchor southwest-northeast section retained a wall");
    }

    size_t volcanicTopCount = 0;
    size_t basaltTopCount = 0;
    bool solidBasaltWindow = false;
    for (int windowZ = -256; windowZ < 256; windowZ += 32) {
        for (int windowX = -256; windowX < 256; windowX += 32) {
            bool allBasalt = true;
            for (int z = windowZ; z < windowZ + 32; ++z) {
                for (int x = windowX; x < windowX + 32; ++x) {
                    const float distance = std::sqrt(
                        static_cast<float>(x * x + z * z));
                    const float volcanicWeight = std::max(
                        0.0f, 1.0f - distance / 256.0f);
                    if (volcanicWeight < 0.58f) {
                        allBasalt = false;
                        continue;
                    }
                    const SurfaceRuleContext context{
                        Biome::VOLCANIC_HIGHLANDS,
                        TerrainArchetype::VOLCANIC_CALDERA, 142,
                        Config::SEA_LEVEL,
                        0.35f + 0.40f * std::abs(std::sin(
                            static_cast<float>(x) * 0.031f)),
                        2, 1.0f, volcanicWeight,
                        std::max(0.0f, 1.0f - distance / 72.0f),
                        0.0f, false
                    };
                    const BlockId top = SurfaceRules::profile(
                        faultSeed, x, z, context).top;
                    require(top != BlockId::OBSIDIAN,
                            "natural volcanic surface generated obsidian");
                    ++volcanicTopCount;
                    if (top == BlockId::BASALT) ++basaltTopCount;
                    else allBasalt = false;
                }
            }
            solidBasaltWindow = solidBasaltWindow || allBasalt;
        }
    }
    require(basaltTopCount * 100 >= volcanicTopCount * 15 &&
            basaltTopCount * 100 <= volcanicTopCount * 40,
            "volcanic highland basalt top ratio left the 15-40 percent band");
    require(!solidBasaltWindow,
            "volcanic surface produced a solid 32x32 basalt window");

    constexpr int faultWindowSize = 512;
    constexpr int faultWindowX = -57;
    constexpr int faultWindowZ = 200;
    std::vector<RegionGenerationData::ColumnInfo> faultWindow(
        faultWindowSize * faultWindowSize);
    faultTerrain.computePaddedRegion(
        faultWindowX, faultWindowZ, faultWindowSize, faultWindowSize, 0,
        faultWindow.data());
    size_t ordinaryEdges = 0;
    size_t gentleEdges = 0;
    for (int z = 0; z < faultWindowSize; ++z) {
        for (int x = 0; x < faultWindowSize; ++x) {
            const auto& center = faultWindow[
                static_cast<size_t>(z) * faultWindowSize + x];
            if (x + 1 < faultWindowSize) {
                const auto& east = faultWindow[
                    static_cast<size_t>(z) * faultWindowSize + x + 1];
                if (center.densityWeight < 0.05f &&
                    east.densityWeight < 0.05f) {
                    const int difference = std::abs(center.height - east.height);
                    require(difference <= 12,
                            "ordinary terrain retained an adjacent height wall");
                    ++ordinaryEdges;
                    if (difference <= 4) ++gentleEdges;
                }
            }
            if (z + 1 < faultWindowSize) {
                const auto& south = faultWindow[
                    static_cast<size_t>(z + 1) * faultWindowSize + x];
                if (center.densityWeight < 0.05f &&
                    south.densityWeight < 0.05f) {
                    const int difference = std::abs(center.height - south.height);
                    require(difference <= 12,
                            "ordinary terrain retained an adjacent height wall");
                    ++ordinaryEdges;
                    if (difference <= 4) ++gentleEdges;
                }
            }
            if (x > 0 && x + 1 < faultWindowSize &&
                z > 0 && z + 1 < faultWindowSize) {
                bool isolated = true;
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dz == 0) continue;
                        const auto& neighbor = faultWindow[
                            static_cast<size_t>(z + dz) * faultWindowSize +
                            x + dx];
                        isolated = isolated &&
                            center.height - neighbor.height > 24;
                    }
                }
                require(!isolated,
                        "terrain produced an isolated 24-block column");
            }
        }
    }
    require(gentleEdges * 1000 >= ordinaryEdges * 999,
            "fewer than 99.9 percent of ordinary adjacent edges are gentle");

    // Point sampling is repeatable, bounded, and seed-sensitive.
    Noise otherLegacy(987654321ULL);
    HeightPipeline other(otherLegacy, 987654321ULL);
    bool seedDiffers = false;
    std::set<Biome> observedBiomes;
    std::set<TerrainArchetype> observedArchetypes;
    std::array<int, BIOME_COUNT> biomeCounts{};
    int riverColumns = 0;
    int maxTerrainHeight = Config::WORLD_MIN_Y;
    int overhangColumns = 0;
    int cappedColumns = 0;
    int sampledColumns = 0;
    int maxBasinUpstream = 0;
    bool foundInlandLake = false;
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
            observedArchetypes.insert(a.archetype);
            ++sampledColumns;
            if (a.height == Config::TERRAIN_MAX_HEIGHT) ++cappedColumns;
            ++biomeCounts[static_cast<size_t>(a.biome)];
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
            maxBasinUpstream = std::max(
                maxBasinUpstream, static_cast<int>(a.basin.upstreamSize));
            foundInlandLake = foundInlandLake ||
                (a.basin.inlandLake && a.climate.continentalness > -0.13f);
            SurfaceColumn changed = other.sampleColumn(x, z);
            if (a.height != changed.height || a.biome != changed.biome ||
                a.river != changed.river) seedDiffers = true;
        }
    }
    require(seedDiffers, "different seeds produced identical surface");
    require(observedBiomes.size() >= 6, "surface lacks biome diversity");
    require(observedArchetypes.size() >= 12,
            "surface lacks macro terrain archetype diversity");
    require(cappedColumns * 1000 < sampledColumns,
            "too many terrain columns were clipped at the build ceiling");
    require(biomeCounts[static_cast<size_t>(Biome::SWAMP)] >= 100 &&
            biomeCounts[static_cast<size_t>(Biome::JUNGLE)] >= 100 &&
            biomeCounts[static_cast<size_t>(Biome::BADLANDS)] >= 100,
            "uncommon inland biomes are too sparse across the exploration sample");
    require(riverColumns > 0, "surface router produced no rivers");
    require(maxBasinUpstream >= 4,
            "basin graph produced no multi-tributary confluence");
    require(foundInlandLake,
            "basin graph produced no deterministic inland lake");
    require(maxTerrainHeight >= 160, "terrain router produced no tall mountains");
    require(overhangColumns > 0, "mountain density produced no overhangs");

    // The complete v8 routing matrix is reachable across several seeds without
    // requiring request-order randomness or enormous contiguous biome scales.
    std::set<Biome> allBiomes = observedBiomes;
    std::set<TerrainArchetype> allArchetypes = observedArchetypes;
    constexpr uint64_t diversitySeeds[] = {
        1, 42, 8675309, 0xDEADBEEFULL, 0x123456789ABCDEF0ULL,
        999999937, 3141592653ULL
    };
    for (const uint64_t diversitySeed : diversitySeeds) {
        Noise diversityLegacy(diversitySeed);
        HeightPipeline diversity(diversityLegacy, diversitySeed);
        std::set<Biome> localBiomes;
        std::set<TerrainArchetype> localArchetypes;
        std::set<TerrainArchetype> localLandArchetypes;
        for (int z = -4096; z <= 4096; z += 64) {
            for (int x = -4096; x <= 4096; x += 64) {
                const SurfaceColumn column = diversity.sampleColumn(x, z);
                localBiomes.insert(column.biome);
                localArchetypes.insert(column.archetype);
                if (column.archetype != TerrainArchetype::DEEP_OCEAN_TRENCH &&
                    column.archetype != TerrainArchetype::ISLAND_ARC &&
                    column.archetype != TerrainArchetype::COASTAL_CLIFFS)
                    localLandArchetypes.insert(column.archetype);
            }
        }
        require(localBiomes.size() >= 12,
                "a fixed seed exposes too few nearby biomes");
        require(localLandArchetypes.size() >= 8,
                "a fixed seed exposes too few nearby land archetypes");
        allBiomes.insert(localBiomes.begin(), localBiomes.end());
        allArchetypes.insert(localArchetypes.begin(), localArchetypes.end());
    }
    if (allBiomes.size() != BIOME_COUNT) {
        std::cerr << "missing biomes:";
        for (int raw = 0; raw < BIOME_COUNT; ++raw)
            if (allBiomes.count(static_cast<Biome>(raw)) == 0)
                std::cerr << ' ' << biomeCommandName(static_cast<Biome>(raw));
        std::cerr << '\n';
    }
    require(allBiomes.size() == BIOME_COUNT,
            "not every v8 biome is reachable across the distribution sample");
    require(allArchetypes.size() == TERRAIN_ARCHETYPE_COUNT,
            "not every v8 terrain archetype is reachable across the distribution sample");

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
                    cached.waterLevel == direct.waterLevel &&
                    cached.localRelief == direct.localRelief &&
                    cached.archetype == direct.archetype &&
                    cached.secondaryArchetype == direct.secondaryArchetype &&
                    std::abs(cached.archetypeBlend - direct.archetypeBlend) < 0.00001f &&
                    std::abs(cached.densityWeight - direct.densityWeight) < 0.00001f &&
                    std::abs(cached.primaryArchetypeWeight -
                             direct.primaryArchetypeWeight) < 0.00001f &&
                    std::abs(cached.volcanicWeight - direct.volcanicWeight) < 0.00001f &&
                    std::abs(cached.craterWeight - direct.craterWeight) < 0.00001f &&
                    cached.basin.upstreamSize == direct.basin.upstreamSize &&
                    std::abs(cached.basin.channelWidth -
                             direct.basin.channelWidth) < 0.00001f &&
                    cached.basin.channelWaterY == direct.basin.channelWaterY &&
                    cached.basin.channelBedY == direct.basin.channelBedY &&
                    std::abs(cached.basin.valleyWidth -
                             direct.basin.valleyWidth) < 0.00001f &&
                    cached.densityMinY == direct.densityMinY &&
                    cached.densityMaxY == direct.densityMaxY,
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
    require(mesh.shadowCasterIndexCount > 0,
            "terrain and foliage did not produce shadow-caster indices");
    require(mesh.shadowCasterIndexOffset ==
                mesh.translucentIndexOffset + mesh.translucentIndexCount,
            "shadow-caster range does not follow visible draw ranges");

    std::vector<uint8_t> bedBlocks(Config::CHUNK_VOLUME, 0);
    bedBlocks[meshIndex(8, 40, 8)] = static_cast<uint8_t>(BlockId::WHITE_BED);
    bedBlocks[meshIndex(8, 40, 7)] =
        static_cast<uint8_t>(BlockId::WHITE_BED_HEAD_NORTH);
    ChunkMesh bedMesh;
    bedMesh.build(0, 0, bedBlocks.data(), maxY,
        [&](int wx, int wy, int wz) {
            if (wx < 0 || wx >= 16 || wz < 0 || wz >= 16 ||
                !Config::isValidWorldY(wy)) return BlockId::AIR;
            return static_cast<BlockId>(bedBlocks[meshIndex(wx, wy, wz)]);
        }, [](int, int, int) -> LightSample { return {15, 0}; });
    float bedMaximumY = -1000.0f;
    bool hasFrame = false, hasMattress = false, hasPillow = false;
    for (const MeshVertex& vertex : bedMesh.vertices) {
        bedMaximumY = std::max(bedMaximumY, vertex.py);
        const uint8_t tile = static_cast<uint8_t>(std::floor(vertex.tile));
        hasFrame = hasFrame || tile == getAtlasTextureIndex(BlockTexture::Planks);
        hasMattress = hasMattress || tile == getAtlasTextureIndex(BlockTexture::WhiteBed);
        hasPillow = hasPillow || tile == getAtlasTextureIndex(BlockTexture::WhiteWool);
    }
    require(bedMesh.vertices.size() > 200 && hasFrame && hasMattress && hasPillow,
            "bed mesh is not a composite frame, mattress, and pillow model");
    require(std::abs(bedMaximumY - (40.0f + 9.0f / 16.0f)) < 0.0001f,
            "bed mesh exceeds its nine-sixteenths maximum height");
    for (size_t index = 0; index + 2 < bedMesh.indices.size(); index += 3) {
        const MeshVertex& a = bedMesh.vertices[bedMesh.indices[index]];
        const MeshVertex& b = bedMesh.vertices[bedMesh.indices[index + 1]];
        const MeshVertex& c = bedMesh.vertices[bedMesh.indices[index + 2]];
        const glm::vec3 normal = glm::cross(
            glm::vec3(b.px-a.px,b.py-a.py,b.pz-a.pz),
            glm::vec3(c.px-a.px,c.py-a.py,c.pz-a.pz));
        require(glm::dot(normal, glm::vec3(
                    FACE_OFFSETS[static_cast<size_t>(a.face)])) < 0.0f,
                "bed cuboid face winding does not match the clockwise renderer");
    }

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

    // Superflat generation is a fixed world-coordinate preset and must use
    // the same four layers through both the region and singleton paths.
    WorldGenerator flatRegionWorld(1234, WorldType::Superflat);
    WorldGenerator flatOtherSeed(9876, WorldType::Superflat);
    const int flatSurface = Config::WORLD_MIN_Y + 3;
    require(flatRegionWorld.getTerrainHeight(-37, 91) == flatSurface &&
            flatRegionWorld.queryHeightBiome(-37, 91).height == flatSurface &&
            flatRegionWorld.queryHeightBiome(-37, 91).biome == Biome::PLAINS,
            "superflat height and biome queries are fixed");
    require(flatRegionWorld.sampleTerrainColumn(-37, 91).waterLevel ==
                Config::WORLD_MIN_Y - 1 &&
            !flatRegionWorld.sampleTerrainColumn(-37, 91).river,
            "superflat columns have no water or river");

    std::vector<std::unique_ptr<Chunk>> flatRegionOwned;
    std::vector<Chunk*> flatRegionChunks;
    for (int cz = 0; cz < 3; ++cz) {
        for (int cx = 0; cx < 3; ++cx) {
            flatRegionOwned.push_back(std::make_unique<Chunk>(cx - 1, cz - 1));
            flatRegionChunks.push_back(flatRegionOwned.back().get());
        }
    }
    std::vector<RegionGenerationData::PendingBlock> flatPending;
    flatRegionWorld.generateRegion(-1, -1, 3, Config::REGION_PADDING,
                                   flatRegionChunks, flatPending);
    require(flatPending.empty(), "superflat region has no cross-chunk placements");
    for (const Chunk* chunk : flatRegionChunks) {
        require(chunk->generated.load(), "superflat region marks chunks generated");
        for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
            for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
                require(chunk->getColumnMaxY(x, z) == flatSurface,
                        "superflat column max is the grass layer");
                for (int y = Config::WORLD_MIN_Y; y < Config::WORLD_MAX_Y; ++y) {
                    const BlockId expected = y == Config::WORLD_MIN_Y
                        ? BlockId::BEDROCK
                        : y <= Config::WORLD_MIN_Y + 2 ? BlockId::DIRT
                        : y == flatSurface ? BlockId::GRASS : BlockId::AIR;
                    require(chunk->getBlock(x, y, z) == expected,
                            "superflat region contains an unexpected block");
                }
            }
        }
    }

    Chunk flatSingleton(-1, -1);
    flatOtherSeed.generate(flatSingleton);
    for (int y = Config::WORLD_MIN_Y; y < Config::WORLD_MAX_Y; ++y)
        require(flatSingleton.getBlock(3, y, 11) ==
                    flatRegionChunks.front()->getBlock(3, y, 11),
                "superflat singleton differs from region output");

    // Heaven uses a separate deterministic island field.  Its void remains
    // air at the world bottom, columns are bounded floating islands rather
    // than a continuous plane, and region/singleton generation agree.
    WorldGenerator heaven(0x123456789ULL, WorldType::Normal,
                          DimensionId::Heaven);
    WorldGenerator heavenRepeat(0x123456789ULL, WorldType::Normal,
                                DimensionId::Heaven);
    WorldGenerator heavenDifferent(0x12345678AULL, WorldType::Normal,
                                   DimensionId::Heaven);
    constexpr std::array<glm::ivec2, 8> heavenProbes = {{
        {-768, -768}, {-257, 113}, {-1, -1}, {0, 0},
        {127, -389}, {384, 256}, {719, -64}, {768, 768}}};
    bool differentSeedChanged = false;
    for (const glm::ivec2& probe : heavenProbes) {
        const SurfaceColumn first = heaven.sampleTerrainColumn(probe.x, probe.y);
        const SurfaceColumn repeat = heavenRepeat.sampleTerrainColumn(
            probe.x, probe.y);
        require(first.height == repeat.height &&
                    first.densityMinY == repeat.densityMinY &&
                    first.densityMaxY == repeat.densityMaxY &&
                    first.biome == repeat.biome,
                "heaven generation is not deterministic for a fixed seed");
        const SurfaceColumn other = heavenDifferent.sampleTerrainColumn(
            probe.x, probe.y);
        differentSeedChanged = differentSeedChanged ||
            first.height != other.height || first.biome != other.biome ||
            first.densityMinY != other.densityMinY;
    }
    require(differentSeedChanged,
            "different Heaven seeds produced identical island samples");
    std::set<Biome> heavenBiomes;
    size_t heavenColumns = 0;
    size_t heavenAdjacentColumns = 0;
    constexpr int heavenSampleMin = -768;
    constexpr int heavenSampleMax = 768;
    constexpr int heavenSampleStep = 8;
    constexpr int heavenSampleSide =
        (heavenSampleMax - heavenSampleMin) / heavenSampleStep + 1;
    std::vector<uint8_t> heavenMask(
        static_cast<size_t>(heavenSampleSide * heavenSampleSide));
    int minimumHeavenDepth = std::numeric_limits<int>::max();
    int maximumHeavenDepth = 0;
    for (int z = -768; z <= 768; z += 8) {
        for (int x = -768; x <= 768; x += 8) {
            const auto column = heaven.sampleTerrainColumn(x, z);
            if (column.height <= Config::WORLD_MIN_Y - 1) continue;
            ++heavenColumns;
            const int gridX = (x - heavenSampleMin) / heavenSampleStep;
            const int gridZ = (z - heavenSampleMin) / heavenSampleStep;
            heavenMask[static_cast<size_t>(gridZ * heavenSampleSide + gridX)] = 1;
            heavenBiomes.insert(column.biome);
            const int depth = column.height - column.densityMinY + 1;
            minimumHeavenDepth = std::min(minimumHeavenDepth, depth);
            maximumHeavenDepth = std::max(maximumHeavenDepth, depth);
            require(column.height >= 96 && column.height <= 236 &&
                        depth >= 6 && depth <= 52,
                    "heaven island column exceeded its bounded vertical profile");
            const auto east = heaven.sampleTerrainColumn(x + 1, z);
            const auto south = heaven.sampleTerrainColumn(x, z + 1);
            for (const auto& neighbor : {east, south}) {
                if (neighbor.height <= Config::WORLD_MIN_Y - 1) continue;
                ++heavenAdjacentColumns;
                require(std::abs(neighbor.height - column.height) <= 1,
                        "heaven island summit contains a one-block spike or pit");
            }
        }
    }
    require(heavenColumns > 1000 && heavenBiomes.size() >= 12,
            "heaven island field lacks density or biome diversity");
    require(heavenAdjacentColumns > 1000,
            "heaven summit smoothness test sampled too few adjacent columns");
    std::vector<int> heavenComponentAreas;
    for (int gridZ = 0; gridZ < heavenSampleSide; ++gridZ) {
        for (int gridX = 0; gridX < heavenSampleSide; ++gridX) {
            const size_t start = static_cast<size_t>(
                gridZ * heavenSampleSide + gridX);
            if (heavenMask[start] == 0) continue;
            heavenMask[start] = 0;
            std::vector<glm::ivec2> pending{{gridX, gridZ}};
            int area = 0;
            while (!pending.empty()) {
                const glm::ivec2 cell = pending.back();
                pending.pop_back();
                ++area;
                for (const glm::ivec2 offset : {
                         glm::ivec2{1, 0}, glm::ivec2{-1, 0},
                         glm::ivec2{0, 1}, glm::ivec2{0, -1}}) {
                    const int nextX = cell.x + offset.x;
                    const int nextZ = cell.y + offset.y;
                    if (nextX < 0 || nextX >= heavenSampleSide ||
                        nextZ < 0 || nextZ >= heavenSampleSide)
                        continue;
                    const size_t index = static_cast<size_t>(
                        nextZ * heavenSampleSide + nextX);
                    if (heavenMask[index] == 0) continue;
                    heavenMask[index] = 0;
                    pending.push_back({nextX, nextZ});
                }
            }
            heavenComponentAreas.push_back(area);
        }
    }
    std::sort(heavenComponentAreas.begin(), heavenComponentAreas.end());
    require(heavenComponentAreas.size() >= 3 &&
                heavenComponentAreas.back() >=
                    heavenComponentAreas.front() * 3,
            "heaven islands lack irregular group sizes or spacing");
    require(minimumHeavenDepth <= 8 && maximumHeavenDepth >= 20 &&
                maximumHeavenDepth - minimumHeavenDepth >= 10,
            "heaven underside does not vary independently of the island mask");
    require(heaven.getTerrainHeight(0, 0) ==
                heaven.queryHeightBiome(0, 0).height,
            "heaven point height and biome queries disagree");
    require(heaven.chunkCacheVersion() != WorldGenContext::CHUNK_CACHE_VERSION &&
                heaven.generationVersion() != WorldGenContext::GENERATION_VERSION &&
                heaven.generationVersion() == WorldGenerator::HEAVEN_GENERATION_VERSION,
            "Heaven v4 cache and generation versions share the overworld key");

    std::vector<std::unique_ptr<Chunk>> heavenRegionOwned;
    std::vector<Chunk*> heavenRegionChunks;
    for (int cz = 0; cz < 3; ++cz) {
        for (int cx = 0; cx < 3; ++cx) {
            heavenRegionOwned.push_back(std::make_unique<Chunk>(cx - 1, cz - 1));
            heavenRegionChunks.push_back(heavenRegionOwned.back().get());
        }
    }
    std::vector<RegionGenerationData::PendingBlock> heavenPending;
    heaven.generateRegion(-1, -1, 3, Config::REGION_PADDING,
                          heavenRegionChunks, heavenPending);
    require(heavenPending.empty(), "heaven generation has no boundary work");
    Chunk heavenSingleton(-1, -1);
    heaven.generate(heavenSingleton);
    for (int y = Config::WORLD_MIN_Y; y < Config::WORLD_MAX_Y; ++y)
        for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z)
            for (int x = 0; x < Config::CHUNK_SIZE_X; ++x)
                require(heavenSingleton.getBlock(x, y, z) ==
                            heavenRegionChunks.front()->getBlock(x, y, z),
                        "heaven singleton differs from region output");
    for (const Chunk* chunk : heavenRegionChunks) {
        require(chunk->getBlock(0, Config::WORLD_MIN_Y, 0) == BlockId::AIR,
                "heaven void incorrectly contains a bottom block");
        for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z)
            for (int x = 0; x < Config::CHUNK_SIZE_X; ++x)
                require(chunk->getBlock(x, Config::WORLD_MIN_Y, z) !=
                            BlockId::BEDROCK,
                        "heaven generated bedrock under an island");
    }

    // v4's four internal ecologies and the new material/landmark layer are
    // world-coordinate driven, so a fixed exploration window should expose
    // every visual language without depending on chunk request order.
    std::array<size_t, 4> heavenEcologyCounts{};
    for (int z = -1024; z <= 1024; z += 16) {
        for (int x = -1024; x <= 1024; x += 16) {
            const SurfaceColumn column = heaven.sampleTerrainColumn(x, z);
            if (column.height <= Config::WORLD_MIN_Y - 1) continue;
            ++heavenEcologyCounts[static_cast<size_t>(
                heaven.heavenEcologyAt(x, z))];
        }
    }
    for (const size_t count : heavenEcologyCounts)
        require(count > 0, "Heaven v4 exploration window missed an ecology");

    std::array<bool, 8> heavenMaterials{};
    bool foundSatellite = false;
    bool foundLandmark = false;
    bool foundForbiddenBlock = false;
    for (int cz = -12; cz <= 12; ++cz) {
        for (int cx = -12; cx <= 12; ++cx) {
            Chunk chunk(cx, cz);
            heaven.generate(chunk);
            for (int y = Config::WORLD_MIN_Y; y < Config::WORLD_MAX_Y; ++y) {
                for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
                    for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
                        const BlockId block = chunk.getBlock(x, y, z);
                        if (block == BlockId::BEDROCK || block == BlockId::WATER)
                            foundForbiddenBlock = true;
                        const auto rawBlock = static_cast<uint8_t>(block);
                        if (rawBlock >= static_cast<uint8_t>(BlockId::AETHER_GRASS) &&
                            rawBlock <= static_cast<uint8_t>(BlockId::STARFLOWER)) {
                            heavenMaterials[static_cast<size_t>(block) -
                                             static_cast<size_t>(BlockId::AETHER_GRASS)] = true;
                        }
                        if (y >= 240 && block == BlockId::CLOUDSTONE)
                            foundSatellite = true;
                        if (block == BlockId::STAR_CRYSTAL && y > Config::WORLD_MIN_Y &&
                            chunk.getBlock(x, y - 1, z) == BlockId::SUNSTONE)
                            foundLandmark = true;
                    }
                }
            }
        }
    }
    for (const bool found : heavenMaterials)
        require(found, "Heaven v4 window missed a dedicated material");
    require(foundSatellite, "Heaven v4 window missed a high satellite island");
    require(foundLandmark, "Heaven v4 window missed an Xiguang ruin landmark");
    require(!foundForbiddenBlock,
            "Heaven v4 generated bedrock or an infinite water body");

    std::cout << "biomes=" << observedBiomes.size()
              << "/" << allBiomes.size()
              << " archetypes=" << observedArchetypes.size()
              << "/" << allArchetypes.size()
              << " rivers=" << riverColumns
              << " swamp=" << biomeCounts[static_cast<size_t>(Biome::SWAMP)]
              << " jungle=" << biomeCounts[static_cast<size_t>(Biome::JUNGLE)]
              << " badlands=" << biomeCounts[static_cast<size_t>(Biome::BADLANDS)]
              << " trees=" << regionSet.size()
              << " ores=" << totalOres << '/' << sampledHostBlocks << '\n';
}
