#pragma once

#include "Config.h"
#include "renderer/VisualQuality.h"
#include "renderer/VoxelGiMaterial.h"
#include "world/Block.h"
#include "world/Chunk.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

enum class VoxelGiAvailability : uint8_t {
    Available,
    Disabled,
    UnsupportedFormat,
    UnsupportedLimits,
    AllocationFailed
};

struct VoxelGiStatus {
    VoxelGiAvailability availability = VoxelGiAvailability::Disabled;
    bool requested = false;
    bool active = false;
    size_t pendingSlices = 0;
    size_t sourceChunks = 0;
    float validVoxelFraction = 0.0f;
    size_t uploadedBytes = 0;
    size_t injectedVoxels = 0;
    size_t pendingCoarseChunks = 0;
    size_t coarseVoxels = 0;
    double cpuCacheMs = 0.0;
    double cpuCoarseMs = 0.0;
    double cpuPackMs = 0.0;
    bool gpuTimingAvailable = false;
    double gpuInjectionMs = 0.0;
    double gpuScreenMs = 0.0;
    double gpuCompositeMs = 0.0;
    double gpuFrameMs = 0.0;
    uint64_t gpuSampleId = 0;
};

struct VoxelGiConfigChange {
    bool resetCache = false;
    bool resetHistory = false;
    bool rebuildResources = false;
};

inline VoxelGiConfigChange voxelGiConfigChange(
        const VoxelGiConfig& before, const VoxelGiConfig& after) {
    const bool resources = before.enabled != after.enabled ||
        before.clipmapResolution != after.clipmapResolution ||
        before.clipmapLevels != after.clipmapLevels;
    const bool spatial = resources || before.distance != after.distance;
    return {spatial, spatial || before.strength != after.strength ||
        before.coneCount != after.coneCount || before.coneSteps != after.coneSteps ||
        before.reflectedGain != after.reflectedGain || before.emissionGain != after.emissionGain,
        resources};
}

// The setting is a 60 Hz reference weight, not a weight per swapchain reuse.
inline float voxelGiHistoryWeight(float referenceWeight, double elapsedSeconds) {
    if (!(elapsedSeconds > 0.0) || elapsedSeconds > 0.5) return 0.0f;
    return static_cast<float>(std::pow(std::clamp(referenceWeight, 0.0f, 0.95f),
                                      elapsedSeconds * 60.0));
}

struct VoxelGiRegion {
    glm::ivec3 offset{0};
    glm::ivec3 extent{0};
    size_t voxelCount() const {
        return static_cast<size_t>(extent.x) * extent.y * extent.z;
    }
};

// A union of dirty planes partitioned into disjoint boxes. Z owns crossings,
// then Y, then X: neither transfers nor compute dispatches overlap.
class VoxelGiDirtyRegions {
public:
    void mark(int axis, int layer) { m_planes[axis][layer] = true; }
    void markFull() { m_full = true; }
    void clear() { *this = {}; }
    std::vector<VoxelGiRegion> regions(int resolution) const {
        if (m_full) return {{{0, 0, 0}, glm::ivec3(resolution)}};
        const auto runs = [&](int axis, bool selected) {
            std::vector<std::pair<int, int>> result;
            for (int i = 0; i < resolution;) {
                if (m_planes[axis][i] != selected) { ++i; continue; }
                const int first = i++;
                while (i < resolution && m_planes[axis][i] == selected) ++i;
                result.emplace_back(first, i - first);
            }
            return result;
        };
        std::vector<VoxelGiRegion> result;
        for (const auto& z : runs(2, true))
            result.push_back({{0, 0, z.first}, {resolution, resolution, z.second}});
        for (const auto& z : runs(2, false)) {
            for (const auto& y : runs(1, true))
                result.push_back({{0, y.first, z.first}, {resolution, y.second, z.second}});
            for (const auto& y : runs(1, false))
                for (const auto& x : runs(0, true)) {
                    result.push_back({{x.first, y.first, z.first},
                                      {x.second, y.second, z.second}});
                    // Highly fragmented edits are cheaper as one full transfer.
                    if (result.size() > 128)
                        return {{{0, 0, 0}, glm::ivec3(resolution)}};
                }
        }
        if (result.size() > 128) return {{{0, 0, 0}, glm::ivec3(resolution)}};
        return result;
    }
private:
    std::array<std::array<bool, 64>, 3> m_planes{};
    bool m_full = false;
};

inline void packVoxelGiRegion(const std::vector<uint8_t>& source, int resolution,
                             const VoxelGiRegion& region, std::vector<uint8_t>& output) {
    const size_t first = output.size();
    output.resize(first + region.voxelCount() * 4);
    size_t destination = first;
    for (int z = 0; z < region.extent.z; ++z)
        for (int y = 0; y < region.extent.y; ++y) {
            const size_t offset = static_cast<size_t>(region.offset.x + resolution *
                (region.offset.y + y + resolution * (region.offset.z + z))) * 4;
            const size_t bytes = static_cast<size_t>(region.extent.x) * 4;
            std::copy_n(source.data() + offset, bytes, output.data() + destination);
            destination += bytes;
        }
}

struct VoxelGiDeviceSupport {
    uint32_t maximum3dDimension = 0;
    uint32_t sampledImagesPerStage = 0;
    uint32_t sampledImagesPerSet = 0;
    uint32_t storageImagesPerStage = 0;
    uint32_t storageImagesPerSet = 0;
    bool attributeSampling = false;
    bool irradianceSampling = false;
    bool irradianceStorage = false;
    uint32_t storageBuffersPerStage = 0;
    uint32_t storageBuffersPerSet = 0;
    uint64_t maximumStorageBufferRange = 0;
};

inline VoxelGiAvailability voxelGiAvailability(
        const VoxelGiDeviceSupport& support) {
    if (support.maximum3dDimension < 64 ||
        support.sampledImagesPerStage < 8 || support.sampledImagesPerSet < 8 ||
        support.storageImagesPerStage < 1 || support.storageImagesPerSet < 1 ||
        support.storageBuffersPerStage < 4 || support.storageBuffersPerSet < 4 ||
        support.maximumStorageBufferRange < 64u * 64u * 64u * sizeof(VoxelGiAux))
        return VoxelGiAvailability::UnsupportedLimits;
    if (!support.attributeSampling || !support.irradianceSampling ||
        !support.irradianceStorage)
        return VoxelGiAvailability::UnsupportedFormat;
    return VoxelGiAvailability::Available;
}

struct VoxelGiPacked {
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    uint8_t opacity = 0;
    uint8_t skyLight = 0;
    uint8_t blockLight = 0;
    uint8_t emission = 0;
    uint8_t valid = 0;
    VoxelGiAux aux{};
};

inline uint8_t voxelGiBlockOpacity(BlockId id) {
    if (id == BlockId::AIR) return 0;
    const auto& properties = getBlockProps(id);
    float opacity = properties.layer == RenderLayer::Opaque ? 1.0f :
        properties.layer == RenderLayer::Cutout ? 0.42f :
        isFluid(id) ? 0.16f : 0.28f;
    if (properties.shape == RenderShape::Cross ||
        properties.shape == RenderShape::CeilingCross) opacity *= 0.25f;
    if (properties.shape == RenderShape::SnowLayer) opacity *= 0.18f;
    if (properties.shape == RenderShape::Slab) opacity *= 0.5f;
    if (properties.shape == RenderShape::Spike) opacity *= 0.35f;
    return voxelGiByte(opacity);
}

inline VoxelGiPacked packVoxelGi(BlockId id, uint8_t packedLight,
                                  const VoxelGiMaterial* material = nullptr) {
    VoxelGiPacked result;
    result.skyLight = static_cast<uint8_t>((packedLight >> 4) * 17u);
    result.blockLight = static_cast<uint8_t>((packedLight & 0x0fu) * 17u);
    result.emission = static_cast<uint8_t>(getLightEmission(id) * 17u);
    result.valid = 255;
    if (id == BlockId::AIR) return result;
    const BlockProperties& properties = getBlockProps(id);
    glm::vec3 color(0.0f);
    for (int f = 0; f < 6; ++f) {
        if (material) color += material->reflectance[f] / 6.0f;
        else for (int c = 0; c < 3; ++c)
            color[c] += std::min(0.9f, voxelGiSrgbToLinear(
                std::clamp(properties.color[c], 0.0f, 1.0f))) / 6.0f;
        setVoxelGiExposure(result.aux, f, 255);
    }
    result.aux.emission = voxelGiRgb(material ? material->emission :
        color / std::max({color.r, color.g, color.b, 0.0001f}) * (result.emission / 255.0f));
    result.red = static_cast<uint8_t>(std::lround(color.r * 255.0f));
    result.green = static_cast<uint8_t>(std::lround(color.g * 255.0f));
    result.blue = static_cast<uint8_t>(std::lround(color.b * 255.0f));
    result.opacity = voxelGiBlockOpacity(id);
    if (result.opacity) { result.aux.occupiedLo = 1; updateVoxelGiCoverage(result.aux, 1); }
    return result;
}

inline int voxelGiFloorDiv(int value, int divisor) {
    int quotient = value / divisor;
    const int remainder = value % divisor;
    if (remainder != 0 && ((remainder < 0) != (divisor < 0))) --quotient;
    return quotient;
}

inline int voxelGiPositiveMod(int value, int divisor) {
    const int remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

inline int voxelGiCellSize(const VoxelGiConfig& config, int level) {
    const int clampedLevel = std::clamp(level, 0, config.clipmapLevels - 1);
    const float requiredCoarse = std::max(
        1.0f, config.distance * 2.0f /
            static_cast<float>(config.clipmapResolution));
    int coarse = 1;
    while (static_cast<float>(coarse) < requiredCoarse) coarse *= 2;
    return std::max(1, coarse >> (config.clipmapLevels - clampedLevel - 1));
}

struct VoxelGiSliceUpdate {
    int level = 0;
    int axis = 2;
    int layer = 0;
    // Empty voxels invalidate an exposed plane before bounded resampling.
    std::vector<VoxelGiPacked> voxels;
};

struct VoxelGiLevelMapping {
    glm::ivec3 minimumCell{0};
    int cellSize = 1;
};

// CPU-side source cache and toroidal clipmap scheduler. It owns snapshots only
// for chunks accepted within the current bounded frame budget. Vulkan consumes
// completed axis-aligned planes immediately and never retains Chunk pointers.
class VoxelGiSceneCache {
public:
    void setMaterials(const VoxelGiMaterialTable& materials) {
        m_materials = materials; m_hasMaterials = true; reset();
    }
    void configure(const VoxelGiConfig& config) {
        const bool changed = m_config.clipmapResolution != config.clipmapResolution ||
            m_config.clipmapLevels != config.clipmapLevels ||
            std::abs(m_config.distance - config.distance) > 0.5f;
        m_config = config;
        if (changed) reset();
    }

    void beginFrame(const glm::dvec3& camera, uint64_t sceneId) {
        ++m_frame;
        m_chunkCopiesRemaining = std::max(1, m_config.sourceCopiesPerFrame);
        m_coarseVoxelsBuilt = 0;
        if (sceneId != m_sceneId) {
            reset();
            m_sceneId = sceneId;
        }
        m_camera = camera;
        ensureLevels();
        for (int level = 0; level < m_config.clipmapLevels; ++level)
            updateLevelOrigin(level);
    }

    bool submit(const Chunk& chunk) {
        if (!intersectsClipmaps(chunk.cx, chunk.cz)) return false;
        const int64_t key = chunkKey(chunk.cx, chunk.cz);
        auto found = m_chunks.find(key);
        if (found != m_chunks.end()) found->second.lastSeen = m_frame;
        const uint64_t revision = chunk.dataRevision();
        if (found != m_chunks.end() && found->second.revision == revision)
            return false;
        if (m_chunkCopiesRemaining <= 0) return false;
        CachedChunk snapshot;
        snapshot.cx = chunk.cx;
        snapshot.cz = chunk.cz;
        snapshot.revision = revision;
        snapshot.lastSeen = m_frame;
        chunk.copyRawState(snapshot.blocks, snapshot.light);
        m_chunks[key] = std::move(snapshot);
        ++m_contentRevision;
        --m_chunkCopiesRemaining;
        markSourceNeighborhood(chunk.cx, chunk.cz);
        return true;
    }

    void endFrame() {
        for (auto it = m_chunks.begin(); it != m_chunks.end();) {
            if (m_frame - it->second.lastSeen > 2 ||
                !intersectsClipmaps(it->second.cx, it->second.cz)) {
                markSourceNeighborhood(it->second.cx, it->second.cz);
                it = m_chunks.erase(it);
                ++m_contentRevision;
            } else ++it;
        }
        size_t budget = m_config.coarseVoxelBudget;
        while (budget >= 8 && !m_coarseDirty.empty()) {
            // Resume the nearest source first, independent of hash iteration.
            auto next = m_coarseDirty.begin();
            double nearest = std::numeric_limits<double>::max();
            for (auto it = m_coarseDirty.begin(); it != m_coarseDirty.end(); ++it) {
                const auto found = m_chunks.find(*it);
                if (found == m_chunks.end()) continue;
                const auto& c = found->second;
                const double dx = c.cx * 16.0 + 8.0 - m_camera.x;
                const double dz = c.cz * 16.0 + 8.0 - m_camera.z;
                const double distance = dx * dx + dz * dz;
                if (distance < nearest || (distance == nearest && *it < *next)) {
                    nearest = distance; next = it;
                }
            }
            const auto found = m_chunks.find(*next);
            if (found == m_chunks.end()) { m_coarseDirty.erase(next); continue; }
            auto& c = found->second;
            if (!buildCoarseCells(c, budget)) break;
            c.coarse = std::move(c.buildingCoarse);
            c.coarseMip = 0; c.coarseCursor = 0;
            markChunkDirty(c.cx, c.cz);
            ++m_contentRevision;
            m_coarseDirty.erase(next);
        }
    }

    std::vector<VoxelGiSliceUpdate> takeUpdates(int maximum) {
        std::vector<VoxelGiSliceUpdate> result = std::move(m_invalidations);
        m_invalidations.clear();
        maximum = std::max(0, maximum);
        int built = 0;
        while (!m_dirty.empty() && built < maximum) {
            const DirtySlice dirty = m_dirty.front();
            m_dirty.pop_front();
            m_dirtySet.erase(sliceKey(dirty.level, dirty.axis, dirty.worldCell));
            const Level& state = m_levels[static_cast<size_t>(dirty.level)];
            if (dirty.worldCell < state.minimumCell[dirty.axis] ||
                dirty.worldCell >= state.minimumCell[dirty.axis] +
                    m_config.clipmapResolution) continue;
            result.push_back(buildSlice(dirty.level, dirty.axis, dirty.worldCell));
            ++built;
        }
        return result;
    }

    void recycleUpdates(std::vector<VoxelGiSliceUpdate>& updates) {
        for (auto& update : updates)
            if (!update.voxels.empty() && m_sliceBuffers.size() <
                    static_cast<size_t>(m_config.updateSlicesPerFrame))
                m_sliceBuffers.push_back(std::move(update.voxels));
        updates.clear();
    }

    size_t pendingSlices() const { return m_dirty.size(); }
    size_t pendingCoarseChunks() const { return m_coarseDirty.size(); }
    size_t coarseVoxelsBuilt() const { return m_coarseVoxelsBuilt; }
    size_t cachedChunks() const { return m_chunks.size(); }
    uint64_t contentRevision() const { return m_contentRevision; }
    const VoxelGiConfig& config() const { return m_config; }
    VoxelGiLevelMapping levelMapping(int level) const {
        if (level < 0 || level >= static_cast<int>(m_levels.size())) return {};
        return {m_levels[static_cast<size_t>(level)].minimumCell,
                voxelGiCellSize(m_config, level)};
    }

    void reset() {
        m_chunks.clear();
        m_coarseDirty.clear();
        m_levels.clear();
        m_dirty.clear();
        m_dirtySet.clear();
        m_invalidations.clear();
        m_sliceBuffers.clear();
        m_sceneId = std::numeric_limits<uint64_t>::max();
    }

private:
    struct CachedChunk {
        int cx = 0;
        int cz = 0;
        uint64_t revision = 0;
        uint64_t lastSeen = 0;
        std::vector<uint16_t> blocks;
        std::vector<uint8_t> light;
        std::array<std::vector<VoxelGiPacked>, 4> coarse;
        std::array<std::vector<VoxelGiPacked>, 4> buildingCoarse;
        int coarseMip = 0;
        size_t coarseCursor = 0;
    };
    struct Level {
        glm::ivec3 minimumCell{0};
        bool initialized = false;
    };
    struct DirtySlice { int level = 0; int axis = 2; int worldCell = 0; };

    VoxelGiMaterialTable m_materials{};
    bool m_hasMaterials = false;
    std::unordered_set<int64_t> m_coarseDirty;
    VoxelGiConfig m_config{};
    glm::dvec3 m_camera{0.0};
    uint64_t m_sceneId = std::numeric_limits<uint64_t>::max();
    uint64_t m_frame = 0;
    uint64_t m_contentRevision = 0;
    int m_chunkCopiesRemaining = 0;
    size_t m_coarseVoxelsBuilt = 0;
    std::unordered_map<int64_t, CachedChunk> m_chunks;
    std::vector<Level> m_levels;
    std::deque<DirtySlice> m_dirty;
    std::unordered_set<uint64_t> m_dirtySet;
    std::vector<VoxelGiSliceUpdate> m_invalidations;
    std::vector<std::vector<VoxelGiPacked>> m_sliceBuffers;

    static int64_t chunkKey(int cx, int cz) {
        return static_cast<int64_t>(
            (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
            static_cast<uint32_t>(cz));
    }

    void ensureLevels() {
        if (static_cast<int>(m_levels.size()) != m_config.clipmapLevels)
            m_levels.assign(static_cast<size_t>(m_config.clipmapLevels), {});
    }

    uint64_t sliceKey(int level, int axis, int worldCell) const {
        return (static_cast<uint64_t>(level * 3 + axis) << 32) |
            static_cast<uint32_t>(worldCell);
    }

    bool intersectsLevel(int cx, int cz, int level) const {
        const auto mapping = levelMapping(level);
        const int minX = mapping.minimumCell.x * mapping.cellSize;
        const int minZ = mapping.minimumCell.z * mapping.cellSize;
        const int extent = m_config.clipmapResolution * mapping.cellSize;
        return cx * Config::CHUNK_SIZE_X < minX + extent &&
            (cx + 1) * Config::CHUNK_SIZE_X > minX &&
            cz * Config::CHUNK_SIZE_Z < minZ + extent &&
            (cz + 1) * Config::CHUNK_SIZE_Z > minZ;
    }

    bool intersectsClipmaps(int cx, int cz) const {
        for (int level = 0; level < static_cast<int>(m_levels.size()); ++level)
            if (intersectsLevel(cx, cz, level)) return true;
        return false;
    }

    void queueFullLevel(int level) {
        const Level& state = m_levels[static_cast<size_t>(level)];
        const int half = m_config.clipmapResolution / 2;
        for (int offset = 0; offset < m_config.clipmapResolution; ++offset) {
            const int z = offset % 2 ? half - (offset + 1) / 2 : half + offset / 2;
            invalidatePlane(level, 2, state.minimumCell.z + z);
        }
    }

    void queueSlice(int level, int axis, int worldCell) {
        if (m_dirtySet.insert(sliceKey(level, axis, worldCell)).second)
            m_dirty.push_back({level, axis, worldCell});
    }

    void invalidatePlane(int level, int axis, int worldCell) {
        VoxelGiSliceUpdate update;
        update.level = level;
        update.axis = axis;
        update.layer = voxelGiPositiveMod(worldCell, m_config.clipmapResolution);
        m_invalidations.push_back(std::move(update));
        queueSlice(level, axis, worldCell);
    }

    void updateLevelOrigin(int levelIndex) {
        Level& state = m_levels[static_cast<size_t>(levelIndex)];
        const int cellSize = voxelGiCellSize(m_config, levelIndex);
        const int resolution = m_config.clipmapResolution;
        const int half = resolution / 2;
        const glm::ivec3 next(
            voxelGiFloorDiv(static_cast<int>(std::floor(m_camera.x)), cellSize) - half,
            voxelGiFloorDiv(static_cast<int>(std::floor(m_camera.y)), cellSize) - half,
            voxelGiFloorDiv(static_cast<int>(std::floor(m_camera.z)), cellSize) - half);
        if (!state.initialized) {
            state.minimumCell = next;
            state.initialized = true;
            queueFullLevel(levelIndex);
            return;
        }
        const glm::ivec3 previous = state.minimumCell;
        state.minimumCell = next;
        // Discard planes that have left the volume; rapid movement cannot grow
        // the queue without bound or upload obsolete ring coordinates.
        for (auto it = m_dirty.begin(); it != m_dirty.end();) {
            if (it->level == levelIndex &&
                (it->worldCell < next[it->axis] ||
                 it->worldCell >= next[it->axis] + resolution)) {
                m_dirtySet.erase(sliceKey(it->level, it->axis, it->worldCell));
                it = m_dirty.erase(it);
            } else ++it;
        }
        const glm::ivec3 delta = next - previous;
        if (glm::any(glm::greaterThanEqual(glm::abs(delta), glm::ivec3(resolution)))) {
            for (int z = 0; z < resolution; ++z)
                invalidatePlane(levelIndex, 2, next.z + z);
            return;
        }
        for (int axis = 0; axis < 3; ++axis) {
            const int first = delta[axis] > 0 ? resolution - delta[axis] : 0;
            const int last = delta[axis] > 0 ? resolution : -delta[axis];
            for (int cell = first; cell < last; ++cell)
                invalidatePlane(levelIndex, axis, next[axis] + cell);
        }
    }

    void markChunkDirty(int cx, int cz) {
        const int worldMinZ = cz * Config::CHUNK_SIZE_Z;
        const int worldMaxZ = worldMinZ + Config::CHUNK_SIZE_Z - 1;
        for (int level = 0; level < static_cast<int>(m_levels.size()); ++level) {
            if (!intersectsLevel(cx, cz, level)) continue;
            const int cellSize = voxelGiCellSize(m_config, level);
            const int minCell = voxelGiFloorDiv(worldMinZ, cellSize);
            const int maxCell = voxelGiFloorDiv(worldMaxZ, cellSize);
            const Level& state = m_levels[static_cast<size_t>(level)];
            for (int z = std::max(minCell, state.minimumCell.z);
                 z <= std::min(maxCell, state.minimumCell.z +
                    m_config.clipmapResolution - 1); ++z)
                queueSlice(level, 2, z);
        }
    }

    void markSourceNeighborhood(int cx, int cz) {
        for (const auto offset : {glm::ivec2(0), glm::ivec2(-1,0), glm::ivec2(1,0),
                                  glm::ivec2(0,-1), glm::ivec2(0,1)}) {
            const int x = cx + offset.x, z = cz + offset.y;
            if (m_chunks.count(chunkKey(x,z)) || (x == cx && z == cz)) {
                const int64_t key = chunkKey(x,z);
                m_coarseDirty.insert(key);
                auto found = m_chunks.find(key);
                if (found != m_chunks.end()) {
                    found->second.buildingCoarse = {};
                    found->second.coarseMip = 0;
                    found->second.coarseCursor = 0;
                }
                markChunkDirty(x,z);
            }
        }
    }

    VoxelGiPacked sourceVoxel(const CachedChunk& chunk, int x, int y, int z) const {
        const size_t index = static_cast<size_t>(x + 16 * (z + 16 * y));
        const auto id = static_cast<BlockId>(chunk.blocks[index]);
        auto result = packVoxelGi(id, chunk.light[index], m_hasMaterials ?
            &m_materials[static_cast<size_t>(id)] : nullptr);
        if (!result.opacity) return result;
        const std::array<glm::ivec3,6> normals{{{0,1,0},{0,-1,0},{0,0,-1},
                                               {0,0,1},{1,0,0},{-1,0,0}}};
        glm::vec3 color(0.0f);
        float total = 0.0f;
        for (int f = 0; f < 6; ++f) {
            const auto p = glm::ivec3(x,y,z) + normals[f];
            uint8_t exposure = 0;
            if (p.y >= 0 && p.y < Config::CHUNK_SIZE_Y) {
                const int nx = voxelGiFloorDiv(p.x,16), nz = voxelGiFloorDiv(p.z,16);
                const CachedChunk* neighbor = &chunk;
                if (nx || nz) {
                    const auto it = m_chunks.find(chunkKey(chunk.cx+nx,chunk.cz+nz));
                    neighbor = it == m_chunks.end() ? nullptr : &it->second;
                }
                if (neighbor) {
                    const size_t i = static_cast<size_t>(voxelGiPositiveMod(p.x,16) +
                        16 * (voxelGiPositiveMod(p.z,16) + 16 * p.y));
                    exposure = 255 - voxelGiBlockOpacity(static_cast<BlockId>(neighbor->blocks[i]));
                }
            }
            setVoxelGiExposure(result.aux, f, exposure);
            if (m_hasMaterials) color += m_materials[static_cast<size_t>(id)].reflectance[f] * float(exposure);
            total += exposure;
        }
        if (m_hasMaterials && total > 0) {
            color /= total;
            result.red = voxelGiByte(color.r); result.green = voxelGiByte(color.g); result.blue = voxelGiByte(color.b);
        }
        return result;
    }

    bool buildCoarseCells(CachedChunk& chunk, size_t& budget) {
        const int maximumCellSize = voxelGiCellSize(m_config, m_config.clipmapLevels - 1);
        for (int mip = chunk.coarseMip, size = 2 << mip;
             mip < 4 && size <= maximumCellSize; ++mip, size *= 2) {
            const int width = Config::CHUNK_SIZE_X / size;
            const int height = Config::CHUNK_SIZE_Y / size;
            auto& output = chunk.buildingCoarse[static_cast<size_t>(mip)];
            output.resize(static_cast<size_t>(width * width * height));
            for (size_t cursor = chunk.coarseCursor; cursor < output.size(); ++cursor) {
                if (budget < 8) {
                    chunk.coarseMip = mip; chunk.coarseCursor = cursor;
                    return false;
                }
                budget -= 8; m_coarseVoxelsBuilt += 8;
                const int x = static_cast<int>(cursor % width);
                const int z = static_cast<int>((cursor / width) % width);
                const int y = static_cast<int>(cursor / (width * width));
                VoxelGiPacked aggregate;
                unsigned red = 0, green = 0, blue = 0, weight = 0, sky = 0, block = 0;
                std::array<unsigned,6> exposure{};
                glm::vec3 emission(0.0f);
                unsigned emitters = 0;
                const int n = std::min(size,4), previousN = std::min(size/2,4);
                for (int dy = 0; dy < 2; ++dy)
                    for (int dz = 0; dz < 2; ++dz)
                        for (int dx = 0; dx < 2; ++dx) {
                            const int px = x*2+dx, py = y*2+dy, pz = z*2+dz;
                            const int previousWidth = width*2;
                            const size_t index = static_cast<size_t>(px + previousWidth*(pz + previousWidth*py));
                            const auto value = mip == 0 ? sourceVoxel(chunk,px,py,pz) :
                                chunk.buildingCoarse[static_cast<size_t>(mip-1)][index];
                            aggregate.opacity = std::max(aggregate.opacity,value.opacity);
                            aggregate.emission = std::max(aggregate.emission,value.emission);
                            aggregate.valid = 255;
                            red += value.red*value.opacity; green += value.green*value.opacity;
                            blue += value.blue*value.opacity; weight += value.opacity;
                            sky += value.skyLight*value.opacity; block += value.blockLight*value.opacity;
                            for (int f = 0; f < 6; ++f) exposure[f] += voxelGiExposure(value.aux,f)*value.opacity;
                            if (value.aux.emission) { emission += voxelGiUnpackRgb(value.aux.emission); ++emitters; }
                            for (int sz = 0; sz < previousN; ++sz)
                                for (int sy = 0; sy < previousN; ++sy)
                                    for (int sx = 0; sx < previousN; ++sx)
                                        if (voxelGiOccupied(value.aux,sx+previousN*(sy+previousN*sz))) {
                                            const int divisor = size > 4 ? 2 : 1;
                                            const int ax = (dx*previousN+sx)/divisor;
                                            const int ay = (dy*previousN+sy)/divisor;
                                            const int az = (dz*previousN+sz)/divisor;
                                            setVoxelGiOccupied(aggregate.aux,ax+n*(ay+n*az));
                                        }
                        }
                if (weight) {
                    aggregate.red = red/weight; aggregate.green = green/weight; aggregate.blue = blue/weight;
                    aggregate.skyLight = sky/weight; aggregate.blockLight = block/weight;
                    for (int f = 0; f < 6; ++f) setVoxelGiExposure(aggregate.aux,f,exposure[f]/weight);
                }
                if (emitters) aggregate.aux.emission = voxelGiRgb(emission/float(emitters));
                updateVoxelGiCoverage(aggregate.aux,n);
                output[static_cast<size_t>(x+width*(z+width*y))] = aggregate;
            }
            chunk.coarseCursor = 0;
            chunk.coarseMip = mip + 1;
        }
        return true;
    }

    VoxelGiPacked sample(int worldX, int worldY, int worldZ, int cellSize) const {
        if (!Config::isValidWorldY(worldY)) return {};
        const int cx = voxelGiFloorDiv(worldX, Config::CHUNK_SIZE_X);
        const int cz = voxelGiFloorDiv(worldZ, Config::CHUNK_SIZE_Z);
        const auto found = m_chunks.find(chunkKey(cx, cz));
        if (found == m_chunks.end()) return {};
        const int x = voxelGiPositiveMod(worldX, Config::CHUNK_SIZE_X);
        const int z = voxelGiPositiveMod(worldZ, Config::CHUNK_SIZE_Z);
        if (cellSize > 1) {
            // Never publish old neighbor exposure or partly rebuilt occupancy.
            if (m_coarseDirty.count(chunkKey(cx, cz))) return {};
            int mip = 0;
            for (int size = 2; size < cellSize; size *= 2) ++mip;
            const int width = Config::CHUNK_SIZE_X / cellSize;
            const size_t index = static_cast<size_t>(x / cellSize + width *
                (z / cellSize + width * (Config::worldYToStorageY(worldY) / cellSize)));
            const auto& coarse = found->second.coarse[static_cast<size_t>(mip)];
            return index < coarse.size() ? coarse[index] : VoxelGiPacked{};
        }
        const size_t index = static_cast<size_t>(x + z * Config::CHUNK_SIZE_X +
            Config::worldYToStorageY(worldY) * Config::CHUNK_SIZE_X *
                Config::CHUNK_SIZE_Z);
        if (index >= found->second.blocks.size() ||
            index >= found->second.light.size()) return {};
        return sourceVoxel(found->second,x,Config::worldYToStorageY(worldY),z);
    }

    VoxelGiSliceUpdate buildSlice(int levelIndex, int axis, int worldCell) {
        VoxelGiSliceUpdate update;
        update.level = levelIndex;
        update.axis = axis;
        update.layer = voxelGiPositiveMod(worldCell, m_config.clipmapResolution);
        const int resolution = m_config.clipmapResolution;
        if (!m_sliceBuffers.empty()) {
            update.voxels = std::move(m_sliceBuffers.back());
            m_sliceBuffers.pop_back();
        }
        update.voxels.resize(static_cast<size_t>(resolution * resolution));
        const Level& level = m_levels[static_cast<size_t>(levelIndex)];
        const int cellSize = voxelGiCellSize(m_config, levelIndex);
        // Vulkan tightly packed plane order: Y/Z for X, X/Z for Y, X/Y for Z.
        const int firstAxis = axis == 0 ? 1 : 0;
        const int secondAxis = axis == 2 ? 1 : 2;
        for (int v = 0; v < resolution; ++v) {
            for (int u = 0; u < resolution; ++u) {
                glm::ivec3 cell = level.minimumCell;
                cell[axis] = worldCell;
                cell[firstAxis] += u;
                cell[secondAxis] += v;
                const glm::ivec3 world = cell * cellSize + glm::ivec3(cellSize / 2);
                const int ringU = voxelGiPositiveMod(cell[firstAxis], resolution);
                const int ringV = voxelGiPositiveMod(cell[secondAxis], resolution);
                update.voxels[static_cast<size_t>(ringU + ringV * resolution)] =
                    sample(world.x, world.y, world.z, cellSize);
            }
        }
        return update;
    }
};
