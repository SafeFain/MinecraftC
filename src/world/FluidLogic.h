#pragma once

#include "world/Block.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

#include <glm/glm.hpp>

inline constexpr std::array<glm::ivec3, 4> FLUID_HORIZONTAL_OFFSETS{{
    {1,0,0}, {-1,0,0}, {0,0,1}, {0,0,-1}
}};

inline uint8_t fluidHorizontalDecay(bool lava) { return lava ? 2 : 1; }
inline uint64_t fluidTickDelay(bool lava) { return lava ? 30u : 5u; }
inline bool isDerivedFluidState(BlockId block) {
    return isFluid(block) && fluidLevel(block) != 0;
}

inline uint8_t nextFluidLevel(bool lava, uint8_t level) {
    const unsigned next = static_cast<unsigned>(level) + fluidHorizontalDecay(lava);
    return static_cast<uint8_t>(std::min(next, 8u));
}

inline bool fluidCanOccupy(BlockId target, bool lava, uint8_t level) {
    const bool same = lava ? isLava(target) : isWater(target);
    const bool opposite = lava ? isWater(target) : isLava(target);
    return opposite || isReplaceableByFluid(target) ||
        (same && fluidLevel(target) > level);
}

using FluidSample = std::function<BlockId(const glm::ivec3&)>;
using FluidAvailable = std::function<bool(const glm::ivec3&)>;

inline int fluidSlopeDistance(const glm::ivec3& origin, bool lava, uint8_t level,
                              int depth, int blockedDirection,
                              const FluidSample& sample,
                              const FluidAvailable& available) {
    constexpr int maximumSearchDepth = 4;
    constexpr int unreachable = 1000;
    if (depth >= maximumSearchDepth) return unreachable;
    int best = unreachable;
    const uint8_t candidateLevel = nextFluidLevel(lava, level);
    if (candidateLevel > 7) return best;
    for (int direction = 0; direction < 4; ++direction) {
        if (direction == blockedDirection) continue;
        const glm::ivec3 candidate = origin + FLUID_HORIZONTAL_OFFSETS[direction];
        if (!available(candidate) ||
            !fluidCanOccupy(sample(candidate), lava, candidateLevel)) continue;
        const glm::ivec3 below = candidate + glm::ivec3(0,-1,0);
        if (available(below) && fluidCanOccupy(sample(below), lava, 1)) return depth;
        best = std::min(best, fluidSlopeDistance(candidate, lava, candidateLevel,
            depth + 1, direction ^ 1, sample, available));
    }
    return best;
}

inline std::vector<glm::ivec3> preferredFluidDirections(
        const glm::ivec3& origin, bool lava, uint8_t currentLevel,
        const FluidSample& sample, const FluidAvailable& available) {
    constexpr int unreachable = 1000;
    const uint8_t nextLevel = nextFluidLevel(lava, currentLevel);
    if (nextLevel > 7) return {};
    std::array<int,4> costs{{unreachable,unreachable,unreachable,unreachable}};
    std::array<bool,4> candidates{{false,false,false,false}};
    int best = unreachable;
    for (int direction = 0; direction < 4; ++direction) {
        const glm::ivec3 candidate = origin + FLUID_HORIZONTAL_OFFSETS[direction];
        if (!available(candidate) || !fluidCanOccupy(sample(candidate),lava,nextLevel))
            continue;
        candidates[direction]=true;
        const glm::ivec3 below = candidate + glm::ivec3(0,-1,0);
        costs[direction] = available(below) && fluidCanOccupy(sample(below),lava,1)
            ? 0 : fluidSlopeDistance(candidate,lava,nextLevel,1,direction^1,sample,available);
        best = std::min(best,costs[direction]);
    }
    std::vector<glm::ivec3> result;
    for (int direction=0;direction<4;++direction)
        if (candidates[direction]&&costs[direction]==best)
            result.push_back(FLUID_HORIZONTAL_OFFSETS[direction]);
    return result;
}
