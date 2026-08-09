#include "renderer/CloudRenderData.h"

#include <algorithm>
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
                    int renderDistanceBlocks) {
    CloudView result;
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
                                               int radius) {
    if (radius < 1 || radius > MAX_CLOUD_RADIUS)
        throw std::invalid_argument("Cloud radius is outside the supported range");
    const int diameter = radius * 2 + 1;
    std::vector<float> density(static_cast<size_t>(diameter * diameter));
    for (int z = 0; z < diameter; ++z) {
        for (int x = 0; x < diameter; ++x) {
            density[static_cast<size_t>(x + z * diameter)] = cloudDensity(
                worldSeed, centerX + x - radius, centerZ + z - radius);
        }
    }

    std::vector<CloudInstance> instances;
    instances.reserve(std::min(MAX_CLOUD_INSTANCES,
        static_cast<size_t>(diameter * diameter * 2)));
    const auto appendLayer = [&](float threshold, float y, float height) {
        constexpr int maxMergeCells = 3;
        std::vector<bool> consumed(static_cast<size_t>(diameter * diameter), false);
        const auto occupied = [&](int x, int z) {
            const size_t index = static_cast<size_t>(x + z * diameter);
            return !consumed[index] && density[index] >= threshold;
        };
        for (int z = 0; z < diameter; ++z) {
            for (int x = 0; x < diameter; ++x) {
                if (!occupied(x, z)) continue;
                int width = 1;
                while (width < maxMergeCells && x + width < diameter &&
                       occupied(x + width, z)) ++width;
                int depth = 1;
                bool canExtend = true;
                while (depth < maxMergeCells && z + depth < diameter && canExtend) {
                    for (int offset = 0; offset < width; ++offset) {
                        if (!occupied(x + offset, z + depth)) {
                            canExtend = false;
                            break;
                        }
                    }
                    if (canExtend) ++depth;
                }
                for (int dz = 0; dz < depth; ++dz) {
                    for (int dx = 0; dx < width; ++dx) {
                        consumed[static_cast<size_t>(
                            x + dx + (z + dz) * diameter)] = true;
                    }
                }
                instances.push_back({
                    static_cast<float>((x - radius) * CLOUD_CELL_SIZE), y,
                    static_cast<float>((z - radius) * CLOUD_CELL_SIZE),
                    static_cast<float>(width * CLOUD_CELL_SIZE),
                    static_cast<float>(depth * CLOUD_CELL_SIZE), height});
            }
        }
    };
    appendLayer(0.53f, 192.0f, 3.0f);
    appendLayer(0.68f, 195.0f, 2.0f);
    return instances;
}
