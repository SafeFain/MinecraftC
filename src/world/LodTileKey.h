#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

struct LodTileKey {
    int32_t x = 0;
    int32_t z = 0;
    uint8_t level = 0;

    friend bool operator==(const LodTileKey& a, const LodTileKey& b) {
        return a.x == b.x && a.z == b.z && a.level == b.level;
    }
};

struct LodTileKeyHash {
    size_t operator()(const LodTileKey& key) const {
        size_t value = std::hash<int32_t>{}(key.x);
        value ^= std::hash<int32_t>{}(key.z) + 0x9e3779b9u +
                 (value << 6) + (value >> 2);
        value ^= std::hash<uint8_t>{}(key.level) + 0x9e3779b9u +
                 (value << 6) + (value >> 2);
        return value;
    }
};
