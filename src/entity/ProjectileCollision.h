#pragma once

#include "entity/ProjectileLogic.h"
#include "world/Block.h"
#include "Config.h"

// Subdivide only to bound voxel candidates, never to approximate the curve.
// Each interval travels at most one block on any axis, so diagonal/high-speed
// shots do not scan the entire enclosing volume. All contacts use exact roots.
template<class BlockLookup>
std::optional<double> projectileBlockHit(
    const glm::dvec3& origin, const glm::vec3& velocity, double seconds,
    BlockLookup&& blockAt) {
    if (seconds < 0.0) return std::nullopt;
    double begin = 0.0;
    do {
        const glm::dvec3 start = projectilePosition(origin, velocity, begin);
        const double verticalSpeed = velocity.y - PROJECTILE_GRAVITY * begin;
        const double speed = std::max({std::abs(static_cast<double>(velocity.x)),
            std::abs(verticalSpeed), std::abs(static_cast<double>(velocity.z))});
        const double step = 2.0 / (speed + std::sqrt(speed * speed +
            2.0 * PROJECTILE_GRAVITY));
        const double end = std::min(seconds, begin + step);
        const glm::dvec3 finish = projectilePosition(origin, velocity, end);
        glm::dvec3 minimum = glm::min(start, finish);
        glm::dvec3 maximum = glm::max(start, finish);
        const double apex = velocity.y / PROJECTILE_GRAVITY;
        if (apex > begin && apex < end)
            maximum.y = projectilePosition(origin, velocity, apex).y;
        // Include the neighbor on exact integer boundaries: collision boxes are closed.
        glm::ivec3 first(glm::ceil(minimum) - glm::dvec3(1.0));
        glm::ivec3 last(glm::floor(maximum));
        first.y = std::max(first.y, Config::WORLD_MIN_Y);
        last.y = std::min(last.y, Config::WORLD_MAX_Y - 1);
        std::optional<double> hit;
        for (int y = first.y; y <= last.y; ++y)
            for (int z = first.z; z <= last.z; ++z)
                for (int x = first.x; x <= last.x; ++x) {
                    const auto boxes = blockSelectionBoxes(blockAt(x, y, z));
                    const glm::dvec3 block(x, y, z);
                    for (uint8_t i = 0; i < boxes.count; ++i) {
                        const auto candidate = projectileAabbHit(origin, velocity,
                            block + glm::dvec3(boxes.boxes[i].min),
                            block + glm::dvec3(boxes.boxes[i].max), end);
                        if (candidate && *candidate >= begin &&
                            (!hit || *candidate < *hit))
                            hit = candidate;
                    }
                }
        if (hit) return hit;
        begin = end;
    } while (begin < seconds);
    return std::nullopt;
}
