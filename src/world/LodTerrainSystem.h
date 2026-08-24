#pragma once

#include "world/ChunkMesh.h"
#include "world/LodSettings.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

class Chunk;
class IGameRenderer;
class SaveStore;
class ThreadPool;
class WorldGenerator;

struct LodSpan {
    int16_t bottom = 0;
    int16_t top = 0;
    BlockId block = BlockId::AIR;
};

struct LodColumn {
    std::vector<LodSpan> spans;
    bool exact = false;
};

struct LodTileKey {
    int32_t x = 0;
    int32_t z = 0;
    uint8_t level = 0;

    friend bool operator==(const LodTileKey& a, const LodTileKey& b) {
        return a.x == b.x && a.z == b.z && a.level == b.level;
    }
};

struct LodTileKeyHash {
    size_t operator()(const LodTileKey& key) const {
        size_t value = std::hash<int32_t>{}(key.x);
        value ^= std::hash<int32_t>{}(key.z) + 0x9e3779b9u +
                 (value << 6) + (value >> 2);
        value ^= std::hash<uint8_t>{}(key.level) + 0x9e3779b9u +
                 (value << 6) + (value >> 2);
        return value;
    }
};

struct LodTileData {
    static constexpr int SIDE = 16;
    std::array<LodColumn, SIDE * SIDE> columns;

    LodColumn& at(int x, int z) { return columns[x + z * SIDE]; }
    const LodColumn& at(int x, int z) const { return columns[x + z * SIDE]; }
    size_t memoryBytes() const;
};

struct LodRenderSubmission {
    const ChunkMesh* mesh = nullptr;
    glm::mat4 model{1.0f};
    float minimumDistance = 0.0f;
    float maximumDistance = 0.0f;
    float distance2 = 0.0f;
};

LodTileData buildApproximateLodTile(const WorldGenerator& generator,
                                    const LodTileKey& key);
LodTileData extractExactLodChunk(const std::vector<uint8_t>& blocks,
                                 int maximumSpans = 24);
ChunkMesh buildLodTileMesh(const LodTileData& data, int cellSize,
                           int maximumSpans);

class LodTerrainSystem {
public:
    LodTerrainSystem() = default;
    ~LodTerrainSystem();

    LodTerrainSystem(const LodTerrainSystem&) = delete;
    LodTerrainSystem& operator=(const LodTerrainSystem&) = delete;

    void setThreadPool(ThreadPool* pool) { m_threadPool = pool; }
    void setSaveStore(SaveStore* store);
    void reset(WorldGenerator* generator);
    void configure(const LodSettings& settings);
    void update(const glm::dvec3& playerPosition, int nearDistanceChunks,
                const std::vector<Chunk*>& activeChunks);
    void processCompleted(IGameRenderer* renderer);
    void releaseGpuMeshes(bool retainCpuGeometry = false);

    const std::vector<LodRenderSubmission>& submissions() const {
        return m_submissions;
    }
    bool enabled() const { return m_settings.enabled; }
    int distanceChunks() const { return m_settings.distanceChunks; }
    size_t residentCpuBytes() const { return m_cpuBytes; }
    size_t residentGpuBytes() const { return m_gpuBytes; }
    int tasksInFlight() const { return m_tasksInFlight.load(); }

private:
    struct Request {
        LodTileKey key;
        float minimumDistance = 0.0f;
        float maximumDistance = 0.0f;
        float distance2 = 0.0f;
    };
    struct Tile {
        LodTileKey key;
        LodTileData data;
        ChunkMesh mesh;
        bool queued = false;
        size_t gpuBytes = 0;
        float minimumDistance = 0.0f;
        float maximumDistance = 0.0f;
        float distance2 = 0.0f;
    };
    struct Completion {
        LodTileKey key;
        uint64_t epoch = 0;
        LodTileData data;
        ChunkMesh mesh;
    };
    struct ExactCompletion {
        int cx = 0;
        int cz = 0;
        uint64_t revision = 0;
        uint64_t epoch = 0;
        LodTileData data;
    };

    LodSettings m_settings;
    ThreadPool* m_threadPool = nullptr;
    SaveStore* m_saveStore = nullptr;
    WorldGenerator* m_generator = nullptr;
    IGameRenderer* m_renderer = nullptr;
    std::filesystem::path m_cacheRoot;
    std::unordered_map<LodTileKey, std::unique_ptr<Tile>, LodTileKeyHash> m_tiles;
    std::vector<Request> m_desired;
    std::vector<LodRenderSubmission> m_submissions;
    std::unordered_map<uint64_t, uint64_t> m_exactRevisions;
    std::unordered_set<uint64_t> m_exactChunks;
    std::deque<Completion> m_completions;
    std::deque<ExactCompletion> m_exactCompletions;
    std::mutex m_completionMutex;
    std::atomic<int> m_tasksInFlight{0};
    uint64_t m_epoch = 1;
    int m_centerChunkX = 0;
    int m_centerChunkZ = 0;
    int m_nearDistanceChunks = 8;
    glm::dvec3 m_playerPosition{0.0};
    bool m_selectionDirty = true;
    size_t m_cpuBytes = 0;
    size_t m_gpuBytes = 0;

    void rebuildSelection();
    void enqueueRequests();
    void observeExactChunks(const std::vector<Chunk*>& activeChunks);
    void rebuildSubmissions();
    void invalidateTilesForChunk(int cx, int cz);
    void scanExactCache();
    std::vector<std::pair<int, int>> exactChunksForTile(
        const LodTileKey& key) const;
    static uint64_t packedChunkKey(int cx, int cz);
};
