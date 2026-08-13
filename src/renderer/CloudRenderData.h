#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

enum CloudFace : uint32_t {
    CloudNegativeZ = 1u << 0,
    CloudPositiveZ = 1u << 1,
    CloudNegativeX = 1u << 2,
    CloudPositiveX = 1u << 3,
    CloudPositiveY = 1u << 4,
    CloudNegativeY = 1u << 5,
};

constexpr uint32_t CLOUD_ALL_FACES = (1u << 6) - 1u;

struct CloudInstance {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float width = 0.0f, depth = 0.0f, height = 0.0f;
    uint32_t visibleFaces = CLOUD_ALL_FACES;
};

struct CloudView {
    int radius = 1;
    int centerX = 0;
    int centerZ = 0;
    glm::vec3 origin{0.0f};
};

constexpr int CLOUD_CELL_SIZE = 16;
constexpr int MAX_CLOUD_RADIUS = 1024 / CLOUD_CELL_SIZE;
constexpr size_t MAX_CLOUD_INSTANCES = 2u * 129u * 129u;

CloudView cloudView(const glm::dvec3& playerPosition, float timeSeconds,
                    int renderDistanceBlocks);
std::vector<CloudInstance> buildCloudInstances(uint64_t worldSeed,
                                               int centerX, int centerZ,
                                               int radius);
