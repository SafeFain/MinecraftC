#include "world/LodTerrainSystem.h"

#include "Config.h"
#include "core/Platform.h"
#include "debug/Log.h"
#include "game/SaveStore.h"
#include "renderer/GameRenderer.h"
#include "threading/ThreadPool.h"
#include "world/BiomeMap.h"
#include "world/Chunk.h"
#include "world/SurfaceRules.h"
#include "world/WorldGenerator.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <system_error>
#include <type_traits>

namespace {
constexpr uint32_t CACHE_REVISION = 4;
constexpr char MAGIC[] = {'M', 'C', 'L', 'D'};

using Bytes = std::vector<uint8_t>;

int floorDiv(int value, int divisor) {
    int quotient = value / divisor;
    if (value % divisor < 0) --quotient;
    return quotient;
}

uint64_t checksum(const Bytes& bytes) {
    uint64_t value = 1469598103934665603ULL;
    for (uint8_t byte : bytes) {
        value ^= byte;
        value *= 1099511628211ULL;
    }
    return value;
}

template<typename T>
void append(Bytes& bytes, T value) {
    using U = std::make_unsigned_t<T>;
    U encoded = static_cast<U>(value);
    for (size_t i = 0; i < sizeof(T); ++i)
        bytes.push_back(static_cast<uint8_t>((encoded >> (i * 8)) & 0xffu));
}

template<typename T>
bool read(const Bytes& bytes, size_t& cursor, T& value) {
    if (cursor + sizeof(T) > bytes.size()) return false;
    using U = std::make_unsigned_t<T>;
    U encoded = 0;
    for (size_t i = 0; i < sizeof(T); ++i)
        encoded |= static_cast<U>(bytes[cursor++]) << (i * 8);
    value = static_cast<T>(encoded);
    return true;
}

Bytes encodeTile(const LodTileData& tile) {
    Bytes payload;
    payload.reserve(4096);
    for (const LodColumn& column : tile.columns) {
        append<uint8_t>(payload, static_cast<uint8_t>(column.exact));
        append<uint16_t>(payload, static_cast<uint16_t>(column.spans.size()));
        for (size_t i = 0; i < column.spans.size(); ++i) {
            append<int16_t>(payload, column.spans[i].bottom);
            append<int16_t>(payload, column.spans[i].top);
            append<uint8_t>(payload, static_cast<uint8_t>(column.spans[i].block));
        }
    }
    return payload;
}

bool decodeTile(const Bytes& payload, LodTileData& tile) {
    size_t cursor = 0;
    for (LodColumn& column : tile.columns) {
        uint8_t exact = 0;
        uint16_t count = 0;
        if (!read(payload, cursor, exact) || !read(payload, cursor, count) ||
            count > Config::WORLD_HEIGHT)
            return false;
        column.exact = exact != 0;
        column.spans.clear();
        column.spans.reserve(count);
        for (uint16_t i = 0; i < count; ++i) {
            LodSpan span;
            uint8_t block = 0;
            if (!read(payload, cursor, span.bottom) ||
                !read(payload, cursor, span.top) ||
                !read(payload, cursor, block) ||
                span.bottom > span.top ||
                block >= static_cast<uint8_t>(BlockId::COUNT))
                return false;
            span.block = static_cast<BlockId>(block);
            column.spans.push_back(span);
        }
    }
    return cursor == payload.size();
}

bool writeTileFile(const std::filesystem::path& path, const LodTileKey& key,
                   const WorldGenerator& generator, const LodTileData& tile) {
    const Bytes payload = encodeTile(tile);
    Bytes file;
    file.insert(file.end(), std::begin(MAGIC), std::end(MAGIC));
    append<uint32_t>(file, CACHE_REVISION);
    append<uint64_t>(file, generator.getSeed());
    append<uint32_t>(file, generator.generationVersion());
    append<uint8_t>(file, static_cast<uint8_t>(generator.worldType()));
    append<uint8_t>(file, static_cast<uint8_t>(generator.dimension()));
    append<int32_t>(file, key.x);
    append<int32_t>(file, key.z);
    append<uint8_t>(file, key.level);
    append<uint32_t>(file, static_cast<uint32_t>(payload.size()));
    append<uint64_t>(file, checksum(payload));
    file.insert(file.end(), payload.begin(), payload.end());

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(reinterpret_cast<const char*>(file.data()),
                 static_cast<std::streamsize>(file.size()));
    output.close();
    if (!output) return false;
    if (Platform::replaceFileAtomically(temporary, path, error)) return true;
    std::filesystem::remove(temporary, error);
    return false;
}

bool readTileFile(const std::filesystem::path& path, const LodTileKey& expected,
                  const WorldGenerator& generator, LodTileData& tile) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    Bytes file((std::istreambuf_iterator<char>(input)), {});
    if (file.size() < 4 || !std::equal(std::begin(MAGIC), std::end(MAGIC), file.begin()))
        return false;
    size_t cursor = 4;
    uint32_t revision = 0, generation = 0, size = 0;
    uint64_t seed = 0, expectedHash = 0;
    uint8_t worldType = 0, dimension = 0, level = 0;
    int32_t x = 0, z = 0;
    if (!read(file, cursor, revision) || !read(file, cursor, seed) ||
        !read(file, cursor, generation) || !read(file, cursor, worldType) ||
        !read(file, cursor, dimension) || !read(file, cursor, x) ||
        !read(file, cursor, z) || !read(file, cursor, level) ||
        !read(file, cursor, size) || !read(file, cursor, expectedHash))
        return false;
    if (revision != CACHE_REVISION || seed != generator.getSeed() ||
        generation != generator.generationVersion() ||
        worldType != static_cast<uint8_t>(generator.worldType()) ||
        dimension != static_cast<uint8_t>(generator.dimension()) ||
        x != expected.x || z != expected.z || level != expected.level ||
        cursor + size != file.size())
        return false;
    Bytes payload(file.begin() + static_cast<std::ptrdiff_t>(cursor), file.end());
    return checksum(payload) == expectedHash && decodeTile(payload, tile);
}

std::filesystem::path tilePath(const std::filesystem::path& root,
                               const LodTileKey& key) {
    return root / "tiles" / ("t_" + std::to_string(key.level) + "_" +
        std::to_string(key.x) + "_" + std::to_string(key.z) + ".lod");
}

std::filesystem::path exactPath(const std::filesystem::path& root, int cx, int cz) {
    return root / "exact" /
        ("c_" + std::to_string(cx) + "_" + std::to_string(cz) + ".lod");
}

LodExactNeighborTiles readExactNeighbors(const std::filesystem::path& root,
                                         const WorldGenerator& generator,
                                         int cx, int cz) {
    LodExactNeighborTiles result;
    for (size_t i = 0; i < ChunkMesh::NEIGHBOR_DEPENDENCY_OFFSETS.size(); ++i) {
        const auto& offset = ChunkMesh::NEIGHBOR_DEPENDENCY_OFFSETS[i];
        LodTileData tile;
        const int nx = cx + offset[0];
        const int nz = cz + offset[1];
        if (readTileFile(exactPath(root, nx, nz), {nx, nz, 0}, generator, tile))
            result[i].emplace(std::move(tile));
    }
    return result;
}

BlockId heavenSurface(WorldGenerator::HeavenBiome biome) {
    switch (biome) {
        case WorldGenerator::HeavenBiome::SunstoneHeights:
        case WorldGenerator::HeavenBiome::MoonpearlTerrace:
            return BlockId::SUNSTONE;
        case WorldGenerator::HeavenBiome::StarCrystalGarden:
        case WorldGenerator::HeavenBiome::GlimmerFen:
            return BlockId::MOSS;
        case WorldGenerator::HeavenBiome::SkystoneBarrens:
            return BlockId::CLOUDSTONE;
        default:
            return BlockId::AETHER_GRASS;
    }
}

BlockId lodTreeFoliage(TreeType type) {
    switch (type) {
        case TreeType::BIRCH: return BlockId::BIRCH_LEAVES;
        case TreeType::SPRUCE: return BlockId::SPRUCE_LEAVES;
        case TreeType::JUNGLE: return BlockId::JUNGLE_LEAVES;
        case TreeType::ACACIA: return BlockId::ACACIA_LEAVES;
        case TreeType::CACTUS: return BlockId::CACTUS_BLOCK;
        default: return BlockId::LEAVES;
    }
}

BlockId lodTreeTrunk(TreeType type) {
    switch (type) {
        case TreeType::BIRCH: return BlockId::BIRCH_WOOD;
        case TreeType::SPRUCE: return BlockId::SPRUCE_WOOD;
        case TreeType::JUNGLE: return BlockId::JUNGLE_WOOD;
        case TreeType::ACACIA: return BlockId::ACACIA_WOOD;
        default: return BlockId::WOOD;
    }
}

bool isLodTreeFoliage(BlockId block) {
    return block == BlockId::LEAVES || block == BlockId::BIRCH_LEAVES ||
           block == BlockId::SPRUCE_LEAVES || block == BlockId::JUNGLE_LEAVES ||
           block == BlockId::ACACIA_LEAVES || block == BlockId::SKYROOT_LEAVES ||
           block == BlockId::CACTUS_BLOCK;
}

bool isFineHeavenDecoration(BlockId block) {
    return block == BlockId::STARFLOWER || block == BlockId::CLOUD_BLOOM ||
           block == BlockId::GLOWSHROOM;
}

int lodTreeRadius(TreeType type) {
    switch (type) {
        case TreeType::JUNGLE:
        case TreeType::ACACIA:
        case TreeType::SWAMP_OAK: return 3;
        case TreeType::CACTUS: return 0;
        default: return 2;
    }
}

bool hasFluidSpan(const LodColumn& column) {
    return std::any_of(column.spans.begin(), column.spans.end(),
        [](const LodSpan& span) { return isFluid(span.block); });
}

void overlayExactChunks(LodTileData& tile, const LodTileKey& key,
                        const std::filesystem::path& root,
                        const WorldGenerator& generator,
                        const std::vector<std::pair<int, int>>& exactChunks) {
    const int cellSize = 1 << key.level;
    const int originX = key.x * LodTileData::SIDE * cellSize;
    const int originZ = key.z * LodTileData::SIDE * cellSize;
    for (const auto& [cx, cz] : exactChunks) {
        LodTileData exact;
        if (!readTileFile(exactPath(root, cx, cz), {cx, cz, 0}, generator, exact))
            continue;
        const int chunkX = cx * Config::CHUNK_SIZE_X;
        const int chunkZ = cz * Config::CHUNK_SIZE_Z;
        const int firstX = std::max(0, floorDiv(chunkX - originX, cellSize));
        const int lastX = std::min(LodTileData::SIDE - 1,
            floorDiv(chunkX + Config::CHUNK_SIZE_X - 1 - originX, cellSize));
        const int firstZ = std::max(0, floorDiv(chunkZ - originZ, cellSize));
        const int lastZ = std::min(LodTileData::SIDE - 1,
            floorDiv(chunkZ + Config::CHUNK_SIZE_Z - 1 - originZ, cellSize));
        for (int tz = firstZ; tz <= lastZ; ++tz) {
            for (int tx = firstX; tx <= lastX; ++tx) {
                // Approximate cells are sampled at their world-space center.
                // Use the exact column at that same representative point;
                // selecting the highest column anywhere in the footprint
                // magnifies a one-block tree, shore, or edit across the whole
                // coarse cell and produces large checkerboard patches.
                const int sampleX = originX + tx * cellSize + cellSize / 2;
                const int sampleZ = originZ + tz * cellSize + cellSize / 2;
                const int localX = sampleX - chunkX;
                const int localZ = sampleZ - chunkZ;
                if (localX >= 0 && localX < Config::CHUNK_SIZE_X &&
                    localZ >= 0 && localZ < Config::CHUNK_SIZE_Z)
                    refineLodColumn(tile.at(tx, tz),
                        exact.at(localX, localZ), cellSize);
            }
        }
    }
}

bool lodSideOccluded(BlockId source, BlockId cover) {
    if (isFluid(source)) {
        return (isWater(source) && isWater(cover)) ||
            (isLava(source) && isLava(cover)) || isSolid(cover);
    }
    return !shouldRenderCubeFace(source, cover);
}

void appendVisibleIntervals(const LodSpan& source, const LodColumn* neighbor,
                            std::vector<std::pair<int, int>>& result) {
    result.clear();
    result.push_back({source.bottom, source.top});
    if (!neighbor) return;
    for (const LodSpan& cover : neighbor->spans) {
        // Water is translucent: it cannot hide the opaque bank behind it.
        // Match the exact chunk mesher's cube/fluid face rules instead of
        // treating every neighboring span as an opaque occluder.
        if (!lodSideOccluded(source.block, cover.block)) continue;
        std::vector<std::pair<int, int>> next;
        for (const auto& interval : result) {
            if (cover.top < interval.first || cover.bottom > interval.second) {
                next.push_back(interval);
                continue;
            }
            if (cover.bottom > interval.first)
                next.push_back({interval.first, cover.bottom - 1});
            if (cover.top < interval.second)
                next.push_back({cover.top + 1, interval.second});
        }
        result.swap(next);
        if (result.empty()) break;
    }
}

void emitFace(ChunkMesh& mesh, std::vector<unsigned int>& opaque,
              std::vector<unsigned int>& translucent, FaceDir face,
              const glm::vec3& minimum, const glm::vec3& maximum,
              BlockId block, float faceOffset = 0.0f) {
    const BlockProperties& properties = getBlockProps(block);
    const BlockTexture texture = getFaceTexture(block, face);
    const float tile = static_cast<float>(getAtlasTextureIndex(texture)) + 15.0f / 512.0f;
    std::vector<unsigned int>& indices = properties.layer == RenderLayer::Translucent
        ? translucent : opaque;
    const unsigned int base = static_cast<unsigned int>(mesh.vertices.size());
    std::array<int, 8> vertexForCorner;
    vertexForCorner.fill(-1);
    for (int cornerIndex : FACE_INDICES[static_cast<size_t>(face)]) {
        int& localIndex = vertexForCorner[static_cast<size_t>(cornerIndex)];
        if (localIndex < 0) {
            const glm::vec3 corner = CUBE_CORNERS[static_cast<size_t>(cornerIndex)];
            const glm::vec3 position = minimum + corner * (maximum - minimum);
            const float u = (face <= FaceDir::BOTTOM || face >= FaceDir::RIGHT)
                ? corner.z * (maximum.z - minimum.z)
                : corner.x * (maximum.x - minimum.x);
            const float v = face <= FaceDir::BOTTOM
                ? corner.x * (maximum.x - minimum.x)
                : corner.y * (maximum.y - minimum.y);
            localIndex = static_cast<int>(mesh.vertices.size() - base);
            mesh.vertices.push_back({position.x, position.y, position.z,
                1.0f, 1.0f, 0.0f, properties.alpha, u, v, tile,
                blockFaceRenderData(block, face) + faceOffset});
        }
        indices.push_back(base + static_cast<unsigned int>(localIndex));
    }
}
}

size_t LodTileData::memoryBytes() const {
    size_t bytes = sizeof(*this);
    for (const LodColumn& column : columns)
        bytes += column.spans.capacity() * sizeof(LodSpan);
    return bytes;
}

std::vector<uint8_t> encodeLodTilePayload(const LodTileData& tile) {
    return encodeTile(tile);
}

bool decodeLodTilePayload(const std::vector<uint8_t>& payload,
                          LodTileData& tile) {
    return decodeTile(payload, tile);
}

LodTileData buildApproximateLodTile(const WorldGenerator& generator,
                                    const LodTileKey& key) {
    LodTileData tile;
    const int cellSize = 1 << key.level;
    const int originX = key.x * LodTileData::SIDE * cellSize;
    const int originZ = key.z * LodTileData::SIDE * cellSize;
    for (int z = 0; z < LodTileData::SIDE; ++z) {
        for (int x = 0; x < LodTileData::SIDE; ++x) {
            int wx = originX + x * cellSize + cellSize / 2;
            int wz = originZ + z * cellSize + cellSize / 2;
            LodColumn& column = tile.at(x, z);
            if (generator.isHeaven()) {
                for (const auto& island : generator.sampleHeavenLayers(wx, wz)) {
                    if (!island.present) continue;
                    column.spans.push_back({static_cast<int16_t>(island.bottom),
                        static_cast<int16_t>(island.top), heavenSurface(island.biome)});
                }
                continue;
            }
            SurfaceColumn sample = generator.sampleTerrainColumn(wx, wz);
            // Narrow rivers and shallow pools often miss the single center
            // sample even though they cover a visible part of a fine LOD cell.
            // Select a deterministic wet sub-sample at nearby quarters, but
            // stop at coarse levels where widening it would create huge water
            // squares instead of useful distant detail.
            if (cellSize > 1 && cellSize <= 16 &&
                sample.waterLevel <= sample.height) {
                const int low = cellSize / 4;
                const int high = cellSize - 1 - low;
                constexpr std::array<std::array<int, 2>, 4> corners{{
                    {{0, 0}}, {{1, 0}}, {{0, 1}}, {{1, 1}}
                }};
                for (const auto& corner : corners) {
                    const int candidateX = originX + x * cellSize +
                        (corner[0] == 0 ? low : high);
                    const int candidateZ = originZ + z * cellSize +
                        (corner[1] == 0 ? low : high);
                    const SurfaceColumn candidate =
                        generator.sampleTerrainColumn(candidateX, candidateZ);
                    if (candidate.waterLevel <= candidate.height) continue;
                    sample = candidate;
                    wx = candidateX;
                    wz = candidateZ;
                    break;
                }
            }
            SurfaceRuleContext context;
            context.biome = sample.biome;
            context.archetype = sample.archetype;
            context.secondaryArchetype = sample.secondaryArchetype;
            context.height = sample.height;
            context.waterLevel = sample.waterLevel;
            context.slope = sample.slope;
            context.localRelief = sample.localRelief;
            context.primaryArchetypeWeight = sample.primaryArchetypeWeight;
            context.secondaryArchetypeWeight = sample.archetypeBlend;
            context.volcanicWeight = sample.volcanicWeight;
            context.craterWeight = sample.craterWeight;
            context.riverWeight = sample.riverWeight;
            context.river = sample.river;
            const SurfaceProfile surface = SurfaceRules::profile(
                generator.getSeed(), wx, wz, context);
            // The surface is a coarse height field, not a floating slab.
            // A short underground span exposes sky below tall neighboring
            // columns and at LOD ring cuts where the two samples disagree.
            column.spans.push_back({static_cast<int16_t>(Config::WORLD_MIN_Y),
                static_cast<int16_t>(sample.height), surface.top});
            if (sample.waterLevel > sample.height) {
                column.spans.push_back({static_cast<int16_t>(sample.height + 1),
                    static_cast<int16_t>(sample.waterLevel), BlockId::WATER});
            }
        }
    }

    // Fine Heaven tiles retain representative, generation-matched surface
    // features. Four samples per axis bound the work independently of the
    // cell size while making narrow trees, crystals and flowers visible.
    if (generator.isHeaven() && cellSize <= 8) {
        const int sampleCount = std::min(cellSize, 4);
        for (int z = 0; z < LodTileData::SIDE; ++z) {
            for (int x = 0; x < LodTileData::SIDE; ++x) {
                const int centerX = originX + x * cellSize + cellSize / 2;
                const int centerZ = originZ + z * cellSize + cellSize / 2;
                const auto centerLayers = generator.sampleHeavenLayers(centerX, centerZ);
                std::array<std::vector<WorldGenerator::HeavenLodFeature>,
                           WorldGenerator::HEAVEN_LAYER_COUNT> selected;
                for (int sampleZ = 0; sampleZ < sampleCount; ++sampleZ) {
                    for (int sampleX = 0; sampleX < sampleCount; ++sampleX) {
                        const int wx = originX + x * cellSize +
                            (sampleX * cellSize + cellSize / 2) / sampleCount;
                        const int wz = originZ + z * cellSize +
                            (sampleZ * cellSize + cellSize / 2) / sampleCount;
                        std::array<std::vector<WorldGenerator::HeavenLodFeature>,
                                   WorldGenerator::HEAVEN_LAYER_COUNT> sampled;
                        for (const auto& feature :
                             generator.sampleHeavenLodFeatures(wx, wz)) {
                            const auto& centerLayer = centerLayers[
                                static_cast<size_t>(feature.layer)];
                            if (centerLayer.biome == feature.biome)
                                sampled[static_cast<size_t>(feature.layer)].push_back(
                                    feature);
                        }
                        for (int layer = 0;
                             layer < WorldGenerator::HEAVEN_LAYER_COUNT; ++layer) {
                            if (!centerLayers[static_cast<size_t>(layer)].present ||
                                !selected[static_cast<size_t>(layer)].empty() ||
                                sampled[static_cast<size_t>(layer)].empty())
                                continue;
                            selected[static_cast<size_t>(layer)] =
                                std::move(sampled[static_cast<size_t>(layer)]);
                        }
                    }
                }

                LodColumn& column = tile.at(x, z);
                for (int layer = 0; layer < WorldGenerator::HEAVEN_LAYER_COUNT;
                     ++layer) {
                    const auto& features = selected[static_cast<size_t>(layer)];
                    if (features.empty()) continue;
                    const int surfaceTop = centerLayers[static_cast<size_t>(layer)].top;
                    for (const auto& feature : features) {
                        if (feature.replacesSurface) {
                            const auto found = std::find_if(
                                column.spans.begin(), column.spans.end(),
                                [surfaceTop](const LodSpan& span) {
                                    return span.top == surfaceTop;
                                });
                            if (found != column.spans.end()) found->block = feature.block;
                            continue;
                        }
                        const int bottom = surfaceTop + feature.bottomOffset;
                        const int top = std::min(Config::WORLD_MAX_Y - 1,
                            surfaceTop + feature.topOffset);
                        if (bottom <= top)
                            column.spans.push_back({static_cast<int16_t>(bottom),
                                static_cast<int16_t>(top), feature.block});
                    }
                }
                std::sort(column.spans.begin(), column.spans.end(),
                    [](const LodSpan& a, const LodSpan& b) {
                        return a.bottom < b.bottom;
                    });
            }
        }
    }

    // Preserve deterministic tree silhouettes in the two finest approximate
    // levels. Individual trees are intentionally omitted once a cell exceeds
    // eight blocks so a canopy can never become a giant distant cube.
    if (!generator.isHeaven() && cellSize <= 8) {
        struct TreeOverlay {
            int bottom = Config::WORLD_MIN_Y;
            int top = Config::WORLD_MIN_Y - 1;
            BlockId block = BlockId::AIR;
        };
        std::array<TreeOverlay, LodTileData::SIDE * LodTileData::SIDE> overlays;
        constexpr int treePadding = 3;
        const int tileSize = LodTileData::SIDE * cellSize;
        const auto trees = generator.sampleLodTrees(
            originX - treePadding, originZ - treePadding,
            tileSize + treePadding * 2, tileSize + treePadding * 2);
        for (const auto& tree : trees) {
            const int radius = lodTreeRadius(tree.type);
            const int treeX = tree.localX - treePadding;
            const int treeZ = tree.localZ - treePadding;
            const int minimumX = std::max(0, floorDiv(treeX - radius, cellSize));
            const int maximumX = std::min(LodTileData::SIDE - 1,
                floorDiv(treeX + radius, cellSize));
            const int minimumZ = std::max(0, floorDiv(treeZ - radius, cellSize));
            const int maximumZ = std::min(LodTileData::SIDE - 1,
                floorDiv(treeZ + radius, cellSize));
            const bool cactus = tree.type == TreeType::CACTUS;
            const int bottom = tree.baseY + (cactus ? 1 :
                std::max(2, tree.trunkHeight - 2));
            const int top = tree.baseY + tree.trunkHeight + (cactus ? 0 : 2);
            if (!cactus) {
                const int trunkX = floorDiv(treeX, cellSize);
                const int trunkZ = floorDiv(treeZ, cellSize);
                if (trunkX >= 0 && trunkX < LodTileData::SIDE &&
                    trunkZ >= 0 && trunkZ < LodTileData::SIDE) {
                    LodColumn& trunkColumn = tile.at(trunkX, trunkZ);
                    trunkColumn.spans.push_back({
                        static_cast<int16_t>(tree.baseY + 1),
                        static_cast<int16_t>(bottom - 1), lodTreeTrunk(tree.type)});
                }
            }
            for (int z = minimumZ; z <= maximumZ; ++z) {
                for (int x = minimumX; x <= maximumX; ++x) {
                    TreeOverlay& overlay = overlays[static_cast<size_t>(
                        x + z * LodTileData::SIDE)];
                    if (top <= overlay.top) continue;
                    overlay = {bottom, top, lodTreeFoliage(tree.type)};
                }
            }
        }
        for (int z = 0; z < LodTileData::SIDE; ++z) {
            for (int x = 0; x < LodTileData::SIDE; ++x) {
                const TreeOverlay& overlay = overlays[static_cast<size_t>(
                    x + z * LodTileData::SIDE)];
                if (overlay.top < overlay.bottom) continue;
                LodColumn& column = tile.at(x, z);
                column.spans.push_back({static_cast<int16_t>(overlay.bottom),
                    static_cast<int16_t>(overlay.top), overlay.block});
                std::sort(column.spans.begin(), column.spans.end(),
                    [](const LodSpan& a, const LodSpan& b) {
                        return a.bottom < b.bottom;
                    });
            }
        }
    }
    return tile;
}

LodTileData extractExactLodChunk(const std::vector<uint8_t>& blocks) {
    LodTileData tile;
    if (blocks.size() != static_cast<size_t>(Config::CHUNK_VOLUME)) return tile;
    auto index = [](int x, int y, int z) {
        return x + z * Config::CHUNK_SIZE_X +
            Config::worldYToStorageY(y) * Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z;
    };
    for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
        for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
            LodColumn& column = tile.at(x, z);
            column.exact = true;
            int y = Config::WORLD_MIN_Y;
            while (y < Config::WORLD_MAX_Y) {
                BlockId block = static_cast<BlockId>(blocks[index(x, y, z)]);
                if (block == BlockId::AIR) { ++y; continue; }
                const int bottom = y;
                const BlockId runBlock = block;
                while (++y < Config::WORLD_MAX_Y) {
                    block = static_cast<BlockId>(blocks[index(x, y, z)]);
                    if (block != runBlock) break;
                }
                column.spans.push_back({static_cast<int16_t>(bottom),
                    static_cast<int16_t>(y - 1), runBlock});
            }
        }
    }
    return tile;
}

void refineLodColumn(LodColumn& approximate, const LodColumn& exact,
                     int cellSize) {
    // Fine approximate cells deliberately recover narrow water that their
    // center sample misses. A later exact center-column extraction must not
    // erase that sub-cell coverage, otherwise exploration alternates the same
    // coarse cell between its blue water surface and yellow seabed. Level zero
    // is already a one-block exact match and should always accept refinement.
    if (cellSize > 1 && hasFluidSpan(approximate) && !hasFluidSpan(exact))
        return;
    if (cellSize > 1 && !approximate.spans.empty() &&
        approximate.spans.front().bottom == Config::WORLD_MIN_Y) {
        // Coarse Overworld cells represent the surface. Copying an entire
        // exact column can replace that continuous base with a cave, a tree,
        // or only the last few runs allowed by the vertical span budget.
        const int surfaceY = approximate.spans.front().top;
        for (const LodSpan& span : exact.spans) {
            if (span.bottom <= surfaceY && span.top >= surfaceY &&
                getBlockProps(span.block).solid) {
                approximate.spans.front().block = span.block;
                break;
            }
        }
        approximate.spans.erase(std::remove_if(approximate.spans.begin(),
            approximate.spans.end(), [surfaceY](const LodSpan& span) {
                return span.bottom > surfaceY && !isFluid(span.block);
            }), approximate.spans.end());
        std::vector<LodSpan> aboveSurface;
        for (const LodSpan& span : exact.spans) {
            if (span.bottom > surfaceY && !isFluid(span.block))
                aboveSurface.push_back(span);
        }
        // Low precision retains six spans. Reserve two for ground and water.
        constexpr size_t maxOverlays = 4;
        const size_t firstOverlay = aboveSurface.size() > maxOverlays
            ? aboveSurface.size() - maxOverlays : 0;
        approximate.spans.insert(approximate.spans.end(),
            aboveSurface.begin() + static_cast<std::ptrdiff_t>(firstOverlay),
            aboveSurface.end());
        std::sort(approximate.spans.begin(), approximate.spans.end(),
            [](const LodSpan& a, const LodSpan& b) {
                return a.bottom < b.bottom;
            });
        approximate.exact = true;
        return;
    }
    approximate = exact;
}

namespace {
bool isFullyExact(const LodTileData& data) {
    return std::all_of(data.columns.begin(), data.columns.end(),
        [](const LodColumn& column) { return column.exact; });
}

LodNeighborEdges sampleNeighborEdges(const WorldGenerator& generator,
                                     const LodTileKey& key) {
    LodNeighborEdges edges;
    const int cellSize = 1 << key.level;
    const int originX = key.x * LodTileData::SIDE * cellSize;
    const int originZ = key.z * LodTileData::SIDE * cellSize;
    for (int i = 0; i < LodTileData::SIDE; ++i) {
        for (int side = 0; side < 4; ++side) {
            const int cellX = side == 2 ? LodTileData::SIDE :
                side == 3 ? -1 : i;
            const int cellZ = side == 0 ? -1 :
                side == 1 ? LodTileData::SIDE : i;
            const int wx = originX + cellX * cellSize + cellSize / 2;
            const int wz = originZ + cellZ * cellSize + cellSize / 2;
            LodColumn& column = edges[static_cast<size_t>(side)][static_cast<size_t>(i)];
            if (generator.isHeaven()) {
                for (const auto& island : generator.sampleHeavenLayers(wx, wz)) {
                    if (island.present)
                        column.spans.push_back({static_cast<int16_t>(island.bottom),
                            static_cast<int16_t>(island.top),
                            heavenSurface(island.biome)});
                }
            } else {
                SurfaceColumn sample = generator.sampleTerrainColumn(wx, wz);
                if (cellSize > 1 && cellSize <= 16 &&
                    sample.waterLevel <= sample.height) {
                    const int low = cellSize / 4;
                    const int high = cellSize - 1 - low;
                    for (int offsetZ : {low, high}) {
                        for (int offsetX : {low, high}) {
                            const SurfaceColumn candidate =
                                generator.sampleTerrainColumn(
                                    originX + cellX * cellSize + offsetX,
                                    originZ + cellZ * cellSize + offsetZ);
                            if (candidate.waterLevel > candidate.height) {
                                sample = candidate;
                                break;
                            }
                        }
                        if (sample.waterLevel > sample.height) break;
                    }
                }
                column.spans.push_back({static_cast<int16_t>(Config::WORLD_MIN_Y),
                    static_cast<int16_t>(sample.height), BlockId::STONE});
                if (sample.waterLevel > sample.height)
                    column.spans.push_back({static_cast<int16_t>(sample.height + 1),
                        static_cast<int16_t>(sample.waterLevel), BlockId::WATER});
            }
        }
    }
    return edges;
}

BlockId exactColumnBlock(const LodColumn& column, int y) {
    for (const LodSpan& span : column.spans) {
        if (y < span.bottom) break;
        if (y <= span.top) return span.block;
    }
    return BlockId::AIR;
}

ChunkMesh buildExactLodTileMesh(const LodTileData& data,
                                const LodExactNeighborTiles* neighbors,
                                bool sealTileEdges) {
    std::vector<uint8_t> blocks(static_cast<size_t>(Config::CHUNK_VOLUME), 0);
    int columnMaxY[Config::CHUNK_SIZE_X][Config::CHUNK_SIZE_Z];
    for (auto& column : columnMaxY)
        std::fill(std::begin(column), std::end(column), Config::WORLD_MIN_Y - 1);
    auto index = [](int x, int y, int z) {
        return x + z * Config::CHUNK_SIZE_X +
            Config::worldYToStorageY(y) * Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z;
    };
    for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
        for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
            for (const LodSpan& span : data.at(x, z).spans) {
                const int bottom = std::max<int>(span.bottom, Config::WORLD_MIN_Y);
                const int top = std::min<int>(span.top, Config::WORLD_MAX_Y - 1);
                for (int y = bottom; y <= top; ++y)
                    blocks[static_cast<size_t>(index(x, y, z))] =
                        static_cast<uint8_t>(span.block);
                columnMaxY[x][z] = std::max(columnMaxY[x][z], top);
            }
        }
    }

    auto neighborBlock = [&](int x, int y, int z) -> BlockId {
        if (!Config::isValidWorldY(y)) return BlockId::AIR;
        if (x >= 0 && x < Config::CHUNK_SIZE_X &&
            z >= 0 && z < Config::CHUNK_SIZE_Z)
            return static_cast<BlockId>(blocks[static_cast<size_t>(index(x, y, z))]);

        const int dx = x < 0 ? -1 : (x >= Config::CHUNK_SIZE_X ? 1 : 0);
        const int dz = z < 0 ? -1 : (z >= Config::CHUNK_SIZE_Z ? 1 : 0);
        const int localX = x < 0 ? Config::CHUNK_SIZE_X - 1 :
            (x >= Config::CHUNK_SIZE_X ? 0 : x);
        const int localZ = z < 0 ? Config::CHUNK_SIZE_Z - 1 :
            (z >= Config::CHUNK_SIZE_Z ? 0 : z);
        if (neighbors) {
            for (size_t i = 0; i < ChunkMesh::NEIGHBOR_DEPENDENCY_OFFSETS.size(); ++i) {
                const auto& offset = ChunkMesh::NEIGHBOR_DEPENDENCY_OFFSETS[i];
                if (offset[0] == dx && offset[1] == dz && (*neighbors)[i])
                    return exactColumnBlock((*neighbors)[i]->at(localX, localZ), y);
            }
        }

        // Match the prior LOD edge policy when an adjacent exact cache does
        // not exist: extend fluid at the boundary so a missing tile cannot
        // create a full-depth ocean wall, while ordinary solids expose a face.
        const int edgeX = std::clamp(x, 0, Config::CHUNK_SIZE_X - 1);
        const int edgeZ = std::clamp(z, 0, Config::CHUNK_SIZE_Z - 1);
        const BlockId edge = static_cast<BlockId>(
            blocks[static_cast<size_t>(index(edgeX, y, edgeZ))]);
        return isFluid(edge) ? edge : BlockId::AIR;
    };
    auto fixedLodLight = [](int, int, int) -> LightSample { return {15, 0}; };

    ChunkMesh mesh;
    mesh.build(0, 0, blocks.data(), columnMaxY, neighborBlock, fixedLodLight);
    // LOD is never submitted to the shadow pass.  The shared builder reuses
    // opaque vertices for shadow indices, so trimming the unused tail retains
    // every visible index without changing geometry.
    const size_t visibleIndices = mesh.translucentIndexOffset +
        mesh.translucentIndexCount;
    mesh.indices.resize(visibleIndices);
    if (sealTileEdges) {
        std::vector<unsigned int> opaque(
            mesh.indices.begin(), mesh.indices.begin() +
                static_cast<std::ptrdiff_t>(mesh.opaqueIndexCount));
        std::vector<unsigned int> translucent(
            mesh.indices.begin() +
                static_cast<std::ptrdiff_t>(mesh.translucentIndexOffset),
            mesh.indices.end());
        constexpr FaceDir faces[] = {FaceDir::FRONT, FaceDir::BACK,
                                     FaceDir::RIGHT, FaceDir::LEFT};
        for (int side = 0; side < 4; ++side) {
            for (int i = 0; i < Config::CHUNK_SIZE_X; ++i) {
                const int x = side == 2 ? Config::CHUNK_SIZE_X - 1 :
                    side == 3 ? 0 : i;
                const int z = side == 0 ? 0 :
                    side == 1 ? Config::CHUNK_SIZE_Z - 1 : i;
                const int neighborX = side == 2 ? Config::CHUNK_SIZE_X :
                    side == 3 ? -1 : i;
                const int neighborZ = side == 0 ? -1 :
                    side == 1 ? Config::CHUNK_SIZE_Z : i;
                auto hiddenBlock = [&](int y) {
                    if (!Config::isValidWorldY(y)) return BlockId::AIR;
                    const BlockId block = static_cast<BlockId>(
                        blocks[static_cast<size_t>(index(x, y, z))]);
                    if (getBlockProps(block).shape != RenderShape::Cube &&
                        !isFluid(block))
                        return BlockId::AIR;
                    const BlockId cover = neighborBlock(neighborX, y, neighborZ);
                    return lodSideOccluded(block, cover)
                        ? block : BlockId::AIR;
                };
                for (int y = Config::WORLD_MIN_Y; y < Config::WORLD_MAX_Y;) {
                    const BlockId block = hiddenBlock(y);
                    if (block == BlockId::AIR) { ++y; continue; }
                    const int bottom = y;
                    while (y + 1 < Config::WORLD_MAX_Y &&
                           hiddenBlock(y + 1) == block) ++y;
                    const float top = isFluid(block)
                        ? static_cast<float>(y) + fluidSurfaceHeight(block) - 0.001f
                        : static_cast<float>(y + 1);
                    glm::vec3 minimum(static_cast<float>(x),
                        static_cast<float>(bottom), static_cast<float>(z));
                    glm::vec3 maximum(minimum.x + 1.0f, top, minimum.z + 1.0f);
                    if (side == 0) maximum.z = minimum.z;
                    else if (side == 1) minimum.z = maximum.z;
                    else if (side == 2) minimum.x = maximum.x;
                    else maximum.x = minimum.x;
                    emitFace(mesh, opaque, translucent, faces[side],
                        minimum, maximum, block, 64.0f);
                    ++y;
                }
            }
        }
        mesh.indices = std::move(opaque);
        mesh.opaqueIndexCount = mesh.indices.size();
        mesh.translucentIndexOffset = mesh.indices.size();
        mesh.indices.insert(mesh.indices.end(), translucent.begin(),
                            translucent.end());
        mesh.translucentIndexCount = translucent.size();
    }
    mesh.shadowCasterIndexOffset = 0;
    mesh.shadowCasterIndexCount = 0;
    mesh.indexCount = mesh.indices.size();
    return mesh;
}
}

ChunkMesh buildLodTileMesh(const LodTileData& data, int cellSize,
                           int maximumSpans,
                           const LodExactNeighborTiles* neighbors,
                           const LodNeighborEdges* edges,
                           bool sealTileEdges) {
    if (cellSize == 1 && isFullyExact(data))
        return buildExactLodTileMesh(data, neighbors, sealTileEdges);

    ChunkMesh mesh;
    std::vector<unsigned int> opaque, translucent;
    std::vector<std::pair<int, int>> intervals;
    for (int z = 0; z < LodTileData::SIDE; ++z) {
        for (int x = 0; x < LodTileData::SIDE; ++x) {
            const LodColumn& column = data.at(x, z);
            // The precision budget may simplify decoration, but dropping a
            // lower solid span removes an entire floating Heaven island.
            const size_t first = column.spans.size() > static_cast<size_t>(maximumSpans)
                ? column.spans.size() - static_cast<size_t>(maximumSpans) : 0;
            for (size_t spanIndex = 0; spanIndex < column.spans.size(); ++spanIndex) {
                const LodSpan& span = column.spans[spanIndex];
                const BlockProperties& properties = getBlockProps(span.block);
                if (spanIndex < first && !properties.solid &&
                    !isFluid(span.block)) continue;
                if (cellSize > 1 && !properties.solid && !isFluid(span.block) &&
                    !(cellSize <= 2 && isFineHeavenDecoration(span.block)))
                    continue;
                const float x0 = static_cast<float>(x * cellSize);
                const float x1 = static_cast<float>((x + 1) * cellSize);
                const float z0 = static_cast<float>(z * cellSize);
                const float z1 = static_cast<float>((z + 1) * cellSize);
                // Match the shared fluid mesher's source height. LOD used to
                // cap water at a full block while exact chunks cap a source at
                // 8/9, leaving a raised shelf wherever the two paths meet.
                const float spanTop = isFluid(span.block)
                    ? static_cast<float>(span.top) +
                        fluidSurfaceHeight(span.block) - 0.001f
                    : static_cast<float>(span.top + 1);
                emitFace(mesh, opaque, translucent, FaceDir::TOP,
                    {x0, spanTop, z0}, {x1, spanTop, z1}, span.block);
                // A single approximate ground span has no visible underside.
                // Exact columns and floating multi-span terrain still retain
                // bottoms for caves, overhangs, and Heaven islands. Fluids
                // never have a bottom face in the exact mesher: emitting one
                // here makes it coplanar with the seabed top and causes the
                // sand/water pattern to flicker as the camera moves.
                const bool hasTreeOverlay = !column.exact &&
                    !column.spans.empty() &&
                    isLodTreeFoliage(column.spans.back().block);
                const bool coveredBelow = spanIndex > 0 &&
                    column.spans[spanIndex - 1].top + 1 >= span.bottom &&
                    getBlockProps(column.spans[spanIndex - 1].block).solid;
                if (!isFluid(span.block) && (column.exact ||
                    (column.spans.size() > 1 &&
                     (!hasTreeOverlay || spanIndex + 1 == column.spans.size()))) &&
                    !coveredBelow) {
                    emitFace(mesh, opaque, translucent, FaceDir::BOTTOM,
                        {x0, static_cast<float>(span.bottom), z0},
                        {x1, static_cast<float>(span.bottom), z1}, span.block);
                }
                struct Side { int dx, dz; FaceDir face; };
                constexpr Side sides[] = {{0,-1,FaceDir::FRONT}, {0,1,FaceDir::BACK},
                    {1,0,FaceDir::RIGHT}, {-1,0,FaceDir::LEFT}};
                for (size_t sideIndex = 0; sideIndex < std::size(sides); ++sideIndex) {
                    const Side& side = sides[sideIndex];
                    const int nx = x + side.dx, nz = z + side.dz;
                    const LodColumn* neighbor = nx >= 0 && nx < LodTileData::SIDE &&
                        nz >= 0 && nz < LodTileData::SIDE ? &data.at(nx, nz) : nullptr;
                    if (!neighbor && edges)
                        neighbor = &(*edges)[sideIndex][static_cast<size_t>(
                            side.dx == 0 ? x : z)];
                    // Direct mesh callers may omit edge samples. In that case
                    // an unknown fluid neighbor must not create a full-depth
                    // square wall across an otherwise continuous ocean.
                    const bool tileEdge = nx < 0 || nx >= LodTileData::SIDE ||
                        nz < 0 || nz >= LodTileData::SIDE;
                    if (!neighbor && isFluid(span.block)) intervals.clear();
                    else appendVisibleIntervals(span, neighbor, intervals);
                    auto emitInterval = [&](const std::pair<int, int>& interval,
                                            float faceOffset) {
                        glm::vec3 minimum{x0, static_cast<float>(interval.first), z0};
                        glm::vec3 maximum{x1, static_cast<float>(interval.second + 1), z1};
                        if (isFluid(span.block) && interval.second == span.top)
                            maximum.y = spanTop;
                        if (side.face == FaceDir::FRONT) maximum.z = minimum.z;
                        else if (side.face == FaceDir::BACK) minimum.z = maximum.z;
                        else if (side.face == FaceDir::RIGHT) minimum.x = maximum.x;
                        else maximum.x = minimum.x;
                        emitFace(mesh, opaque, translucent, side.face,
                                 minimum, maximum, span.block, faceOffset);
                    };
                    for (const auto& interval : intervals)
                        emitInterval(interval, 0.0f);
                    if (sealTileEdges && tileEdge) {
                        // Keep the portions hidden by a same-level neighbor as
                        // transition skirts. The fragment shader displays them
                        // only where the next tile belongs to another ring.
                        int next = span.bottom;
                        for (const auto& interval : intervals) {
                            if (next < interval.first)
                                emitInterval({next, interval.first - 1}, 64.0f);
                            next = interval.second + 1;
                        }
                        if (next <= span.top)
                            emitInterval({next, span.top}, 64.0f);
                    }
                }
            }
        }
    }
    mesh.indices = std::move(opaque);
    mesh.opaqueIndexCount = mesh.indices.size();
    mesh.translucentIndexOffset = mesh.indices.size();
    mesh.indices.insert(mesh.indices.end(), translucent.begin(), translucent.end());
    mesh.translucentIndexCount = translucent.size();
    mesh.indexCount = mesh.indices.size();
    return mesh;
}

uint64_t LodTerrainSystem::packedChunkKey(int cx, int cz) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
           static_cast<uint32_t>(cz);
}

LodTerrainSystem::~LodTerrainSystem() {
    if (m_threadPool) m_threadPool->waitIdle();
    releaseGpuMeshes();
}

void LodTerrainSystem::setSaveStore(SaveStore* store) {
    m_saveStore = store;
    m_cacheRoot = store ? store->worldDirectory() / "lod" / "r4" /
        ("d_" + std::to_string(static_cast<int>(m_generator
            ? m_generator->dimension() : DimensionId::Overworld)))
        : std::filesystem::path{};
    m_exactColumnsByX.clear();
    m_scannedCacheRoot.clear();
    if (store) scanExactCache();
}

void LodTerrainSystem::reset(WorldGenerator* generator) {
    if (m_threadPool) m_threadPool->waitIdle();
    releaseGpuMeshes();
    m_tiles.clear();
    m_desired.clear();
    m_submissions.clear();
    m_exactRevisions.clear();
    m_nearFallbackChunks.clear();
    m_completions.clear();
    m_exactCompletions.clear();
    m_cpuBytes = m_gpuBytes = 0;
    m_tasksInFlight = 0;
    m_generator = generator;
    m_nextRequestLevel = 0;
    if (m_saveStore && m_generator) {
        m_cacheRoot = m_saveStore->worldDirectory() / "lod" / "r4" /
            ("d_" + std::to_string(static_cast<int>(m_generator->dimension())));
    }
    ++m_epoch;
    m_selectionDirty = true;
    if (m_saveStore) scanExactCache();
}

void LodTerrainSystem::configure(const LodSettings& settings) {
    if (settings == m_settings) return;
    const bool geometryChanged = settings.precision != m_settings.precision;
    const bool selectionChanged = geometryChanged ||
        settings.distanceChunks != m_settings.distanceChunks ||
        settings.enabled != m_settings.enabled;
    m_settings = settings;
    if (selectionChanged) {
        ++m_epoch;
        m_selectionDirty = true;
        if (geometryChanged) {
            // Existing meshes remain valid approximations while the new span
            // budget rebuilds them. Keep them visible until replacements have
            // crossed the GPU upload boundary.
            for (auto& [key, tile] : m_tiles) {
                (void)key;
                tile->dirty = true;
            }
        }
    }
    if (!m_settings.enabled) {
        releaseGpuMeshes();
        m_tiles.clear();
        m_cpuBytes = 0;
        m_desired.clear();
        m_submissions.clear();
    } else if (selectionChanged && m_generator) {
        rebuildSelection();
        enqueueRequests();
        rebuildSubmissions();
    }
}

void LodTerrainSystem::scanExactCache() {
    if (m_cacheRoot == m_scannedCacheRoot) return;
    m_exactColumnsByX.clear();
    m_scannedCacheRoot = m_cacheRoot;
    std::error_code error;
    const auto directory = m_cacheRoot / "exact";
    if (!std::filesystem::is_directory(directory, error)) return;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error || !entry.is_regular_file()) continue;
        int cx = 0, cz = 0;
        if (std::sscanf(entry.path().filename().string().c_str(), "c_%d_%d.lod", &cx, &cz) == 2)
            m_exactColumnsByX[cx].insert(cz);
    }
}

std::vector<std::pair<int, int>> LodTerrainSystem::exactChunksForTile(
    const LodTileKey& key) const {
    std::vector<std::pair<int, int>> result;
    const int chunksPerSide = 1 << key.level;
    const int minimumX = key.x * chunksPerSide;
    const int minimumZ = key.z * chunksPerSide;
    const int maximumX = minimumX + chunksPerSide;
    const int maximumZ = minimumZ + chunksPerSide;
    for (auto x = m_exactColumnsByX.lower_bound(minimumX);
         x != m_exactColumnsByX.end() && x->first < maximumX; ++x) {
        for (auto z = x->second.lower_bound(minimumZ);
             z != x->second.end() && *z < maximumZ; ++z)
            result.push_back({x->first, *z});
    }
    return result;
}

void LodTerrainSystem::rebuildSelection() {
    m_desired.clear();
    if (!m_settings.enabled || !m_generator) return;
    const int quality = lodHorizontalQuality(m_settings.precision);
    // Keep two full chunk rings of overlap so an asynchronously missing near
    // mesh still has stable terrain behind it. Unlike the former fixed
    // eight-block cutoff, this follows the player's render-distance setting.
    const float inner = static_cast<float>(
        std::max(0, m_nearDistanceChunks - 2) * Config::CHUNK_SIZE_X);
    const float outer = static_cast<float>(m_settings.distanceChunks * Config::CHUNK_SIZE_X);
    const float centerX = static_cast<float>(m_centerChunkX * Config::CHUNK_SIZE_X + 8);
    const float centerZ = static_cast<float>(m_centerChunkZ * Config::CHUNK_SIZE_Z + 8);
    // Level 12 reaches 4096-block cells, which is required for the 4096-chunk
    // hard limit at the low precision preset without expanding the outer ring.
    constexpr int maximumLevel = 12;
    // Request a small guard band before a tile reaches either visible edge.
    // At normal movement speeds this gives the worker lane multiple frames to
    // generate and upload it instead of exposing sky at the moving frontier.
    constexpr float prefetchDistance = 2.0f * Config::CHUNK_SIZE_X;
    // Always hand the real terrain to a one-block LOD grid before coarsening.
    // Without this band a large near render distance can skip level zero and
    // meet a two- or four-block ocean mesh directly at the visible boundary.
    constexpr float fineBoundaryBand = Config::CHUNK_SIZE_X;
    struct Band {
        int level;
        int tileSize;
        float minimum;
        float maximum;
    };
    std::vector<Band> bands;
    float previousMaximum = inner;
    for (int level = 0; level <= maximumLevel; ++level) {
        const int cellSize = 1 << level;
        const int tileSize = LodTileData::SIDE * cellSize;
        const float nominalMinimum = level == 0
            ? inner : cellSize * quality * 0.5f;
        const float minimum = std::max(previousMaximum, nominalMinimum);
        float levelMaximum = level == maximumLevel
            ? outer : static_cast<float>(cellSize * quality);
        if (level == 0)
            levelMaximum = std::max(levelMaximum, inner + fineBoundaryBand);
        const float maximum = std::min(outer, levelMaximum);
        if (maximum <= minimum) continue;
        bands.push_back({level, tileSize, minimum, maximum});
        previousMaximum = maximum;
    }
    for (size_t bandIndex = 0; bandIndex < bands.size(); ++bandIndex) {
        const Band& band = bands[bandIndex];
        const int level = band.level;
        const int tileSize = band.tileSize;
        const float minimum = band.minimum;
        const float maximum = band.maximum;
        // Both sides of a precision transition classify the same complete
        // coarse tile. Keep every fine tile in that coarse footprint resident.
        const float minimumGrid = bandIndex == 0 ? 0.0f :
            static_cast<float>(tileSize);
        const float maximumGrid = bandIndex + 1 == bands.size() ? 0.0f :
            static_cast<float>(bands[bandIndex + 1].tileSize);
        const float selectionPadding = level == 0
            ? fineBoundaryBand : prefetchDistance;
        const float boundsPadding = selectionPadding +
            (maximumGrid > 0.0f ? maximumGrid : 0.0f);
        const int minTileX = floorDiv(static_cast<int>(std::floor(
            centerX - maximum - boundsPadding)), tileSize);
        const int maxTileX = floorDiv(static_cast<int>(std::floor(
            centerX + maximum + boundsPadding)), tileSize);
        const int minTileZ = floorDiv(static_cast<int>(std::floor(
            centerZ - maximum - boundsPadding)), tileSize);
        const int maxTileZ = floorDiv(static_cast<int>(std::floor(
            centerZ + maximum + boundsPadding)), tileSize);
        for (int tz = minTileZ; tz <= maxTileZ; ++tz) {
            for (int tx = minTileX; tx <= maxTileX; ++tx) {
                const float tileCenterX = (tx + 0.5f) * tileSize;
                const float tileCenterZ = (tz + 0.5f) * tileSize;
                const float dx = tileCenterX - centerX;
                const float dz = tileCenterZ - centerZ;
                const float distance = std::sqrt(dx * dx + dz * dz);
                const float margin = tileSize * 0.72f;
                if (minimumGrid > 0.0f
                        ? distance + selectionPadding < minimum
                        : distance + margin + selectionPadding < minimum)
                    continue;
                if (maximumGrid > 0.0f) {
                    const int grid = static_cast<int>(maximumGrid);
                    const float parentX =
                        (floorDiv(tx * tileSize, grid) + 0.5f) * grid;
                    const float parentZ =
                        (floorDiv(tz * tileSize, grid) + 0.5f) * grid;
                    const float parentDx = parentX - centerX;
                    const float parentDz = parentZ - centerZ;
                    if (std::sqrt(parentDx * parentDx + parentDz * parentDz)
                        >= maximum + selectionPadding)
                        continue;
                } else if (distance - margin - selectionPadding > maximum) {
                    continue;
                }
                m_desired.push_back({{tx, tz, static_cast<uint8_t>(level)},
                    minimum, maximum, dx * dx + dz * dz,
                    minimumGrid, maximumGrid});
            }
        }
    }

    // The configured inner boundary assumes every near chunk already has a
    // GPU mesh. During fast movement that is temporarily false: retiring the
    // level-zero tile first exposes sky until generation, meshing and upload
    // catch up. Retain/request only the missing chunks as a transient backing
    // layer. The normal chunks are drawn after the LOD depth clear, so each
    // fallback disappears cleanly as soon as its replacement becomes ready.
    if (inner > 0.0f) {
        for (uint64_t packed : m_nearFallbackChunks) {
            const int chunkX = static_cast<int>(
                static_cast<int32_t>(packed >> 32));
            const int chunkZ = static_cast<int>(
                static_cast<int32_t>(packed & 0xffffffffu));
            const float tileCenterX =
                static_cast<float>(chunkX * Config::CHUNK_SIZE_X + 8);
            const float tileCenterZ =
                static_cast<float>(chunkZ * Config::CHUNK_SIZE_Z + 8);
            const float dx = tileCenterX - centerX;
            const float dz = tileCenterZ - centerZ;
            const float distance2 = dx * dx + dz * dz;
            constexpr float tileRadius = Config::CHUNK_SIZE_X * 0.72f;
            if (std::sqrt(distance2) - tileRadius >= inner) continue;

            const LodTileKey key{chunkX, chunkZ, 0};
            const auto existing = std::find_if(
                m_desired.begin(), m_desired.end(),
                [&key](const Request& request) { return request.key == key; });
            if (existing != m_desired.end()) {
                existing->minimumDistance = 0.0f;
                existing->maximumDistance =
                    std::max(existing->maximumDistance, inner);
                existing->distance2 = distance2;
            } else {
                m_desired.push_back({key, 0.0f, inner, distance2});
            }
        }
    }
    std::sort(m_desired.begin(), m_desired.end(),
        [](const Request& a, const Request& b) { return a.distance2 < b.distance2; });

    std::unordered_set<LodTileKey, LodTileKeyHash> wanted;
    for (const Request& request : m_desired) wanted.insert(request.key);
    for (auto it = m_tiles.begin(); it != m_tiles.end();) {
        if (wanted.count(it->first) != 0) { ++it; continue; }
        if (it->second->mesh.gpuReady && m_renderer) {
            m_gpuBytes -= std::min(m_gpuBytes, it->second->gpuBytes);
            m_renderer->releaseChunkMesh(it->second->mesh);
            it->second->gpuBytes = 0;
        }
        size_t cpuBytes = it->second->data.memoryBytes() +
            it->second->mesh.uploadBytes();
        if (it->second->pendingData)
            cpuBytes += it->second->pendingData->memoryBytes() +
                it->second->pendingMesh->uploadBytes();
        m_cpuBytes -= std::min(m_cpuBytes, cpuBytes);
        it = m_tiles.erase(it);
    }
    m_selectionDirty = false;
}

size_t LodTerrainSystem::selectedTileCountAtLevel(uint8_t level) const {
    return static_cast<size_t>(std::count_if(
        m_desired.begin(), m_desired.end(), [level](const Request& request) {
            return request.key.level == level;
        }));
}

bool LodTerrainSystem::isTileSelected(const LodTileKey& key) const {
    return std::any_of(m_desired.begin(), m_desired.end(),
        [&key](const Request& request) { return request.key == key; });
}

bool LodTerrainSystem::hasExactChunk(int cx, int cz) const {
    const auto found = m_exactColumnsByX.find(cx);
    return found != m_exactColumnsByX.end() &&
        found->second.count(cz) != 0;
}

size_t LodTerrainSystem::residentTileCount() const {
    return static_cast<size_t>(std::count_if(
        m_tiles.begin(), m_tiles.end(), [](const auto& entry) {
            return entry.second->resident;
        }));
}

bool LodTerrainSystem::coverageReady() const {
    if (!m_settings.enabled) return true;
    if (m_selectionDirty) return false;
    return std::all_of(m_desired.begin(), m_desired.end(), [this](const Request& request) {
        const auto found = m_tiles.find(request.key);
        return found != m_tiles.end() && found->second->resident &&
            (found->second->mesh.gpuReady ||
             (found->second->mesh.indexCount == 0 &&
              !found->second->pendingMesh));
    });
}

float LodTerrainSystem::coverageFraction() const {
    if (!m_settings.enabled || (!m_selectionDirty && m_desired.empty()))
        return 1.0f;
    if (m_selectionDirty || m_desired.empty()) return 0.0f;
    const size_t ready = static_cast<size_t>(std::count_if(
        m_desired.begin(), m_desired.end(), [this](const Request& request) {
            const auto found = m_tiles.find(request.key);
            return found != m_tiles.end() && found->second->resident &&
                (found->second->mesh.gpuReady ||
                 (found->second->mesh.indexCount == 0 &&
                  !found->second->pendingMesh));
        }));
    return static_cast<float>(ready) / static_cast<float>(m_desired.size());
}

size_t LodTerrainSystem::residentTileCountAtLevel(uint8_t level) const {
    return static_cast<size_t>(std::count_if(
        m_tiles.begin(), m_tiles.end(), [level](const auto& entry) {
            return entry.first.level == level && entry.second->resident;
        }));
}

float LodTerrainSystem::selectedMinimumDistanceAtLevel(uint8_t level) const {
    float distance = std::numeric_limits<float>::max();
    for (const Request& request : m_desired) {
        if (request.key.level == level)
            distance = std::min(distance, request.minimumDistance);
    }
    return distance;
}

float LodTerrainSystem::selectedMaximumDistance() const {
    float distance = 0.0f;
    for (const Request& request : m_desired)
        distance = std::max(distance, request.maximumDistance);
    return distance;
}

void LodTerrainSystem::observeExactChunks(const std::vector<Chunk*>& activeChunks) {
    if (!m_threadPool || !m_generator || m_cacheRoot.empty()) return;
    const LodWorkBudget budget = lodWorkBudget(m_settings.aggressiveness);
    if (m_tasksInFlight.load() >= budget.maxInFlight) return;
    for (Chunk* chunk : activeChunks) {
        if (!chunk || !chunk->generated.load()) continue;
        const uint64_t packed = packedChunkKey(chunk->cx, chunk->cz);
        const uint64_t revision = chunk->dataRevision();
        const auto found = m_exactRevisions.find(packed);
        if (found != m_exactRevisions.end() && found->second == revision) continue;
        std::vector<uint8_t> blocks, light;
        chunk->copyRawState(blocks, light);
        m_exactRevisions[packed] = revision;
        const int cx = chunk->cx, cz = chunk->cz;
        WorldGenerator* generator = m_generator;
        const auto root = m_cacheRoot;
        ++m_tasksInFlight;
        auto task = [this, blocks = std::move(blocks), cx, cz,
                     revision, generator, root]() mutable {
            struct CompletionGuard {
                std::atomic<int>& count;
                ~CompletionGuard() { --count; }
            } guard{m_tasksInFlight};
            try {
                LodTileData data = extractExactLodChunk(blocks);
                const bool persisted = writeTileFile(
                    exactPath(root, cx, cz), {cx, cz, 0}, *generator, data);
                std::lock_guard lock(m_completionMutex);
                m_exactCompletions.push_back(
                    {cx, cz, revision, persisted});
            } catch (...) {
                std::lock_guard lock(m_completionMutex);
                m_exactCompletions.push_back({cx, cz, revision, false});
                throw;
            }
        };
        try {
            m_threadPool->enqueuePriority(std::move(task), 1000);
        } catch (...) {
            --m_tasksInFlight;
            m_exactRevisions.erase(packed);
            throw;
        }
        break;
    }
}

void LodTerrainSystem::enqueueRequests() {
    if (!m_threadPool || !m_generator || m_cacheRoot.empty()) return;
    const LodWorkBudget budget = lodWorkBudget(m_settings.aggressiveness);
    auto enqueuePass = [&](bool replacements) {
        constexpr uint8_t levelCount = 13;
        while (m_tasksInFlight.load() < budget.maxInFlight) {
            bool enqueued = false;
            for (uint8_t offset = 0; offset < levelCount && !enqueued; ++offset) {
                const uint8_t level = static_cast<uint8_t>(
                    (m_nextRequestLevel + offset) % levelCount);
                for (const Request& request : m_desired) {
                    if (request.key.level != level) continue;
                    auto found = m_tiles.find(request.key);
                    if (found != m_tiles.end()) {
                        found->second->minimumDistance = request.minimumDistance;
                        found->second->maximumDistance = request.maximumDistance;
                        found->second->distance2 = request.distance2;
                        if (found->second->queued || found->second->pendingMesh ||
                            (!found->second->dirty && found->second->resident))
                            continue;
                    }
                    const bool hasRenderableMesh = found != m_tiles.end() &&
                        (found->second->mesh.gpuReady ||
                         !found->second->mesh.empty());
                    if (hasRenderableMesh != replacements) continue;
                    if (found == m_tiles.end()) {
                        auto tile = std::make_unique<Tile>();
                        tile->key = request.key;
                        found = m_tiles.emplace(request.key, std::move(tile)).first;
                    }
                    found->second->queued = true;
                    found->second->dirty = false;
                    found->second->minimumDistance = request.minimumDistance;
                    found->second->maximumDistance = request.maximumDistance;
                    found->second->distance2 = request.distance2;
                    const uint64_t epoch = m_epoch;
                    const int spanLimit = lodVerticalSpanLimit(m_settings.precision);
                    WorldGenerator* generator = m_generator;
                    const auto root = m_cacheRoot;
                    const auto exact = exactChunksForTile(request.key);
                    const LodTileKey key = request.key;
                    ++m_tasksInFlight;
                    auto task = [this, key, epoch, spanLimit, generator,
                                 root, exact]() {
                        struct CompletionGuard {
                            std::atomic<int>& count;
                            ~CompletionGuard() { --count; }
                        } guard{m_tasksInFlight};
                        try {
                            LodTileData data;
                            if (!readTileFile(tilePath(root, key), key,
                                              *generator, data)) {
                                data = buildApproximateLodTile(*generator, key);
                                writeTileFile(tilePath(root, key), key,
                                              *generator, data);
                            }
                            // Older r4 approximate tiles stored only a short
                            // surface slab. Repair them in memory as well so
                            // existing worlds get continuous coverage.
                            if (!generator->isHeaven()) {
                                for (LodColumn& column : data.columns) {
                                    if (!column.exact && !column.spans.empty())
                                        column.spans.front().bottom =
                                            Config::WORLD_MIN_Y;
                                }
                            }
                            overlayExactChunks(data, key, root, *generator, exact);
                            LodExactNeighborTiles neighbors;
                            const LodExactNeighborTiles* neighborPointer = nullptr;
                            if (key.level == 0 && isFullyExact(data)) {
                                neighbors = readExactNeighbors(
                                    root, *generator, key.x, key.z);
                                neighborPointer = &neighbors;
                            }
                            LodNeighborEdges edges;
                            const LodNeighborEdges* edgePointer = nullptr;
                            if (key.level > 0 || !isFullyExact(data)) {
                                edges = sampleNeighborEdges(*generator, key);
                                edgePointer = &edges;
                            }
                            ChunkMesh mesh = buildLodTileMesh(
                                data, 1 << key.level, spanLimit, neighborPointer,
                                edgePointer, true);
                            {
                                std::lock_guard lock(m_completionMutex);
                                m_completions.push_back({key, epoch,
                                    std::move(data), std::move(mesh)});
                            }
                        } catch (...) {
                            std::lock_guard lock(m_completionMutex);
                            m_completions.push_back({key, epoch, {}, {}, false});
                            throw;
                        }
                    };
                    try {
                        m_threadPool->enqueuePriority(std::move(task),
                            100 - static_cast<int>(
                                std::min(request.distance2, 1000000.0f)));
                    } catch (...) {
                        --m_tasksInFlight;
                        found->second->queued = false;
                        found->second->dirty = true;
                        throw;
                    }
                    m_nextRequestLevel = static_cast<uint8_t>(
                        (level + 1) % levelCount);
                    enqueued = true;
                    break;
                }
            }
            if (!enqueued) return;
        }
    };
    // Missing coverage is correctness-critical. Cycle across active levels so
    // dense fine rings cannot postpone all outer Heaven bands; requests within
    // each level retain their near-to-far ordering. Stale exact refinements
    // keep their old GPU mesh and must never starve a newly visible tile.
    enqueuePass(false);
    enqueuePass(true);
}

void LodTerrainSystem::update(const glm::dvec3& playerPosition,
                              int nearDistanceChunks,
                              const std::vector<Chunk*>& activeChunks) {
    if (!m_settings.enabled || !m_generator) return;
    m_playerPosition = playerPosition;
    const int cx = static_cast<int>(std::floor(playerPosition.x / Config::CHUNK_SIZE_X));
    const int cz = static_cast<int>(std::floor(playerPosition.z / Config::CHUNK_SIZE_Z));
    bool jumped = false;
    if (cx != m_centerChunkX || cz != m_centerChunkZ ||
        nearDistanceChunks != m_nearDistanceChunks) {
        if (std::abs(cx - m_centerChunkX) > 8 ||
            std::abs(cz - m_centerChunkZ) > 8) {
            ++m_epoch;
            jumped = true;
        }
        m_centerChunkX = cx;
        m_centerChunkZ = cz;
        m_nearDistanceChunks = nearDistanceChunks;
        m_selectionDirty = true;
    }
    std::unordered_set<uint64_t> nearFallbackChunks;
    const int64_t nearDistance2 = static_cast<int64_t>(nearDistanceChunks) *
        nearDistanceChunks;
    // ChunkStreamer allocates only a bounded number of chunks each frame.
    // Start with the complete target, including coordinates not allocated yet,
    // then remove the chunks whose replacement geometry is available.
    for (int dz = -nearDistanceChunks; dz <= nearDistanceChunks; ++dz) {
        for (int dx = -nearDistanceChunks; dx <= nearDistanceChunks; ++dx) {
            if (static_cast<int64_t>(dx) * dx +
                static_cast<int64_t>(dz) * dz <= nearDistance2)
                nearFallbackChunks.insert(packedChunkKey(cx + dx, cz + dz));
        }
    }
    for (const Chunk* chunk : activeChunks) {
        if (!chunk) continue;
        const int64_t dx = static_cast<int64_t>(chunk->cx) - cx;
        const int64_t dz = static_cast<int64_t>(chunk->cz) - cz;
        if (dx * dx + dz * dz > nearDistance2) continue;
        const ChunkMesh& mesh = chunk->getMesh();
        const bool renderable =
            chunk->lifecycle.load() == Chunk::LifecycleState::Renderable &&
            (mesh.indexCount == 0 || mesh.gpuReady);
        if (renderable)
            nearFallbackChunks.erase(packedChunkKey(chunk->cx, chunk->cz));
    }
    if (nearFallbackChunks != m_nearFallbackChunks) {
        m_nearFallbackChunks = std::move(nearFallbackChunks);
        m_selectionDirty = true;
    }
    if (m_selectionDirty) rebuildSelection();
    if (jumped && !m_desired.empty()) {
        // Put one far silhouette tile into the bounded worker window first;
        // the normal round-robin then resumes its near-to-far work per level.
        m_nextRequestLevel = std::max_element(
            m_desired.begin(), m_desired.end(),
            [](const Request& a, const Request& b) {
                return a.key.level < b.key.level;
            })->key.level;
    }
    enqueueRequests();
    // Exact extraction is a refinement lane. Fill only capacity left after
    // missing/dirty LOD requests so exploration cannot permanently starve the
    // moving far-terrain frontier, especially on the one-worker preset.
    observeExactChunks(activeChunks);
}

void LodTerrainSystem::invalidateTilesForChunk(int cx, int cz) {
    for (auto& [key, tile] : m_tiles) {
        const bool exactNeighborDependency = key.level == 0 &&
            std::abs(key.x - cx) <= 1 && std::abs(key.z - cz) <= 1;
        const int chunksPerSide = 1 << key.level;
        const int minimumX = key.x * chunksPerSide;
        const int minimumZ = key.z * chunksPerSide;
        const bool containsChunk = cx >= minimumX &&
            cx < minimumX + chunksPerSide && cz >= minimumZ &&
            cz < minimumZ + chunksPerSide;
        if (!containsChunk && !exactNeighborDependency) continue;
        // Stale-while-revalidate: never punch a visible hole while the exact
        // replacement is generated and waiting for its GPU upload budget.
        tile->dirty = true;
    }
}

void LodTerrainSystem::processCompleted(IGameRenderer* renderer) {
    if (renderer) m_renderer = renderer;
    const LodWorkBudget budget = lodWorkBudget(m_settings.aggressiveness);
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double, std::milli>(budget.completionMs));
    int uploads = 0;
    size_t uploadBytes = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        ExactCompletion exact;
        bool hasExact = false;
        Completion completion;
        bool hasCompletion = false;
        {
            std::lock_guard lock(m_completionMutex);
            if (!m_exactCompletions.empty()) {
                exact = std::move(m_exactCompletions.front());
                m_exactCompletions.pop_front();
                hasExact = true;
            } else if (!m_completions.empty()) {
                completion = std::move(m_completions.front());
                m_completions.pop_front();
                hasCompletion = true;
            }
        }
        if (!hasExact && !hasCompletion) break;
        if (hasExact) {
            const uint64_t packed = packedChunkKey(exact.cx, exact.cz);
            const auto revision = m_exactRevisions.find(packed);
            if (revision == m_exactRevisions.end() ||
                revision->second != exact.revision) continue;
            if (!exact.persisted) {
                m_exactRevisions.erase(revision);
                continue;
            }
            // Selection epochs change on movement and settings edits, but an
            // exact chunk remains valid for the same generator and revision.
            m_exactColumnsByX[exact.cx].insert(exact.cz);
            invalidateTilesForChunk(exact.cx, exact.cz);
            continue;
        }
        auto found = m_tiles.find(completion.key);
        if (found == m_tiles.end()) continue;
        Tile& tile = *found->second;
        tile.queued = false;
        if (!completion.success) {
            tile.dirty = true;
            continue;
        }
        if (completion.epoch != m_epoch) {
            tile.dirty = true;
            continue;
        }
        m_cpuBytes += completion.data.memoryBytes() + completion.mesh.uploadBytes();
        tile.resident = true;
        if (tile.mesh.gpuReady || !tile.mesh.empty()) {
            if (tile.pendingData) {
                m_cpuBytes -= std::min(m_cpuBytes,
                    tile.pendingData->memoryBytes() + tile.pendingMesh->uploadBytes());
            }
            tile.pendingData.emplace(std::move(completion.data));
            tile.pendingMesh.emplace(std::move(completion.mesh));
        } else {
            tile.data = std::move(completion.data);
            tile.mesh = std::move(completion.mesh);
        }
    }
    if (renderer) {
        for (const Request& request : m_desired) {
            if (uploads >= budget.uploadsPerFrame) break;
            const auto found = m_tiles.find(request.key);
            if (found == m_tiles.end()) continue;
            Tile& tile = *found->second;
            ChunkMesh* candidate = tile.pendingMesh
                ? &*tile.pendingMesh : &tile.mesh;
            if (candidate->empty() || (!tile.pendingMesh && candidate->gpuReady))
                continue;
            const size_t bytes = candidate->uploadBytes();
            if (uploads > 0 && uploadBytes + bytes > budget.uploadBytesPerFrame)
                continue;
            if (tile.pendingMesh) {
                const size_t oldCpuBytes = tile.data.memoryBytes() +
                    tile.mesh.uploadBytes();
                if (tile.mesh.gpuReady) renderer->releaseChunkMesh(tile.mesh);
                m_gpuBytes -= std::min(m_gpuBytes, tile.gpuBytes);
                renderer->uploadChunkMesh(*tile.pendingMesh);
                tile.data = std::move(*tile.pendingData);
                tile.mesh = std::move(*tile.pendingMesh);
                tile.pendingData.reset();
                tile.pendingMesh.reset();
                m_cpuBytes -= std::min(m_cpuBytes, oldCpuBytes);
            } else {
                renderer->uploadChunkMesh(tile.mesh);
            }
            tile.gpuBytes = bytes;
            m_gpuBytes += bytes;
            uploadBytes += bytes;
            ++uploads;
            tile.mesh.vertices.clear();
            tile.mesh.vertices.shrink_to_fit();
            tile.mesh.indices.clear();
            tile.mesh.indices.shrink_to_fit();
            m_cpuBytes -= std::min(m_cpuBytes, bytes);
        }
    }
    // Selection bounds resident memory. Evicting a selected, unfinished tile
    // here would immediately request it again and can prevent coverage forever.
    enqueueRequests();
    rebuildSubmissions();
}

void LodTerrainSystem::rebuildSubmissions() {
    m_submissions.clear();
    const double originX = m_playerPosition.x;
    const double originZ = m_playerPosition.z;
    std::unordered_map<LodTileKey, const Request*, LodTileKeyHash> requests;
    requests.reserve(m_desired.size());
    for (const Request& request : m_desired)
        requests.emplace(request.key, &request);
    auto ready = [this](const LodTileKey& key) {
        const auto found = m_tiles.find(key);
        return found != m_tiles.end() && found->second->resident &&
            (found->second->mesh.gpuReady ||
             (found->second->mesh.indexCount == 0 &&
              !found->second->pendingMesh));
    };
    auto parentKey = [](const Request& request) {
        const int tileSize = LodTileData::SIDE << request.key.level;
        const int parentSize = static_cast<int>(request.maximumGrid);
        const int ratio = parentSize / tileSize;
        uint8_t level = request.key.level;
        for (int step = ratio; step > 1; step >>= 1) ++level;
        return LodTileKey{floorDiv(request.key.x, ratio),
                          floorDiv(request.key.z, ratio), level};
    };
    std::unordered_set<LodTileKey, LodTileKeyHash> missingFineParents;
    for (const Request& request : m_desired) {
        if (request.maximumGrid > 0.0f && !ready(request.key))
            missingFineParents.insert(parentKey(request));
    }
    for (const Request& request : m_desired) {
        const auto found = m_tiles.find(request.key);
        if (found == m_tiles.end() || !found->second->mesh.gpuReady) continue;
        float minimumDistance = request.minimumDistance;
        float maximumDistance = request.maximumDistance;
        if (missingFineParents.count(request.key) != 0)
            minimumDistance = 0.0f;
        if (request.maximumGrid > 0.0f) {
            const LodTileKey parent = parentKey(request);
            const auto parentRequest = requests.find(parent);
            if (parentRequest != requests.end() && !ready(parent))
                maximumDistance = parentRequest->second->maximumDistance;
        }
        const int cellSize = 1 << request.key.level;
        const int64_t worldX = static_cast<int64_t>(request.key.x) *
            LodTileData::SIDE * cellSize;
        const int64_t worldZ = static_cast<int64_t>(request.key.z) *
            LodTileData::SIDE * cellSize;
        const int tileSize = LodTileData::SIDE * cellSize;
        const int grid = static_cast<int>(std::max(
            request.minimumGrid, request.maximumGrid));
        const auto phase = [grid](int64_t coordinate) {
            return grid > 0 ? static_cast<float>(
                (coordinate % grid + grid) % grid) : 0.0f;
        };
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(
            static_cast<float>(static_cast<double>(worldX) - originX), 0.0f,
            static_cast<float>(static_cast<double>(worldZ) - originZ)));
        m_submissions.push_back({&found->second->mesh, model,
            glm::vec4(phase(worldX), request.minimumGrid,
                      phase(worldZ), request.maximumGrid),
            tileSize, minimumDistance, maximumDistance, request.distance2});
    }
}

void LodTerrainSystem::releaseGpuMeshes(bool retainCpuGeometry) {
    if (m_renderer) {
        for (auto& [key, tile] : m_tiles) {
            (void)key;
            if (tile->mesh.gpuReady) m_renderer->releaseChunkMesh(tile->mesh);
            tile->gpuBytes = 0;
        }
    } else {
        for (auto& [key, tile] : m_tiles) {
            (void)key;
            tile->mesh.abandonGpuResources();
            tile->gpuBytes = 0;
        }
    }
    m_gpuBytes = 0;
    m_submissions.clear();
    if (retainCpuGeometry) {
        m_cpuBytes = 0;
        const int spanLimit = lodVerticalSpanLimit(m_settings.precision);
        for (auto& [key, tile] : m_tiles) {
            if (tile->mesh.empty()) {
                LodExactNeighborTiles neighbors;
                const LodExactNeighborTiles* neighborPointer = nullptr;
                if (m_generator && key.level == 0 && isFullyExact(tile->data)) {
                    neighbors = readExactNeighbors(
                        m_cacheRoot, *m_generator, key.x, key.z);
                    neighborPointer = &neighbors;
                }
                LodNeighborEdges edges;
                const LodNeighborEdges* edgePointer = nullptr;
                if (m_generator && (key.level > 0 ||
                                    !isFullyExact(tile->data))) {
                    edges = sampleNeighborEdges(*m_generator, key);
                    edgePointer = &edges;
                }
                tile->mesh = buildLodTileMesh(tile->data, 1 << key.level,
                    spanLimit, neighborPointer, edgePointer, true);
            }
            m_cpuBytes += tile->data.memoryBytes() + tile->mesh.uploadBytes();
            if (tile->pendingData && tile->pendingMesh)
                m_cpuBytes += tile->pendingData->memoryBytes() +
                    tile->pendingMesh->uploadBytes();
        }
    }
}
