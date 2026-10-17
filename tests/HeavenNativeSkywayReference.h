#pragma once
#include "world/WorldGenerator.h"

// Frozen pre-extraction native Skyway algorithms from 899b514. Keep independent
// of the production policy so a shared regression cannot move both expectations.
struct HeavenNativeSkywayReference {
    struct HeavenSkyway {int worldX,worldZ,mainFloorY;bool origin;};
    static constexpr uint64_t HEAVEN_SKYWAY_DOMAIN=0x534B595741595631ULL;
    static constexpr int HEAVEN_SKYWAY_CELL=256,HEAVEN_LAYER_COUNT=5;
    inline static constexpr int HEAVEN_SKYWAY_OFFSETS[5]={-88,-44,0,44,88};
    const WorldGenerator& generator;
    uint64_t m_seed;
    glm::ivec3 m_heavenSpawnBlock;
    HeavenSkyway m_originSkyway;
    explicit HeavenNativeSkywayReference(const WorldGenerator& g):generator(g),m_seed(g.getSeed()),m_heavenSpawnBlock(g.heavenSpawnBlock()),m_originSkyway(findOriginSkyway()){}
    static int floorDiv(int n,int d){return static_cast<int>(std::floor(double(n)/d));}
HeavenSkyway findOriginSkyway() const {
    for (int radius = 5; radius <= 32; ++radius) {
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::abs(dx) != radius && std::abs(dz) != radius) continue;
                const int x = m_heavenSpawnBlock.x + dx;
                const int z = m_heavenSpawnBlock.z + dz;
                const auto center = generator.sampleHeavenLayers(x, z)[2];
                if (!center.present ||
                    std::abs(center.top - m_heavenSpawnBlock.y) > 2)
                    continue;
                bool supported = true;
                for (int oz = -3; oz <= 3 && supported; oz += 3) {
                    for (int ox = -3; ox <= 3; ox += 3) {
                        const auto column =
                            generator.sampleHeavenLayers(x + ox, z + oz)[2];
                        if (!column.present ||
                            std::abs(column.top - center.top) > 2) {
                            supported = false;
                            break;
                        }
                    }
                }
                if (supported) return HeavenSkyway{x, z, center.top, true};
            }
        }
    }
    return HeavenSkyway{m_heavenSpawnBlock.x + 5, m_heavenSpawnBlock.z,
                        m_heavenSpawnBlock.y, true};
}

std::optional<HeavenSkyway>
heavenSkywayForCell(int cellX, int cellZ) const {
    const uint64_t cellSeed = WorldGenContext::hashPosition(
        m_seed ^ HEAVEN_SKYWAY_DOMAIN, cellX, 0, cellZ);
    for (int attempt = 0; attempt < 64; ++attempt) {
        const uint64_t h = WorldGenContext::hashPosition(
            cellSeed, attempt, 31, -attempt);
        const int x = cellX * HEAVEN_SKYWAY_CELL + 12 +
            static_cast<int>((h >> 8) % (HEAVEN_SKYWAY_CELL - 24));
        const int z = cellZ * HEAVEN_SKYWAY_CELL + 12 +
            static_cast<int>((h >> 32) % (HEAVEN_SKYWAY_CELL - 24));
        const auto center = generator.sampleHeavenLayers(x, z)[2];
        if (!center.present) continue;
        bool supported = true;
        for (int dz = -3; dz <= 3 && supported; dz += 3) {
            for (int dx = -3; dx <= 3; dx += 3) {
                const auto column =
                    generator.sampleHeavenLayers(x + dx, z + dz)[2];
                if (!column.present || std::abs(column.top - center.top) > 2) {
                    supported = false;
                    break;
                }
            }
        }
        if (supported) return HeavenSkyway{x, z, center.top, false};
    }
    return {};
}

std::optional<glm::dvec3> heavenSkywayDestination(
    const glm::ivec3& core, bool upward) const {
    std::optional<HeavenSkyway> skyway;
    if (core.x == m_originSkyway.worldX && core.z == m_originSkyway.worldZ)
        skyway = m_originSkyway;
    if (!skyway) {
        const int cellX = floorDiv(core.x, HEAVEN_SKYWAY_CELL);
        const int cellZ = floorDiv(core.z, HEAVEN_SKYWAY_CELL);
        skyway = heavenSkywayForCell(cellX, cellZ);
        if (!skyway || skyway->worldX != core.x || skyway->worldZ != core.z)
            return {};
    }
    int layer = -1;
    for (int i = 0; i < HEAVEN_LAYER_COUNT; ++i) {
        if (core.y == skyway->mainFloorY + HEAVEN_SKYWAY_OFFSETS[i] + 1) {
            layer = i;
            break;
        }
    }
    if (layer < 0) return {};
    const int targetLayer = layer + (upward ? 1 : -1);
    if (targetLayer < 0 || targetLayer >= HEAVEN_LAYER_COUNT) return {};
    const int floorY = skyway->mainFloorY + HEAVEN_SKYWAY_OFFSETS[targetLayer];
    return glm::dvec3(skyway->worldX + 1.5, floorY + 1.01,
                      skyway->worldZ + 0.5);
}

};
