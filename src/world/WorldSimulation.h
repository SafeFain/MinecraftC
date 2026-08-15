#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

#include "game/Weather.h"
#include "world/Block.h"

class ChunkStore;
class World;
class WorldPersistence;

// Owns the deterministic survival/world simulation ticks: crop and sapling
// growth over player edits, precipitation-driven snow accumulation and melt,
// fire spread and aging, and TNT ignition reporting (lava contact from the
// fluid scheduler, flame contact from fire spread). Block access goes through
// the owning World; override iteration uses the persistence component.
class WorldSimulation {
public:
    WorldSimulation(World& world, WorldPersistence& persistence,
                    ChunkStore& chunks)
        : m_world(world), m_persistence(persistence), m_chunks(chunks) {}

    void tickSurvival(const glm::dvec3& playerPosition, uint64_t tick,
                      bool raining = false);
    void tickWeather(const WeatherSystem& weather, bool daytime, uint64_t tick);

    // Drain the TNT ignition list accumulated since the last call.
    std::vector<glm::ivec3> takeTntIgnitions();

    // Report a TNT lit by lava contact (called by the fluid scheduler
    // through the owning world).
    void pushTntIgnition(const glm::ivec3& position) {
        m_tntIgnitions.push_back(position);
    }

    // Drop simulation state (seed reset / teardown).
    void clear() {
        m_fireAges.clear();
        m_tntIgnitions.clear();
    }

private:
    struct BlockPosHash {
        size_t operator()(const glm::ivec3& p) const {
            size_t h = std::hash<int>{}(p.x);
            h ^= std::hash<int>{}(p.y) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<int>{}(p.z) + 0x9e3779b9u + (h << 6) + (h >> 2);
            return h;
        }
    };

    bool growSapling(const glm::ivec3& p, BlockId sapling);
    bool hasWaterForFarmland(const glm::ivec3& position, bool raining) const;

    World& m_world;
    WorldPersistence& m_persistence;
    ChunkStore& m_chunks;
    std::unordered_map<glm::ivec3, uint8_t, BlockPosHash> m_fireAges;
    std::vector<glm::ivec3> m_tntIgnitions;
};
