#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <queue>
#include <unordered_map>
#include <vector>

#include "world/Block.h"

class ChunkStore;
class World;

// Owns Java-style scheduled fluid ticks.  The queue identity is
// (position, fluid type), matching LevelTicks rather than treating water and
// lava at the same coordinate as one task.
class FluidScheduler {
public:
    FluidScheduler(World& world, ChunkStore& chunks)
        : m_world(world), m_chunks(chunks) {}

    // Advance the current world tick and process all due fluid cells.
    void tick(uint64_t tick);

    // Schedule a position and its six neighbors for a fluid update.
    void scheduleAround(const glm::ivec3& position, uint64_t minimumDelay = 1);

    // Wake only the seam cells of a newly generated chunk.  Unloaded chunks
    // are never sampled as air and no ocean-wide scan is performed.
    void scheduleChunkBoundary(int chunkX, int chunkZ);

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
    struct FluidDue {
        uint64_t due = 0;
        uint64_t order = 0;
    };
    struct ScheduledFluidLater {
        bool operator()(const ScheduledFluidTick& a,
                        const ScheduledFluidTick& b) const {
            if (a.due != b.due) return a.due > b.due;
            return a.order > b.order;
        }
    };

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
    ChunkStore& m_chunks;
    std::priority_queue<ScheduledFluidTick, std::vector<ScheduledFluidTick>,
                        ScheduledFluidLater> m_fluidTicks;
    std::unordered_map<FluidTickKey, FluidDue, FluidTickKeyHash>
        m_scheduledFluidDue;
    uint64_t m_currentWorldTick = 0;
    uint64_t m_nextInsertionOrder = 0;
};
