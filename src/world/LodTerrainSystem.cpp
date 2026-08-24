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
constexpr uint32_t CACHE_REVISION = 1;
constexpr size_t CPU_LIMIT = 64u * 1024u * 1024u;
constexpr size_t GPU_LIMIT = 128u * 1024u * 1024u;
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
        append<uint8_t>(payload, static_cast<uint8_t>(
            std::min<size_t>(column.spans.size(), 24)));
        for (size_t i = 0; i < std::min<size_t>(column.spans.size(), 24); ++i) {
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
        uint8_t exact = 0, count = 0;
        if (!read(payload, cursor, exact) || !read(payload, cursor, count) || count > 24)
            return false;
        column.exact = exact != 0;
        column.spans.clear();
        column.spans.reserve(count);
        for (uint8_t i = 0; i < count; ++i) {
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

int highestTop(const LodColumn& column) {
    int top = Config::WORLD_MIN_Y - 1;
    for (const LodSpan& span : column.spans) top = std::max(top, int(span.top));
    return top;
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
        for (int z = 0; z < LodTileData::SIDE; ++z) {
            for (int x = 0; x < LodTileData::SIDE; ++x) {
                const int wx = cx * Config::CHUNK_SIZE_X + x;
                const int wz = cz * Config::CHUNK_SIZE_Z + z;
                const int tx = floorDiv(wx - originX, cellSize);
                const int tz = floorDiv(wz - originZ, cellSize);
                if (tx < 0 || tx >= LodTileData::SIDE ||
                    tz < 0 || tz >= LodTileData::SIDE) continue;
                LodColumn& destination = tile.at(tx, tz);
                const LodColumn& source = exact.at(x, z);
                if (!destination.exact || highestTop(source) >= highestTop(destination))
                    destination = source;
            }
        }
    }
}

void appendVisibleIntervals(int bottom, int top, const LodColumn* neighbor,
                            std::vector<std::pair<int, int>>& result) {
    result.clear();
    result.push_back({bottom, top});
    if (!neighbor) return;
    for (const LodSpan& cover : neighbor->spans) {
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
              BlockId block) {
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
                static_cast<float>(face)});
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

LodTileData buildApproximateLodTile(const WorldGenerator& generator,
                                    const LodTileKey& key) {
    LodTileData tile;
    const int cellSize = 1 << key.level;
    const int originX = key.x * LodTileData::SIDE * cellSize;
    const int originZ = key.z * LodTileData::SIDE * cellSize;
    for (int z = 0; z < LodTileData::SIDE; ++z) {
        for (int x = 0; x < LodTileData::SIDE; ++x) {
            const int wx = originX + x * cellSize + cellSize / 2;
            const int wz = originZ + z * cellSize + cellSize / 2;
            LodColumn& column = tile.at(x, z);
            if (generator.isHeaven()) {
                for (const auto& island : generator.sampleHeavenLayers(wx, wz)) {
                    if (!island.present) continue;
                    column.spans.push_back({static_cast<int16_t>(island.bottom),
                        static_cast<int16_t>(island.top), heavenSurface(island.biome)});
                }
                continue;
            }
            const SurfaceColumn sample = generator.sampleTerrainColumn(wx, wz);
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
            const int depth = std::max(8, cellSize * 2);
            column.spans.push_back({static_cast<int16_t>(std::max(
                Config::WORLD_MIN_Y, sample.height - depth)),
                static_cast<int16_t>(sample.height), surface.top});
            if (sample.waterLevel > sample.height) {
                column.spans.push_back({static_cast<int16_t>(sample.height + 1),
                    static_cast<int16_t>(sample.waterLevel), BlockId::WATER});
            }
        }
    }
    return tile;
}

LodTileData extractExactLodChunk(const std::vector<uint8_t>& blocks,
                                 int maximumSpans) {
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
                const RenderLayer layer = getBlockProps(block).layer;
                const int bottom = y;
                BlockId representative = block;
                while (++y < Config::WORLD_MAX_Y) {
                    block = static_cast<BlockId>(blocks[index(x, y, z)]);
                    if (block == BlockId::AIR || getBlockProps(block).layer != layer) break;
                    representative = block;
                }
                column.spans.push_back({static_cast<int16_t>(bottom),
                    static_cast<int16_t>(y - 1), representative});
            }
            if (static_cast<int>(column.spans.size()) > maximumSpans) {
                std::stable_sort(column.spans.begin(), column.spans.end(),
                    [](const LodSpan& a, const LodSpan& b) {
                        return a.top > b.top;
                    });
                column.spans.resize(static_cast<size_t>(maximumSpans));
                std::sort(column.spans.begin(), column.spans.end(),
                    [](const LodSpan& a, const LodSpan& b) { return a.bottom < b.bottom; });
            }
        }
    }
    return tile;
}

ChunkMesh buildLodTileMesh(const LodTileData& data, int cellSize,
                           int maximumSpans) {
    ChunkMesh mesh;
    std::vector<unsigned int> opaque, translucent;
    std::vector<std::pair<int, int>> intervals;
    for (int z = 0; z < LodTileData::SIDE; ++z) {
        for (int x = 0; x < LodTileData::SIDE; ++x) {
            const LodColumn& column = data.at(x, z);
            const size_t first = column.spans.size() > static_cast<size_t>(maximumSpans)
                ? column.spans.size() - static_cast<size_t>(maximumSpans) : 0;
            for (size_t spanIndex = first; spanIndex < column.spans.size(); ++spanIndex) {
                const LodSpan& span = column.spans[spanIndex];
                const float x0 = static_cast<float>(x * cellSize);
                const float x1 = static_cast<float>((x + 1) * cellSize);
                const float z0 = static_cast<float>(z * cellSize);
                const float z1 = static_cast<float>((z + 1) * cellSize);
                emitFace(mesh, opaque, translucent, FaceDir::TOP,
                    {x0, span.top + 1.0f, z0}, {x1, span.top + 1.0f, z1}, span.block);
                // A single approximate ground span has no visible underside.
                // Exact columns and floating multi-span terrain still retain
                // bottoms for caves, overhangs, and Heaven islands.
                if (column.exact || column.spans.size() > 1) {
                    emitFace(mesh, opaque, translucent, FaceDir::BOTTOM,
                        {x0, static_cast<float>(span.bottom), z0},
                        {x1, static_cast<float>(span.bottom), z1}, span.block);
                }
                struct Side { int dx, dz; FaceDir face; };
                constexpr Side sides[] = {{0,-1,FaceDir::FRONT}, {0,1,FaceDir::BACK},
                    {1,0,FaceDir::RIGHT}, {-1,0,FaceDir::LEFT}};
                for (const Side& side : sides) {
                    const int nx = x + side.dx, nz = z + side.dz;
                    const LodColumn* neighbor = nx >= 0 && nx < LodTileData::SIDE &&
                        nz >= 0 && nz < LodTileData::SIDE ? &data.at(nx, nz) : nullptr;
                    appendVisibleIntervals(span.bottom, span.top, neighbor, intervals);
                    for (const auto& interval : intervals) {
                        glm::vec3 minimum{x0, static_cast<float>(interval.first), z0};
                        glm::vec3 maximum{x1, static_cast<float>(interval.second + 1), z1};
                        if (side.face == FaceDir::FRONT) maximum.z = minimum.z;
                        else if (side.face == FaceDir::BACK) minimum.z = maximum.z;
                        else if (side.face == FaceDir::RIGHT) minimum.x = maximum.x;
                        else maximum.x = minimum.x;
                        emitFace(mesh, opaque, translucent, side.face,
                                 minimum, maximum, span.block);
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
    m_cacheRoot = store ? store->worldDirectory() / "lod" / "r1" /
        ("d_" + std::to_string(static_cast<int>(m_generator
            ? m_generator->dimension() : DimensionId::Overworld)))
        : std::filesystem::path{};
    m_exactChunks.clear();
    if (store) scanExactCache();
}

void LodTerrainSystem::reset(WorldGenerator* generator) {
    if (m_threadPool) m_threadPool->waitIdle();
    releaseGpuMeshes();
    m_tiles.clear();
    m_desired.clear();
    m_submissions.clear();
    m_exactRevisions.clear();
    m_completions.clear();
    m_exactCompletions.clear();
    m_cpuBytes = m_gpuBytes = 0;
    m_generator = generator;
    if (m_saveStore && m_generator) {
        m_cacheRoot = m_saveStore->worldDirectory() / "lod" / "r1" /
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
            releaseGpuMeshes();
            m_tiles.clear();
            m_cpuBytes = m_gpuBytes = 0;
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
    m_exactChunks.clear();
    std::error_code error;
    const auto directory = m_cacheRoot / "exact";
    if (!std::filesystem::is_directory(directory, error)) return;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error || !entry.is_regular_file()) continue;
        int cx = 0, cz = 0;
        if (std::sscanf(entry.path().filename().string().c_str(), "c_%d_%d.lod", &cx, &cz) == 2)
            m_exactChunks.insert(packedChunkKey(cx, cz));
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
    for (uint64_t packed : m_exactChunks) {
        const int cx = static_cast<int>(static_cast<int32_t>(packed >> 32));
        const int cz = static_cast<int>(static_cast<int32_t>(packed & 0xffffffffu));
        if (cx >= minimumX && cx < maximumX && cz >= minimumZ && cz < maximumZ)
            result.push_back({cx, cz});
    }
    return result;
}

void LodTerrainSystem::rebuildSelection() {
    m_desired.clear();
    if (!m_settings.enabled || !m_generator) return;
    const int quality = lodHorizontalQuality(m_settings.precision);
    // Keep LOD underneath the outer two full-chunk rings. The near pass clears
    // depth and overwrites it, while an asynchronously missing near mesh still
    // has stable terrain behind it instead of revealing the sky.
    const float inner = static_cast<float>(std::max(1, m_nearDistanceChunks - 2) *
        Config::CHUNK_SIZE_X);
    const float outer = static_cast<float>(m_settings.distanceChunks * Config::CHUNK_SIZE_X);
    const float centerX = static_cast<float>(m_centerChunkX * Config::CHUNK_SIZE_X + 8);
    const float centerZ = static_cast<float>(m_centerChunkZ * Config::CHUNK_SIZE_Z + 8);
    // Level 12 reaches 4096-block cells, which is required for the 4096-chunk
    // hard limit at the low precision preset without expanding the outer ring.
    constexpr int maximumLevel = 12;
    for (int level = 0; level <= maximumLevel; ++level) {
        const int cellSize = 1 << level;
        const int tileSize = LodTileData::SIDE * cellSize;
        const float minimum = std::max(inner, level == 0 ? inner : cellSize * quality * 0.5f);
        const float maximum = std::min(outer,
            level == maximumLevel ? outer : static_cast<float>(cellSize * quality));
        if (maximum <= minimum) continue;
        const int minTileX = floorDiv(static_cast<int>(std::floor(centerX - maximum)), tileSize);
        const int maxTileX = floorDiv(static_cast<int>(std::floor(centerX + maximum)), tileSize);
        const int minTileZ = floorDiv(static_cast<int>(std::floor(centerZ - maximum)), tileSize);
        const int maxTileZ = floorDiv(static_cast<int>(std::floor(centerZ + maximum)), tileSize);
        for (int tz = minTileZ; tz <= maxTileZ; ++tz) {
            for (int tx = minTileX; tx <= maxTileX; ++tx) {
                const float tileCenterX = (tx + 0.5f) * tileSize;
                const float tileCenterZ = (tz + 0.5f) * tileSize;
                const float dx = tileCenterX - centerX;
                const float dz = tileCenterZ - centerZ;
                const float distance = std::sqrt(dx * dx + dz * dz);
                const float margin = tileSize * 0.72f;
                if (distance + margin < minimum || distance - margin > maximum) continue;
                const float shaderMinimum = minimum <= inner + 0.5f
                    ? 8.0f : minimum;
                m_desired.push_back({{tx, tz, static_cast<uint8_t>(level)},
                    shaderMinimum, maximum, dx * dx + dz * dz});
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
        const uint64_t epoch = m_epoch;
        WorldGenerator* generator = m_generator;
        const auto root = m_cacheRoot;
        ++m_tasksInFlight;
        m_threadPool->enqueuePriority([this, blocks = std::move(blocks), cx, cz,
                                      revision, epoch, generator, root]() mutable {
            LodTileData data = extractExactLodChunk(blocks, 24);
            writeTileFile(exactPath(root, cx, cz), {cx, cz, 0}, *generator, data);
            {
                std::lock_guard lock(m_completionMutex);
                m_exactCompletions.push_back({cx, cz, revision, epoch, std::move(data)});
            }
            --m_tasksInFlight;
        }, 1000);
        break;
    }
}

void LodTerrainSystem::enqueueRequests() {
    if (!m_threadPool || !m_generator || m_cacheRoot.empty()) return;
    const LodWorkBudget budget = lodWorkBudget(m_settings.aggressiveness);
    for (const Request& request : m_desired) {
        if (m_tasksInFlight.load() >= budget.maxInFlight) break;
        auto found = m_tiles.find(request.key);
        if (found != m_tiles.end() && (found->second->queued ||
                found->second->pendingMesh || (!found->second->dirty &&
                (found->second->mesh.gpuReady || !found->second->mesh.empty())))) {
            found->second->minimumDistance = request.minimumDistance;
            found->second->maximumDistance = request.maximumDistance;
            found->second->distance2 = request.distance2;
            continue;
        }
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
        m_threadPool->enqueuePriority([this, key, epoch, spanLimit, generator,
                                      root, exact]() {
            LodTileData data;
            if (!readTileFile(tilePath(root, key), key, *generator, data)) {
                data = buildApproximateLodTile(*generator, key);
                writeTileFile(tilePath(root, key), key, *generator, data);
            }
            overlayExactChunks(data, key, root, *generator, exact);
            ChunkMesh mesh = buildLodTileMesh(data, 1 << key.level, spanLimit);
            {
                std::lock_guard lock(m_completionMutex);
                m_completions.push_back({key, epoch, std::move(data), std::move(mesh)});
            }
            --m_tasksInFlight;
        }, 100 - static_cast<int>(std::min(request.distance2, 1000000.0f)));
    }
}

void LodTerrainSystem::update(const glm::dvec3& playerPosition,
                              int nearDistanceChunks,
                              const std::vector<Chunk*>& activeChunks) {
    if (!m_settings.enabled || !m_generator) return;
    m_playerPosition = playerPosition;
    const int cx = static_cast<int>(std::floor(playerPosition.x / Config::CHUNK_SIZE_X));
    const int cz = static_cast<int>(std::floor(playerPosition.z / Config::CHUNK_SIZE_Z));
    if (cx != m_centerChunkX || cz != m_centerChunkZ ||
        nearDistanceChunks != m_nearDistanceChunks) {
        if (std::abs(cx - m_centerChunkX) > 8 || std::abs(cz - m_centerChunkZ) > 8)
            ++m_epoch;
        m_centerChunkX = cx;
        m_centerChunkZ = cz;
        m_nearDistanceChunks = nearDistanceChunks;
        m_selectionDirty = true;
    }
    if (m_selectionDirty) rebuildSelection();
    observeExactChunks(activeChunks);
    enqueueRequests();
}

void LodTerrainSystem::invalidateTilesForChunk(int cx, int cz) {
    for (auto& [key, tile] : m_tiles) {
        const int chunksPerSide = 1 << key.level;
        const int minimumX = key.x * chunksPerSide;
        const int minimumZ = key.z * chunksPerSide;
        if (cx < minimumX || cx >= minimumX + chunksPerSide ||
            cz < minimumZ || cz >= minimumZ + chunksPerSide) continue;
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
            if (exact.epoch != m_epoch) continue;
            m_exactChunks.insert(packedChunkKey(exact.cx, exact.cz));
            invalidateTilesForChunk(exact.cx, exact.cz);
            continue;
        }
        auto found = m_tiles.find(completion.key);
        if (found == m_tiles.end()) continue;
        Tile& tile = *found->second;
        tile.queued = false;
        if (completion.epoch != m_epoch) {
            tile.dirty = true;
            continue;
        }
        m_cpuBytes += completion.data.memoryBytes() + completion.mesh.uploadBytes();
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
            const size_t replacedGpuBytes = tile.pendingMesh ? tile.gpuBytes : 0;
            if (uploadBytes + bytes > budget.uploadBytesPerFrame ||
                m_gpuBytes - std::min(m_gpuBytes, replacedGpuBytes) + bytes > GPU_LIMIT)
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
    if (m_cpuBytes > CPU_LIMIT) {
        for (auto it = m_tiles.begin(); it != m_tiles.end() && m_cpuBytes > CPU_LIMIT;) {
            if (it->second->mesh.gpuReady || it->second->queued ||
                it->second->pendingMesh) { ++it; continue; }
            m_cpuBytes -= std::min(m_cpuBytes, it->second->data.memoryBytes() +
                it->second->mesh.uploadBytes());
            it = m_tiles.erase(it);
        }
    }
    enqueueRequests();
    rebuildSubmissions();
}

void LodTerrainSystem::rebuildSubmissions() {
    m_submissions.clear();
    const double originX = m_playerPosition.x;
    const double originZ = m_playerPosition.z;
    for (const Request& request : m_desired) {
        const auto found = m_tiles.find(request.key);
        if (found == m_tiles.end() || !found->second->mesh.gpuReady) continue;
        const int cellSize = 1 << request.key.level;
        const double worldX = static_cast<double>(request.key.x * LodTileData::SIDE * cellSize);
        const double worldZ = static_cast<double>(request.key.z * LodTileData::SIDE * cellSize);
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(
            static_cast<float>(worldX - originX), 0.0f,
            static_cast<float>(worldZ - originZ)));
        m_submissions.push_back({&found->second->mesh, model,
            glm::vec2(static_cast<float>(worldX), static_cast<float>(worldZ)),
            request.minimumDistance, request.maximumDistance, request.distance2});
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
            if (tile->mesh.empty())
                tile->mesh = buildLodTileMesh(tile->data, 1 << key.level, spanLimit);
            m_cpuBytes += tile->data.memoryBytes() + tile->mesh.uploadBytes();
        }
    }
}
