#pragma once

#include <cstdint>
#include <limits>
#include <optional>

#include <glm/glm.hpp>

#include "world/BiomeMap.h"

template <typename Query>
std::optional<glm::ivec2> locateNearestBiome(
    const glm::ivec2& origin, Biome target, Query&& query,
    int maxDistance = 8192, int sampleStep = 32) {
    if (query(origin.x, origin.y) == target) return origin;
    if (maxDistance < 1 || sampleStep < 1) return {};

    for (int radius = sampleStep; radius <= maxDistance; radius += sampleStep) {
        std::optional<glm::ivec2> best;
        int64_t bestDistance = std::numeric_limits<int64_t>::max();
        auto consider = [&](int dx, int dz) {
            const glm::ivec2 candidate(origin.x + dx, origin.y + dz);
            if (query(candidate.x, candidate.y) != target) return;
            const int64_t distance = static_cast<int64_t>(dx) * dx +
                                     static_cast<int64_t>(dz) * dz;
            if (distance < bestDistance) {
                best = candidate;
                bestDistance = distance;
            }
        };
        for (int offset = -radius; offset <= radius; offset += sampleStep) {
            consider(offset, -radius);
            consider(offset, radius);
            if (offset != -radius && offset != radius) {
                consider(-radius, offset);
                consider(radius, offset);
            }
        }
        if (!best) continue;

        // Refine the coarse climate sample to a block coordinate inside the
        // located biome and choose the closest point in its surrounding cell.
        glm::ivec2 refined = *best;
        bestDistance = std::numeric_limits<int64_t>::max();
        for (int dz = -sampleStep; dz <= sampleStep; ++dz) {
            for (int dx = -sampleStep; dx <= sampleStep; ++dx) {
                const glm::ivec2 candidate(best->x + dx, best->y + dz);
                if (query(candidate.x, candidate.y) != target) continue;
                const int64_t ox = static_cast<int64_t>(candidate.x) - origin.x;
                const int64_t oz = static_cast<int64_t>(candidate.y) - origin.y;
                const int64_t distance = ox * ox + oz * oz;
                if (distance < bestDistance) {
                    refined = candidate;
                    bestDistance = distance;
                }
            }
        }
        return refined;
    }
    return {};
}
