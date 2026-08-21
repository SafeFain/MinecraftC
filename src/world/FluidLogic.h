#pragma once

#include "world/Block.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

#include <glm/glm.hpp>

// The four horizontal directions are deliberately kept in a stable order.
// Besides making the routing deterministic this is the order Java's
// FluidState uses when it visits equally good destinations.
inline constexpr std::array<glm::ivec3, 4> FLUID_HORIZONTAL_OFFSETS{{
    {1,0,0}, {-1,0,0}, {0,0,1}, {0,0,-1}
}};

inline uint8_t fluidHorizontalDecay(bool lava) { return lava ? 2 : 1; }
inline uint64_t fluidTickDelay(bool lava) { return lava ? 30u : 5u; }
inline int fluidSlopeFindDistance(bool lava) { return lava ? 2 : 4; }
inline bool fluidCanConvertToSource(bool lava) { return !lava; }

inline uint64_t fluidSpreadDelay(bool lava, BlockId oldBlock, BlockId newBlock,
                                 uint64_t random) {
    if (!lava) return fluidTickDelay(false);
    const auto oldState = decodeFluidState(oldBlock);
    const auto newState = decodeFluidState(newBlock);
    if (!oldState.has_value() || !newState.has_value() || oldState->falling ||
        newState->falling || oldState->amount >= newState->amount)
        return fluidTickDelay(true);
    return random % 4 == 0 ? fluidTickDelay(true) : fluidTickDelay(true) * 4;
}

// The old level API is retained for callers and old saves. New code should
// use FluidStateInfo::amount: amount 8 is a full source/falling column and
// amount 1 is the last horizontal water/lava state.
inline bool isDerivedFluidState(BlockId block) {
    const auto state = decodeFluidState(block);
    return state.has_value() && !state->source;
}

inline uint8_t nextFluidLevel(bool lava, uint8_t level) {
    const unsigned next = static_cast<unsigned>(level) + fluidHorizontalDecay(lava);
    return static_cast<uint8_t>(std::min(next, 8u));
}

inline bool sameFluid(BlockId block, bool lava) {
    return lava ? isLava(block) : isWater(block);
}

// Compatibility predicate using the legacy level representation. The
// historical helper accepted opposite fluids; the amount-based scheduler uses
// the stricter directional predicate below before applying Java mixing.
inline bool fluidCanOccupy(BlockId target, bool lava, uint8_t level) {
    const bool same = sameFluid(target, lava);
    const bool opposite = lava ? isWater(target) : isLava(target);
    const auto state = decodeFluidState(target);
    return opposite || isReplaceableByFluid(target) ||
        (same && state.has_value() && !state->source && fluidLevel(target) > level);
}

inline bool fluidCanReceiveAmount(BlockId target, bool lava, uint8_t amount,
                                  bool falling = false) {
    const auto state = decodeFluidState(target);
    if (state.has_value()) {
        if (state->lava != lava) return false;
        // A source is never overwritten by an ordinary flow. A falling
        // column carries amount 8 and can refresh a weaker derived state.
        return !state->source && state->amount < amount;
    }
    (void)falling;
    return isReplaceableByFluid(target);
}

// FlowingFluid's water-hole guard prevents a non-source column from fanning
// out when the block below is already the same fluid source (or is a fluid
// holding hole that the directional replacement rules intentionally reject).
inline bool isFluidWaterHole(BlockId target, bool lava) {
    const auto state = decodeFluidState(target);
    return (state.has_value() && state->lava == lava) ||
           isReplaceableByFluid(target);
}

using FluidSample = std::function<BlockId(const glm::ivec3&)>;
using FluidAvailable = std::function<bool(const glm::ivec3&)>;

// Java's getSlopeDistance. `amount` is the amount available in the current
// cell; each horizontal step consumes the material-specific dropoff.
inline int fluidSlopeDistanceByAmount(const glm::ivec3& origin, bool lava,
                                      uint8_t amount, int depth,
                                      int blockedDirection,
                                      const FluidSample& sample,
                                      const FluidAvailable& available) {
    constexpr int unreachable = 1000;
    if (depth >= fluidSlopeFindDistance(lava)) return unreachable;
    const uint8_t candidateAmount = amount > fluidHorizontalDecay(lava)
        ? static_cast<uint8_t>(amount - fluidHorizontalDecay(lava)) : 0;
    if (candidateAmount == 0) return unreachable;
    int best = unreachable;
    for (int direction = 0; direction < 4; ++direction) {
        if (direction == blockedDirection) continue;
        const glm::ivec3 candidate = origin + FLUID_HORIZONTAL_OFFSETS[direction];
        if (!available(candidate) ||
            !fluidCanReceiveAmount(sample(candidate), lava, candidateAmount))
            continue;
        const glm::ivec3 below = candidate + glm::ivec3(0,-1,0);
        if (available(below) &&
            fluidCanReceiveAmount(sample(below), lava, 8, true)) return depth;
        best = std::min(best, fluidSlopeDistanceByAmount(
            candidate, lava, candidateAmount, depth + 1, direction ^ 1,
            sample, available));
    }
    return best;
}

inline std::vector<glm::ivec3> preferredFluidDirectionsByAmount(
        const glm::ivec3& origin, bool lava, uint8_t currentAmount,
        bool currentFalling, const FluidSample& sample,
        const FluidAvailable& available) {
    constexpr int unreachable = 1000;
    const uint8_t sideAmount = currentFalling
        ? static_cast<uint8_t>(7)
        : (currentAmount > fluidHorizontalDecay(lava)
            ? static_cast<uint8_t>(currentAmount - fluidHorizontalDecay(lava)) : 0);
    if (sideAmount == 0) return {};
    std::array<int,4> costs{{unreachable,unreachable,unreachable,unreachable}};
    std::array<bool,4> candidates{{false,false,false,false}};
    int best = unreachable;
    for (int direction = 0; direction < 4; ++direction) {
        const glm::ivec3 candidate = origin + FLUID_HORIZONTAL_OFFSETS[direction];
        if (!available(candidate) ||
            !fluidCanReceiveAmount(sample(candidate), lava, sideAmount)) continue;
        candidates[direction] = true;
        const glm::ivec3 below = candidate + glm::ivec3(0,-1,0);
        costs[direction] = available(below) &&
            fluidCanReceiveAmount(sample(below), lava, 8, true)
            ? 0 : fluidSlopeDistanceByAmount(candidate, lava, sideAmount,
                                              1, direction ^ 1, sample, available);
        best = std::min(best, costs[direction]);
    }
    // A flat, supported surface has no downward route.  It is still a valid
    // horizontal destination: Java's spread code keeps all equally best
    // candidates instead of treating the absence of a drop as a dead end.
    // This is what lets a source on superflat terrain fan out normally.
    std::vector<glm::ivec3> result;
    for (int direction = 0; direction < 4; ++direction)
        if (candidates[direction] && costs[direction] == best)
            result.push_back(FLUID_HORIZONTAL_OFFSETS[direction]);
    return result;
}

// Legacy level-based routing wrappers. Their numerical behavior is kept for
// old callers/tests while the scheduler uses the amount-based API above.
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

// A compact version of FlowingFluid#getFlow used by both fluid meshing and
// tests. The vector points toward the lower neighboring surface.
inline glm::vec2 fluidFlowVector(const glm::ivec3& origin, bool lava,
                                 const FluidSample& sample,
                                 const FluidAvailable& available) {
    const BlockId current = sample(origin);
    const auto state = decodeFluidState(current);
    if (!state.has_value() || state->lava != lava) return glm::vec2(0.0f);
    const float own = fluidSurfaceHeight(current);
    glm::vec2 flow(0.0f);
    for (int direction = 0; direction < 4; ++direction) {
        const glm::ivec3 neighbor = origin + FLUID_HORIZONTAL_OFFSETS[direction];
        if (!available(neighbor)) continue;
        const BlockId neighborBlock = sample(neighbor);
        float neighborHeight = 0.0f;
        if (sameFluid(neighborBlock, lava)) {
            neighborHeight = fluidSurfaceHeight(neighborBlock);
        } else {
            const glm::ivec3 below = neighbor + glm::ivec3(0,-1,0);
            if (available(below) && sameFluid(sample(below), lava))
                neighborHeight = fluidSurfaceHeight(sample(below));
            else
                continue;
        }
        const float contribution = own - neighborHeight;
        flow += glm::vec2(static_cast<float>(FLUID_HORIZONTAL_OFFSETS[direction].x),
                          static_cast<float>(FLUID_HORIZONTAL_OFFSETS[direction].z)) * contribution;
    }
    if (state->falling && available(origin + glm::ivec3(0, 1, 0)) &&
        isSolid(sample(origin + glm::ivec3(0, 1, 0)))) {
        flow += glm::vec2(0.0f, -6.0f);
    }
    const float length = glm::length(flow);
    return length > 1e-5f ? flow / length : glm::vec2(0.0f);
}

// Java's corner sampler: solid neighbors contribute no fluid height, air
// contributes zero, and high corners (>= .8) get a tenfold weight.
inline float fluidCornerHeight(const glm::ivec3& corner, bool lava,
                               const FluidSample& sample,
                               const FluidAvailable& available) {
    float weighted = 0.0f;
    float weight = 0.0f;
    for (int dz = -1; dz <= 0; ++dz) {
        for (int dx = -1; dx <= 0; ++dx) {
            const glm::ivec3 p = corner + glm::ivec3(dx, 0, dz);
            if (!available(p)) continue;
            const BlockId block = sample(p);
            float height = -1.0f;
            if (sameFluid(block, lava)) {
                const glm::ivec3 above = p + glm::ivec3(0, 1, 0);
                if (available(above) && sameFluid(sample(above), lava)) height = 1.0f;
                else height = fluidSurfaceHeight(block);
            } else if (!isSolid(block)) {
                height = 0.0f;
            }
            if (height < 0.0f) continue;
            const float contributionWeight = height >= 0.8f ? 10.0f : 1.0f;
            weighted += height * contributionWeight;
            weight += contributionWeight;
        }
    }
    return weight > 0.0f ? weighted / weight : 0.0f;
}
