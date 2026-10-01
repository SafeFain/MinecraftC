#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include "Config.h"

enum CloudFace : uint32_t {
    CloudNegativeZ = 1u << 0,
    CloudPositiveZ = 1u << 1,
    CloudNegativeX = 1u << 2,
    CloudPositiveX = 1u << 3,
    CloudPositiveY = 1u << 4,
    CloudNegativeY = 1u << 5,
};

// Backend-neutral cloud profile. Heaven uses the analytic horizon cloud sea
// in the sky shader and therefore must not enqueue ordinary voxel cloud cells.
enum class CloudLayerStyle : uint8_t {
    Overworld,
    Heaven
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
    CloudLayerStyle style = CloudLayerStyle::Overworld;
};

constexpr int CLOUD_CELL_SIZE = 16;
constexpr int MAX_CLOUD_RADIUS = 1024 / CLOUD_CELL_SIZE;
constexpr int CLOUD_LOD_RADIUS = Config::CLOUD_LOD_DISTANCE / CLOUD_CELL_SIZE;
// Conservative bound including split side faces at mixed-resolution edges.
constexpr size_t MAX_CLOUD_INSTANCES =
    10u * (2u * CLOUD_LOD_RADIUS + 1u) * (2u * CLOUD_LOD_RADIUS + 1u);

CloudView cloudView(const glm::dvec3& playerPosition, float timeSeconds,
                    int renderDistanceBlocks,
                    CloudLayerStyle style = CloudLayerStyle::Overworld);
std::vector<CloudInstance> buildCloudInstances(uint64_t worldSeed,
                                               int centerX, int centerZ,
                                               int radius,
                                               CloudLayerStyle style =
                                                   CloudLayerStyle::Overworld);
// Exact cells inside radius, progressively coarser world-aligned cells outside.
std::vector<CloudInstance> buildCloudLodInstances(uint64_t worldSeed,
    int centerX, int centerZ, int radius,
    CloudLayerStyle style = CloudLayerStyle::Overworld);
// Camera-relative central chunk origin, squared streaming radius and chunk size.
glm::vec4 cloudNearRegion(const glm::dvec3& playerPosition, int distanceChunks);
