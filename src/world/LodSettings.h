#pragma once

#include <cstdint>

enum class LodAggressiveness : uint8_t { PowerSaver, Balanced, Fast, Extreme };
enum class LodPrecision : uint8_t { Low, Medium, High, Ultra };

struct LodSettings {
    bool enabled = true;
    int distanceChunks = 128;
    LodAggressiveness aggressiveness = LodAggressiveness::Balanced;
    LodPrecision precision = LodPrecision::Medium;

    friend bool operator==(const LodSettings& a, const LodSettings& b) {
        return a.enabled == b.enabled &&
               a.distanceChunks == b.distanceChunks &&
               a.aggressiveness == b.aggressiveness &&
               a.precision == b.precision;
    }
    friend bool operator!=(const LodSettings& a, const LodSettings& b) {
        return !(a == b);
    }
};

struct LodWorkBudget {
    int maxInFlight = 2;
    double completionMs = 1.5;
    int uploadsPerFrame = 2;
    uint64_t uploadBytesPerFrame = 4u * 1024u * 1024u;
};

inline LodWorkBudget lodWorkBudget(LodAggressiveness value) {
    switch (value) {
        case LodAggressiveness::PowerSaver:
            return {1, 0.5, 1, 2u * 1024u * 1024u};
        case LodAggressiveness::Balanced:
            return {2, 1.5, 2, 4u * 1024u * 1024u};
        case LodAggressiveness::Fast:
            return {4, 3.0, 4, 8u * 1024u * 1024u};
        case LodAggressiveness::Extreme:
            return {8, 6.0, 8, 16u * 1024u * 1024u};
    }
    return {};
}

inline int lodHorizontalQuality(LodPrecision value) {
    // Keep even the lowest preset useful as distant terrain: a cell spans at
    // most roughly distance / 64, while higher presets progressively retain
    // silhouette detail instead of merely increasing vertical span storage.
    constexpr int values[] = {64, 96, 128, 144};
    return values[static_cast<int>(value)];
}

inline int lodVerticalSpanLimit(LodPrecision value) {
    constexpr int values[] = {6, 8, 16, 24};
    return values[static_cast<int>(value)];
}
