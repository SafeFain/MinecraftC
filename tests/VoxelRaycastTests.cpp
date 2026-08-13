#include "world/VoxelRaycast.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <tuple>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

using Cell = std::tuple<int, int, int>;
}

int main() {
    std::set<Cell> solid;
    const auto trace = [&](const glm::dvec3& origin, const glm::vec3& direction,
                           double distance) {
        return voxelRaycast(origin, direction, distance,
            [&](const glm::ivec3& block) {
                return solid.count({block.x, block.y, block.z}) != 0;
            });
    };

    solid.insert({1, 0, 0});
    require(!trace({0.9, 0.5, 0.5}, {1.0f, 0.0f, 0.0f}, 0.05),
            "ray crossed a wall beyond its short maximum distance");
    const auto shortHit = trace(
        {0.9, 0.5, 0.5}, {2.0f, 0.0f, 0.0f}, 0.11);
    require(shortHit && shortHit->blockPos == glm::ivec3(1, 0, 0) &&
            shortHit->faceNormal == glm::ivec3(-1, 0, 0),
            "sub-third-block ray missed the adjacent wall or was not normalized");

    solid.clear();
    solid.insert({0, 1, 0});
    require(trace({0.5, 0.2, 0.5}, {0.01f, 3.0f, 0.0f}, 1.0).has_value(),
            "steep sight ray passed through a floor");
    require(!trace({0.5, 0.2, 0.5}, {0.01f, 3.0f, 0.0f}, 0.79),
            "steep sight ray inspected beyond its endpoint");

    solid.clear();
    solid.insert({-2, 0, 0});
    const auto negativeHit = trace(
        {-0.2, 0.5, 0.5}, {-1.0f, 0.0f, 0.0f}, 2.0);
    require(negativeHit && negativeHit->blockPos == glm::ivec3(-2, 0, 0),
            "negative-coordinate ray did not use mathematical voxel floors");

    solid.clear();
    require(!trace({0.5, 0.5, 0.5}, {1.0f, 1.0f, 1.0f}, 20.0),
            "unobstructed diagonal ray reported a false hit");
    require(!trace({0.5, 0.5, 0.5}, glm::vec3(0.0f), 20.0),
            "zero-length direction produced a hit");

    std::cout << "Voxel raycast tests passed\n";
}
