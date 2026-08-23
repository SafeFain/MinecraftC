#include "renderer/CloudRenderData.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace {
uint64_t cloudHash(uint64_t seed, int x, int z) {
    uint64_t value = seed ^ static_cast<uint64_t>(static_cast<int64_t>(x)) *
        0x9E3779B97F4A7C15ULL;
    value ^= static_cast<uint64_t>(static_cast<int64_t>(z)) *
             0xD1B54A32D192ED03ULL;
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

int floorDiv(int value, int divisor) {
    int quotient = value / divisor;
    if (value % divisor < 0) --quotient;
    return quotient;
}

float cloudRandom(uint64_t seed, int x, int z) {
    return static_cast<float>(cloudHash(seed, x, z) & 0xFFFFFFULL) /
           static_cast<float>(0xFFFFFFULL);
}

float cloudValueNoise(uint64_t seed, int x, int z, int scale) {
    const int x0 = floorDiv(x, scale);
    const int z0 = floorDiv(z, scale);
    float tx = static_cast<float>(x - x0 * scale) / static_cast<float>(scale);
    float tz = static_cast<float>(z - z0 * scale) / static_cast<float>(scale);
    tx = tx * tx * (3.0f - 2.0f * tx);
    tz = tz * tz * (3.0f - 2.0f * tz);
    const float a = cloudRandom(seed, x0, z0);
    const float b = cloudRandom(seed, x0 + 1, z0);
    const float c = cloudRandom(seed, x0, z0 + 1);
    const float d = cloudRandom(seed, x0 + 1, z0 + 1);
    return (a + (b - a) * tx) * (1.0f - tz) +
           (c + (d - c) * tx) * tz;
}

float cloudDensity(uint64_t seed, int x, int z) {
    const float broad = cloudValueNoise(seed, x, z, 8);
    const float detail = cloudValueNoise(
        seed ^ 0xA0761D6478BD642FULL, x + 37, z - 53, 4);
    return broad * 0.70f + detail * 0.30f;
}
} // namespace

CloudView cloudView(const glm::dvec3& playerPosition, float timeSeconds,
                    int renderDistanceBlocks, CloudLayerStyle style) {
    CloudView result;
    result.style = style;
    result.radius = std::clamp(
        (renderDistanceBlocks + CLOUD_CELL_SIZE - 1) / CLOUD_CELL_SIZE,
        1, MAX_CLOUD_RADIUS);
    const double drift = static_cast<double>(timeSeconds) * 0.8;
    result.centerX = static_cast<int>(std::floor(
        (playerPosition.x - drift) / CLOUD_CELL_SIZE));
    result.centerZ = static_cast<int>(std::floor(
        playerPosition.z / CLOUD_CELL_SIZE));
    result.origin = {
        static_cast<float>(static_cast<double>(result.centerX) * CLOUD_CELL_SIZE +
                           drift - playerPosition.x), 0.0f,
        static_cast<float>(static_cast<double>(result.centerZ) * CLOUD_CELL_SIZE -
                           playerPosition.z)};
    return result;
}

std::vector<CloudInstance> buildCloudInstances(uint64_t worldSeed,
                                               int centerX, int centerZ,
                                               int radius,
                                               CloudLayerStyle style) {
    if (radius < 1 || radius > MAX_CLOUD_RADIUS)
        throw std::invalid_argument("Cloud radius is outside the supported range");
    if (style == CloudLayerStyle::Heaven) return {};
    const int diameter = radius * 2 + 1;
    std::vector<float> density(static_cast<size_t>(diameter * diameter));
    for (int z = 0; z < diameter; ++z) {
        for (int x = 0; x < diameter; ++x) {
            density[static_cast<size_t>(x + z * diameter)] = cloudDensity(
                worldSeed, centerX + x - radius, centerZ + z - radius);
        }
    }

    constexpr std::array<float, 2> thresholds{0.53f, 0.68f};
    std::array<std::vector<bool>, 2> occupied;
    for (size_t layer = 0; layer < occupied.size(); ++layer) {
        occupied[layer].resize(density.size());
        for (size_t index = 0; index < density.size(); ++index)
            occupied[layer][index] = density[index] >= thresholds[layer];
    }

    std::vector<CloudInstance> instances;
    instances.reserve(std::min(MAX_CLOUD_INSTANCES,
        static_cast<size_t>(diameter * diameter * 2)));
    const auto appendLayer = [&](size_t layer, float y, float height) {
        const auto isOccupied = [&](int x, int z) {
            return x >= 0 && x < diameter && z >= 0 && z < diameter &&
                occupied[layer][static_cast<size_t>(x + z * diameter)];
        };
        for (int z = 0; z < diameter; ++z) {
            for (int x = 0; x < diameter; ++x) {
                const size_t index = static_cast<size_t>(x + z * diameter);
                if (!occupied[layer][index]) continue;
                uint32_t visibleFaces = 0;
                if (!isOccupied(x, z - 1)) visibleFaces |= CloudNegativeZ;
                if (!isOccupied(x, z + 1)) visibleFaces |= CloudPositiveZ;
                if (!isOccupied(x - 1, z)) visibleFaces |= CloudNegativeX;
                if (!isOccupied(x + 1, z)) visibleFaces |= CloudPositiveX;
                const bool occupiedAbove = layer + 1 < occupied.size() &&
                    occupied[layer + 1][index];
                const bool occupiedBelow = layer > 0 && occupied[layer - 1][index];
                if (!occupiedAbove) visibleFaces |= CloudPositiveY;
                if (!occupiedBelow) visibleFaces |= CloudNegativeY;
                instances.push_back({
                    static_cast<float>((x - radius) * CLOUD_CELL_SIZE), y,
                    static_cast<float>((z - radius) * CLOUD_CELL_SIZE),
                    static_cast<float>(CLOUD_CELL_SIZE),
                    static_cast<float>(CLOUD_CELL_SIZE), height, visibleFaces});
            }
        }
    };
    appendLayer(0, 192.0f, 3.0f);
    appendLayer(1, 195.0f, 2.0f);
    return instances;
}
