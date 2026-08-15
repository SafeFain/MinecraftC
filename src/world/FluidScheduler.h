#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <queue>
#include <unordered_map>
#include <vector>

#include "world/Block.h"

class ChunkStore;
class World;

// Owns the scheduled fluid tick queue and its update rules. Fluid cells are
// scheduled when blocks change or when fluids spread, and processed by tick()
// up to the current world tick. Block access goes through the owning World
// (block queries, derived-block writes, TNT ignition reporting); the chunk
// lock is the ChunkStore's, taken per access.
class FluidScheduler {
public:
    FluidScheduler(World& world, ChunkStore& chunks)
        : m_world(world), m_chunks(chunks) {}

    // Advance the current world tick and process all due fluid cells.
    void tick(uint64_t tick);

    // Schedule a position and its six neighbors for a fluid update.
    void scheduleAround(const glm::ivec3& position, uint64_t minimumDelay = 1);

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
    struct ScheduledFluidTick {
        uint64_t due = 0;
        glm::ivec3 position{0};
    };
    struct ScheduledFluidLater {
        bool operator()(const ScheduledFluidTick& a,
                        const ScheduledFluidTick& b) const {
            if (a.due != b.due) return a.due > b.due;
            if (a.position.y != b.position.y) return a.position.y > b.position.y;
            if (a.position.z != b.position.z) return a.position.z > b.position.z;
            return a.position.x > b.position.x;
        }
    };

    void updateCell(const glm::ivec3& position, uint64_t tick);

    World& m_world;
    ChunkStore& m_chunks;
    std::priority_queue<ScheduledFluidTick, std::vector<ScheduledFluidTick>,
                        ScheduledFluidLater> m_fluidTicks;
    std::unordered_map<glm::ivec3, uint64_t, BlockPosHash> m_scheduledFluidDue;
    uint64_t m_currentWorldTick = 0;
};
