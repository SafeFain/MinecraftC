#pragma once

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Config.h"
#include "world/Block.h"

class ChunkStore;
class World;

struct FluidTickBudget {
    size_t maximumUpdates = Config::FLUID_UPDATES_PER_TICK;
    std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::time_point::max();
};

struct FluidTickStats {
    size_t examined = 0;
    size_t updated = 0;
    size_t changed = 0;
    size_t deferred = 0;
    size_t queueSize = 0;
    bool deadlineReached = false;
};

// Owns Java-style scheduled fluid ticks.  The queue identity is
// (position, fluid type), matching LevelTicks rather than treating water and
// lava at the same coordinate as one task.
class FluidScheduler {
public:
    explicit FluidScheduler(World& world) : m_world(world) {}
    // Compatibility overload for integrations that used to pass the owning
    // ChunkStore.  Fluid scheduling now queries it through World, so the
    // second reference is intentionally not retained.
    FluidScheduler(World& world, ChunkStore&) : m_world(world) {}

    // Advance the current world tick and process a bounded batch. Every key
    // exists in the heap at most once, so examined entries are live work.
    FluidTickStats tick(uint64_t tick, const FluidTickBudget& budget);
    size_t tick(uint64_t worldTick,
                size_t maximumUpdates = Config::FLUID_UPDATES_PER_TICK) {
        return this->tick(worldTick, FluidTickBudget{maximumUpdates}).updated;
    }

    // Schedule a position and its six neighbors for a fluid update.
    void scheduleAround(const glm::ivec3& position, uint64_t minimumDelay = 1);

    // Called after every world block mutation.  It applies the Java lava /
    // water mixing matrix to the changed cell and the affected lava cells.
    void onBlockChanged(const glm::ivec3& position, BlockId previous,
                       BlockId current);

    // Drop the queue (seed reset / world teardown).
    void clear();

private:
    struct BlockPosHash {
        size_t operator()(const glm::ivec3& p) const {
            size_t h = std::hash<int>{}(p.x);
            h ^= std::hash<int>{}(p.y) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<int>{}(p.z) + 0x9e3779b9u + (h << 6) + (h >> 2);
            return h;
        }
    };

    struct FluidTickKey {
        glm::ivec3 position{0};
        bool lava = false;
        bool operator==(const FluidTickKey& other) const {
            return lava == other.lava && position == other.position;
        }
    };
    struct FluidTickKeyHash {
        size_t operator()(const FluidTickKey& key) const {
            size_t h = BlockPosHash{}(key.position);
            h ^= std::hash<bool>{}(key.lava) + 0x9e3779b9u + (h << 6) + (h >> 2);
            return h;
        }
    };
    struct ScheduledFluidTick {
        uint64_t due = 0;
        uint64_t order = 0;
        glm::ivec3 position{0};
        bool lava = false;
    };
    static bool earlier(const ScheduledFluidTick& a,
                        const ScheduledFluidTick& b);
    void swapHeapNodes(size_t first, size_t second);
    void siftUp(size_t index);
    void siftDown(size_t index);
    ScheduledFluidTick removeHeapRoot();
    void removeHeapAt(size_t index);
    void cancelAt(const glm::ivec3& position, bool lava);

    void scheduleAt(const glm::ivec3& position, uint64_t minimumDelay,
                    bool forceReschedule = false);
    void rescheduleAt(const glm::ivec3& position, uint64_t delay);
    void updateCell(const glm::ivec3& position, uint64_t tick);
    void tryMixingAt(const glm::ivec3& lavaPosition);
    void spreadTo(const glm::ivec3& from, const glm::ivec3& to,
                  bool lava, uint8_t amount, bool falling,
                  uint64_t tick);
    void randomTickLava(uint64_t tick);
    static uint64_t hash(uint64_t value);

    World& m_world;
    std::vector<ScheduledFluidTick> m_fluidTicks;
    std::unordered_map<FluidTickKey, size_t, FluidTickKeyHash>
        m_scheduledFluidIndices;
    std::unordered_set<glm::ivec3, BlockPosHash> m_affectedSet;
    std::vector<glm::ivec3> m_affectedPositions;
    uint64_t m_currentWorldTick = 0;
    uint64_t m_nextInsertionOrder = 0;
};
