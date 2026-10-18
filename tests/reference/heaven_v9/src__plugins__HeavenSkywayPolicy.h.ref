#pragma once
#include "Config.h"
#include "world/WorldGenContext.h"
#include <glm/glm.hpp>
#include <array>
#include <optional>
#include <cmath>
namespace HeavenPolicy {
inline constexpr uint64_t HEAVEN_SKYWAY_DOMAIN=0x534B595741595631ULL;
inline constexpr int HEAVEN_SKYWAY_CELL=256;
inline constexpr std::array<int,5> HEAVEN_SKYWAY_OFFSETS{{-88,-44,0,44,88}};
struct HeavenSkyway {int worldX=0,worldZ=0,mainFloorY=0;bool origin=false;};
inline int floorDiv(int n,int d){return n/d-((n%d)<0?1:0);}
template<class Sample> HeavenSkyway findOrigin(glm::ivec3 spawn,Sample sample){
    for (int radius = 5; radius <= 32; ++radius) {
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::abs(dx) != radius && std::abs(dz) != radius) continue;
                const int x = spawn.x + dx;
                const int z = spawn.z + dz;
                const auto center = sample(x, z);
                if (!center.present ||
                    std::abs(center.top - spawn.y) > 2)
                    continue;
                bool supported = true;
                for (int oz = -3; oz <= 3 && supported; oz += 3) {
                    for (int ox = -3; ox <= 3; ox += 3) {
                        const auto column =
                            sample(x + ox, z + oz);
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
    return HeavenSkyway{spawn.x + 5, spawn.z,
                        spawn.y, true};
}
template<class Sample> std::optional<HeavenSkyway> forCell(uint64_t seed,int cellX,int cellZ,Sample sample){
    const uint64_t cellSeed = WorldGenContext::hashPosition(
        seed ^ HEAVEN_SKYWAY_DOMAIN, cellX, 0, cellZ);
    for (int attempt = 0; attempt < 64; ++attempt) {
        const uint64_t h = WorldGenContext::hashPosition(
            cellSeed, attempt, 31, -attempt);
        const int x = cellX * HEAVEN_SKYWAY_CELL + 12 +
            static_cast<int>((h >> 8) % (HEAVEN_SKYWAY_CELL - 24));
        const int z = cellZ * HEAVEN_SKYWAY_CELL + 12 +
            static_cast<int>((h >> 32) % (HEAVEN_SKYWAY_CELL - 24));
        const auto center = sample(x, z);
        if (!center.present) continue;
        bool supported = true;
        for (int dz = -3; dz <= 3 && supported; dz += 3) {
            for (int dx = -3; dx <= 3; dx += 3) {
                const auto column =
                    sample(x + dx, z + dz);
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
template<class Cell> std::optional<glm::dvec3> destination(glm::ivec3 core,bool upward,HeavenSkyway origin,Cell cell){
    std::optional<HeavenSkyway> skyway;
    if (core.x == origin.worldX && core.z == origin.worldZ)
        skyway = origin;
    if (!skyway) {
        const int cellX = floorDiv(core.x, HEAVEN_SKYWAY_CELL);
        const int cellZ = floorDiv(core.z, HEAVEN_SKYWAY_CELL);
        skyway = cell(cellX, cellZ);
        if (!skyway || skyway->worldX != core.x || skyway->worldZ != core.z)
            return {};
    }
    int layer = -1;
    for (int i = 0; i < 5; ++i) {
        if (core.y == skyway->mainFloorY + HEAVEN_SKYWAY_OFFSETS[i] + 1) {
            layer = i;
            break;
        }
    }
    if (layer < 0) return {};
    const int targetLayer = layer + (upward ? 1 : -1);
    if (targetLayer < 0 || targetLayer >= 5) return {};
    const int floorY = skyway->mainFloorY + HEAVEN_SKYWAY_OFFSETS[targetLayer];
    return glm::dvec3(skyway->worldX + 1.5, floorY + 1.01,
                      skyway->worldZ + 0.5);
}
}
