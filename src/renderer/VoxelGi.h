#pragma once

#include "Config.h"
#include "renderer/VisualQuality.h"
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
};

struct VoxelGiDeviceSupport {
    uint32_t maximum3dDimension = 0;
    uint32_t sampledImagesPerStage = 0;
    uint32_t sampledImagesPerSet = 0;
    uint32_t storageImagesPerStage = 0;
    uint32_t storageImagesPerSet = 0;
    bool attributeSampling = false;
    bool irradianceSampling = false;
    bool irradianceStorage = false;
    bool irradianceBlit = false;
};

inline VoxelGiAvailability voxelGiAvailability(
        const VoxelGiDeviceSupport& support) {
    if (support.maximum3dDimension < 64 ||
        support.sampledImagesPerStage < 7 || support.sampledImagesPerSet < 7 ||
        support.storageImagesPerStage < 1 || support.storageImagesPerSet < 1)
        return VoxelGiAvailability::UnsupportedLimits;
    if (!support.attributeSampling || !support.irradianceSampling ||
        !support.irradianceStorage || !support.irradianceBlit)
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
};

inline VoxelGiPacked packVoxelGi(BlockId id, uint8_t packedLight) {
    VoxelGiPacked result;
    result.skyLight = static_cast<uint8_t>((packedLight >> 4) * 17u);
    result.blockLight = static_cast<uint8_t>((packedLight & 0x0fu) * 17u);
    result.emission = static_cast<uint8_t>(getLightEmission(id) * 17u);
    result.valid = 255;
    if (id == BlockId::AIR) return result;
    const BlockProperties& properties = getBlockProps(id);
    const glm::vec3 color = glm::clamp(properties.color, glm::vec3(0.0f),
                                      glm::vec3(1.0f));
    result.red = static_cast<uint8_t>(std::lround(color.r * 255.0f));
    result.green = static_cast<uint8_t>(std::lround(color.g * 255.0f));
    result.blue = static_cast<uint8_t>(std::lround(color.b * 255.0f));
    float opacity = properties.layer == RenderLayer::Opaque ? 1.0f :
        properties.layer == RenderLayer::Cutout ? 0.42f :
        isFluid(id) ? 0.16f : 0.28f;
    if (properties.shape == RenderShape::Cross ||
        properties.shape == RenderShape::CeilingCross) opacity *= 0.25f;
    if (properties.shape == RenderShape::SnowLayer) opacity *= 0.18f;
    if (properties.shape == RenderShape::Slab) opacity *= 0.5f;
    if (properties.shape == RenderShape::Spike) opacity *= 0.35f;
    result.opacity = static_cast<uint8_t>(std::lround(opacity * 255.0f));
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
    int zLayer = 0;
    std::vector<VoxelGiPacked> voxels;
};

struct VoxelGiLevelMapping {
    glm::ivec3 minimumCell{0};
    int cellSize = 1;
};

inline bool voxelGiRejectHistory(float currentDepth, float previousDepth,
                                 const glm::vec3& currentNormal,
                                 const glm::vec3& previousNormal,
                                 bool insideScreen, bool clipmapValid) {
    if (!insideScreen || !clipmapValid || currentDepth <= 0.0f ||
        previousDepth <= 0.0f) return true;
    const float tolerance = std::max(0.18f, currentDepth * 0.018f);
    return std::abs(currentDepth - previousDepth) > tolerance ||
        glm::dot(glm::normalize(currentNormal),
                 glm::normalize(previousNormal)) < 0.82f;
}

inline glm::vec3 voxelGiNeighborhoodClamp(const glm::vec3& history,
                                          const glm::vec3& minimum,
                                          const glm::vec3& maximum) {
    return glm::clamp(history, minimum, maximum);
}

// CPU-side source cache and toroidal clipmap scheduler. It owns snapshots only
// for chunks accepted within the current bounded frame budget. Vulkan consumes
// completed Z slices immediately and never retains Chunk pointers.
class VoxelGiSceneCache {
public:
    void configure(const VoxelGiConfig& config) {
        const bool changed = m_config.clipmapResolution != config.clipmapResolution ||
            m_config.clipmapLevels != config.clipmapLevels ||
            std::abs(m_config.distance - config.distance) > 0.5f;
        m_config = config;
        if (changed) reset();
    }

    void beginFrame(const glm::dvec3& camera, uint64_t sceneId) {
        ++m_frame;
        m_chunkCopiesRemaining = std::max(1, m_config.updateSlicesPerFrame / 2);
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
        --m_chunkCopiesRemaining;
        markChunkDirty(chunk.cx, chunk.cz);
        return true;
    }

    void endFrame() {
        for (auto it = m_chunks.begin(); it != m_chunks.end();) {
            if (m_frame - it->second.lastSeen > 2) it = m_chunks.erase(it);
            else ++it;
        }
    }

    std::vector<VoxelGiSliceUpdate> takeUpdates(int maximum) {
        std::vector<VoxelGiSliceUpdate> result;
        maximum = std::max(0, maximum);
        while (!m_dirty.empty() && static_cast<int>(result.size()) < maximum) {
            const DirtySlice dirty = m_dirty.front();
            m_dirty.pop_front();
            m_dirtySet.erase((static_cast<uint64_t>(
                static_cast<uint32_t>(dirty.level)) << 32) |
                static_cast<uint32_t>(dirty.worldZ));
            if (dirty.level < 0 || dirty.level >= static_cast<int>(m_levels.size()))
                continue;
            result.push_back(buildSlice(dirty.level, dirty.worldZ));
        }
        return result;
    }

    size_t pendingSlices() const { return m_dirty.size(); }
    const VoxelGiConfig& config() const { return m_config; }
    VoxelGiLevelMapping levelMapping(int level) const {
        if (level < 0 || level >= static_cast<int>(m_levels.size())) return {};
        return {m_levels[static_cast<size_t>(level)].minimumCell,
                voxelGiCellSize(m_config, level)};
    }

    void reset() {
        m_chunks.clear();
        m_levels.clear();
        m_dirty.clear();
        m_dirtySet.clear();
        m_sceneId = std::numeric_limits<uint64_t>::max();
    }

private:
    struct CachedChunk {
        int cx = 0;
        int cz = 0;
        uint64_t revision = 0;
        uint64_t lastSeen = 0;
        std::vector<uint8_t> blocks;
        std::vector<uint8_t> light;
    };
    struct Level {
        glm::ivec3 minimumCell{0};
        bool initialized = false;
    };
    struct DirtySlice { int level = 0; int worldZ = 0; };

    VoxelGiConfig m_config{};
    glm::dvec3 m_camera{0.0};
    uint64_t m_sceneId = std::numeric_limits<uint64_t>::max();
    uint64_t m_frame = 0;
    int m_chunkCopiesRemaining = 0;
    std::unordered_map<int64_t, CachedChunk> m_chunks;
    std::vector<Level> m_levels;
    std::deque<DirtySlice> m_dirty;
    std::unordered_set<uint64_t> m_dirtySet;

    static int64_t chunkKey(int cx, int cz) {
        return static_cast<int64_t>(
            (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
            static_cast<uint32_t>(cz));
    }

    void ensureLevels() {
        if (static_cast<int>(m_levels.size()) != m_config.clipmapLevels)
            m_levels.assign(static_cast<size_t>(m_config.clipmapLevels), {});
    }

    void queueFullLevel(int level) {
        const Level& state = m_levels[static_cast<size_t>(level)];
        for (int z = 0; z < m_config.clipmapResolution; ++z)
            queueSlice(level, state.minimumCell.z + z);
    }

    void queueSlice(int level, int worldZ) {
        const uint64_t key = (static_cast<uint64_t>(
            static_cast<uint32_t>(level)) << 32) |
            static_cast<uint32_t>(worldZ);
        if (m_dirtySet.insert(key).second) m_dirty.push_back({level, worldZ});
    }

    void updateLevelOrigin(int levelIndex) {
        Level& state = m_levels[static_cast<size_t>(levelIndex)];
        const int cellSize = voxelGiCellSize(m_config, levelIndex);
        const int half = m_config.clipmapResolution / 2;
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
        const int deltaZ = next.z - previous.z;
        state.minimumCell = next;
        if (std::abs(deltaZ) >= m_config.clipmapResolution) {
            queueFullLevel(levelIndex);
            return;
        }
        if (deltaZ > 0)
            for (int z = m_config.clipmapResolution - deltaZ;
                 z < m_config.clipmapResolution; ++z)
                queueSlice(levelIndex, next.z + z);
        else if (deltaZ < 0)
            for (int z = 0; z < -deltaZ; ++z)
                queueSlice(levelIndex, next.z + z);
        // X/Y movement changes the contents of every Z plane. Re-queue them
        // gradually; this remains bounded and avoids a synchronous rebuild.
        if (next.x != previous.x || next.y != previous.y)
            queueFullLevel(levelIndex);
    }

    void markChunkDirty(int cx, int cz) {
        const int worldMinZ = cz * Config::CHUNK_SIZE_Z;
        const int worldMaxZ = worldMinZ + Config::CHUNK_SIZE_Z - 1;
        for (int level = 0; level < static_cast<int>(m_levels.size()); ++level) {
            const int cellSize = voxelGiCellSize(m_config, level);
            const int minCell = voxelGiFloorDiv(worldMinZ, cellSize);
            const int maxCell = voxelGiFloorDiv(worldMaxZ, cellSize);
            const Level& state = m_levels[static_cast<size_t>(level)];
            for (int z = std::max(minCell, state.minimumCell.z);
                 z <= std::min(maxCell, state.minimumCell.z +
                    m_config.clipmapResolution - 1); ++z)
                queueSlice(level, z);
        }
        (void)cx;
    }

    VoxelGiPacked sample(int worldX, int worldY, int worldZ) const {
        if (!Config::isValidWorldY(worldY)) return {};
        const int cx = voxelGiFloorDiv(worldX, Config::CHUNK_SIZE_X);
        const int cz = voxelGiFloorDiv(worldZ, Config::CHUNK_SIZE_Z);
        const auto found = m_chunks.find(chunkKey(cx, cz));
        if (found == m_chunks.end()) return {};
        const int x = voxelGiPositiveMod(worldX, Config::CHUNK_SIZE_X);
        const int z = voxelGiPositiveMod(worldZ, Config::CHUNK_SIZE_Z);
        const size_t index = static_cast<size_t>(x + z * Config::CHUNK_SIZE_X +
            Config::worldYToStorageY(worldY) * Config::CHUNK_SIZE_X *
                Config::CHUNK_SIZE_Z);
        if (index >= found->second.blocks.size() ||
            index >= found->second.light.size()) return {};
        return packVoxelGi(static_cast<BlockId>(found->second.blocks[index]),
                           found->second.light[index]);
    }

    VoxelGiSliceUpdate buildSlice(int levelIndex, int worldCellZ) const {
        VoxelGiSliceUpdate update;
        update.level = levelIndex;
        update.zLayer = voxelGiPositiveMod(
            worldCellZ, m_config.clipmapResolution);
        const int resolution = m_config.clipmapResolution;
        update.voxels.resize(static_cast<size_t>(resolution * resolution));
        const Level& level = m_levels[static_cast<size_t>(levelIndex)];
        const int cellSize = voxelGiCellSize(m_config, levelIndex);
        for (int y = 0; y < resolution; ++y) {
            for (int x = 0; x < resolution; ++x) {
                const int worldCellX = level.minimumCell.x + x;
                const int worldCellY = level.minimumCell.y + y;
                const int worldX = worldCellX * cellSize + cellSize / 2;
                const int worldY = worldCellY * cellSize + cellSize / 2;
                const int worldZ = worldCellZ * cellSize + cellSize / 2;
                const int ringX = voxelGiPositiveMod(worldCellX, resolution);
                const int ringY = voxelGiPositiveMod(worldCellY, resolution);
                update.voxels[static_cast<size_t>(ringX + ringY * resolution)] =
                    sample(worldX, worldY, worldZ);
            }
        }
        return update;
    }
};
