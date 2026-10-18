// Pre-migration World queries retained solely for historical comparison tests.
#include "world/World.h"
#include <algorithm>
#include <cmath>
#include <limits>
int World::heavenBiomePaletteIndex(int worldX, int worldZ) const {
    if (!m_generator.isHeaven()) return 0;
    return static_cast<int>(m_generator.heavenBiomeAt(worldX, worldZ));
}

std::optional<glm::dvec3> World::heavenSkywayDestination(
    const glm::ivec3& core, bool upward) const {
    if (getBlock(core.x, core.y, core.z) != BlockId::STAR_CRYSTAL)
        return {};
    const auto destination = m_generator.heavenSkywayDestination(core, upward);
    if (!destination) return {};
    const int x = static_cast<int>(std::floor(destination->x));
    const int y = static_cast<int>(std::floor(destination->y));
    const int z = static_cast<int>(std::floor(destination->z));
    if (!generatedAt(x, z) ||
        !isFullCollisionBlock(getBlock(x, y - 1, z)) ||
        blockCollisionBoxes(getBlock(x, y, z)).count != 0 ||
        blockCollisionBoxes(getBlock(x, y + 1, z)).count != 0)
        return {};
    return destination;
}

bool World::isHeavenSkywayCore(const glm::ivec3& core) const {
    if (getBlock(core.x, core.y, core.z) != BlockId::STAR_CRYSTAL)
        return false;
    return m_generator.heavenSkywayDestination(core, true).has_value() ||
           m_generator.heavenSkywayDestination(core, false).has_value();
}

std::optional<glm::dvec3> World::starstepDestination(
    const glm::dvec3& origin, const glm::vec3& direction,
    float maxDistance) const {
    if (!m_generator.isHeaven()) return {};
    const auto hit = raycast(origin, direction, maxDistance);
    if (!hit) return {};
    std::optional<glm::dvec3> best;
    double bestDistance = std::numeric_limits<double>::max();
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int x = hit->blockPos.x + dx;
            const int z = hit->blockPos.z + dz;
            if (!generatedAt(x, z)) continue;
            int top = hit->blockPos.y;
            if (!isFullCollisionBlock(getBlock(x, top, z))) continue;
            while (top + 1 < Config::WORLD_MAX_Y &&
                   isFullCollisionBlock(getBlock(x, top + 1, z))) {
                ++top;
            }
            const int standY = top + 1;
            if (standY + 1 >= Config::WORLD_MAX_Y ||
                blockCollisionBoxes(getBlock(x, standY, z)).count != 0 ||
                blockCollisionBoxes(getBlock(x, standY + 1, z)).count != 0)
                continue;
            const glm::dvec3 candidate{x + 0.5, standY + 0.01, z + 0.5};
            const double distance = glm::distance(origin, candidate);
            if (distance > maxDistance || distance >= bestDistance) continue;
            bestDistance = distance;
            best = candidate;
        }
    }
    return best;
}
