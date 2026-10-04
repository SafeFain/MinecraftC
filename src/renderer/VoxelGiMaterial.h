#pragma once

#include "world/Block.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

inline float voxelGiSrgbToLinear(float value) {
    return value <= 0.04045f ? value / 12.92f :
        std::pow((value + 0.055f) / 1.055f, 2.4f);
}

struct VoxelGiMaterial {
    std::array<glm::vec3, 6> reflectance{};
    glm::vec3 emission{0.0f};
};
struct VoxelGiMaterialTable : std::vector<VoxelGiMaterial> {
    VoxelGiMaterialTable() : std::vector<VoxelGiMaterial>(static_cast<size_t>(BlockId::COUNT)) {}
};

// Six scalar uints have an identical 24-byte stride in C++ and GLSL std430.
// Masks use x + n*(y + n*z), with n=min(cellSize,4); no shaderInt64 needed.
struct VoxelGiAux {
    uint32_t occupiedLo = 0;
    uint32_t occupiedHi = 0;
    uint32_t coverage = 0;
    uint32_t exposureLo = 0;
    uint32_t exposureHi = 0;
    uint32_t emission = 0;
};
static_assert(sizeof(VoxelGiAux) == 24);
static_assert(offsetof(VoxelGiAux, occupiedHi) == 4);
static_assert(offsetof(VoxelGiAux, coverage) == 8);
static_assert(offsetof(VoxelGiAux, exposureLo) == 12);
static_assert(offsetof(VoxelGiAux, exposureHi) == 16);
static_assert(offsetof(VoxelGiAux, emission) == 20);

inline uint8_t voxelGiByte(float value) {
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}
inline uint32_t voxelGiRgb(const glm::vec3& value) {
    return voxelGiByte(value.r) | (uint32_t(voxelGiByte(value.g)) << 8) |
        (uint32_t(voxelGiByte(value.b)) << 16);
}
inline glm::vec3 voxelGiUnpackRgb(uint32_t value) {
    return glm::vec3(value & 255, (value >> 8) & 255, (value >> 16) & 255) / 255.0f;
}
inline uint8_t voxelGiExposure(const VoxelGiAux& value, int face) {
    return static_cast<uint8_t>((face < 4 ? value.exposureLo >> (face * 8) :
        value.exposureHi >> ((face - 4) * 8)) & 255);
}
inline void setVoxelGiExposure(VoxelGiAux& value, int face, uint8_t exposure) {
    uint32_t& word = face < 4 ? value.exposureLo : value.exposureHi;
    const int shift = (face < 4 ? face : face - 4) * 8;
    word = (word & ~(255u << shift)) | (uint32_t(exposure) << shift);
}
inline bool voxelGiOccupied(const VoxelGiAux& value, int bit) {
    return ((bit < 32 ? value.occupiedLo : value.occupiedHi) >> (bit % 32)) & 1u;
}
inline void setVoxelGiOccupied(VoxelGiAux& value, int bit) {
    (bit < 32 ? value.occupiedLo : value.occupiedHi) |= 1u << (bit % 32);
}
inline void updateVoxelGiCoverage(VoxelGiAux& value, int n) {
    for (int axis = 0; axis < 3; ++axis) {
        int columns = 0;
        for (int v = 0; v < n; ++v) for (int u = 0; u < n; ++u) {
            bool occupied = false;
            for (int t = 0; t < n; ++t) {
                const int x = axis == 0 ? t : u;
                const int y = axis == 1 ? t : axis == 0 ? u : v;
                const int z = axis == 2 ? t : v;
                occupied |= voxelGiOccupied(value, x + n * (y + n * z));
            }
            columns += occupied;
        }
        value.coverage |= uint32_t(voxelGiByte(float(columns) / (n * n))) << (axis * 8);
    }
}
