#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include <glm/glm.hpp>

struct VoxelRaycastHit {
    glm::ivec3 blockPos{0};
    glm::ivec3 faceNormal{0};
    double distance = 0.0;
};

template <typename BlocksRay>
std::optional<VoxelRaycastHit> voxelRaycast(
    const glm::dvec3& origin, const glm::vec3& direction, double maxDistance,
    BlocksRay&& blocksRay) {
    if (!std::isfinite(maxDistance) || maxDistance < 0.0)
        return std::nullopt;

    glm::dvec3 rayDirection(direction);
    const double directionLength = glm::length(rayDirection);
    if (!std::isfinite(directionLength) || directionLength <= 1e-12)
        return std::nullopt;
    rayDirection /= directionLength;

    glm::ivec3 blockPos(glm::floor(origin));
    const glm::ivec3 step(
        rayDirection.x > 0.0 ? 1 : (rayDirection.x < 0.0 ? -1 : 0),
        rayDirection.y > 0.0 ? 1 : (rayDirection.y < 0.0 ? -1 : 0),
        rayDirection.z > 0.0 ? 1 : (rayDirection.z < 0.0 ? -1 : 0));

    const double infinity = std::numeric_limits<double>::infinity();
    const glm::dvec3 tDelta(
        step.x != 0 ? 1.0 / std::abs(rayDirection.x) : infinity,
        step.y != 0 ? 1.0 / std::abs(rayDirection.y) : infinity,
        step.z != 0 ? 1.0 / std::abs(rayDirection.z) : infinity);
    glm::dvec3 tMax(
        step.x > 0 ? (blockPos.x + 1.0 - origin.x) * tDelta.x
                   : (step.x < 0 ? (origin.x - blockPos.x) * tDelta.x : infinity),
        step.y > 0 ? (blockPos.y + 1.0 - origin.y) * tDelta.y
                   : (step.y < 0 ? (origin.y - blockPos.y) * tDelta.y : infinity),
        step.z > 0 ? (blockPos.z + 1.0 - origin.z) * tDelta.z
                   : (step.z < 0 ? (origin.z - blockPos.z) * tDelta.z : infinity));

    glm::ivec3 faceNormal(0);
    double traveled = 0.0;
    while (traveled <= maxDistance) {
        if (blocksRay(blockPos))
            return VoxelRaycastHit{blockPos, faceNormal, traveled};

        faceNormal = glm::ivec3(0);
        if (tMax.x <= tMax.y && tMax.x <= tMax.z) {
            traveled = tMax.x;
            if (traveled > maxDistance) break;
            faceNormal.x = -step.x;
            blockPos.x += step.x;
            tMax.x += tDelta.x;
        } else if (tMax.y <= tMax.x && tMax.y <= tMax.z) {
            traveled = tMax.y;
            if (traveled > maxDistance) break;
            faceNormal.y = -step.y;
            blockPos.y += step.y;
            tMax.y += tDelta.y;
        } else {
            traveled = tMax.z;
            if (traveled > maxDistance) break;
            faceNormal.z = -step.z;
            blockPos.z += step.z;
            tMax.z += tDelta.z;
        }
    }
    return std::nullopt;
}
