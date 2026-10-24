#pragma once

#include "game/GameRules.h"

// Native gameplay policy is independent of optional content providers.
struct DimensionRules {
    int minimumY = -64;
    int maximumY = 320; // exclusive
    bool skyLight = true;
    bool ultrawarm = false;
    bool bedsExplode = false;
    bool respawnAnchorsWork = false;
    double coordinateScale = 1.0;
    bool weather = true;
    bool contains(int y) const { return y >= minimumY && y < maximumY; }
};

inline DimensionRules dimensionRules(DimensionId dimension) {
    if (dimension == DimensionId::Nether)
        return {0, 256, false, true, true, true, 8.0, false};
    return {};
}
