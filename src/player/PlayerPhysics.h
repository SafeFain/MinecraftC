#pragma once

#include <algorithm>
#include <cmath>

#include "Config.h"
#include "world/Block.h"

namespace PlayerPhysics {

inline int movementSubsteps(float distance, float maximumStep = 0.20f) {
    return std::max(
        1, static_cast<int>(std::ceil(std::abs(distance) / maximumStep)));
}

template<typename BlockGetter>
float findSupportHeight(float px, float bottomY, float pz,
                        BlockGetter&& getBlock) {
    constexpr float margin = 0.001f;
    const float halfWidth = Config::PLAYER_WIDTH * 0.5f;
    const int minX = static_cast<int>(std::floor(px - halfWidth + margin));
    const int maxX = static_cast<int>(std::floor(px + halfWidth - margin));
    const int minZ = static_cast<int>(std::floor(pz - halfWidth + margin));
    const int maxZ = static_cast<int>(std::floor(pz + halfWidth - margin));
    const int scanTop = std::min(
        Config::CHUNK_SIZE_Y - 1,
        static_cast<int>(std::floor(bottomY + margin)));

    float support = 0.0f;
    for (int bx = minX; bx <= maxX; ++bx) {
        for (int bz = minZ; bz <= maxZ; ++bz) {
            for (int by = scanTop; by >= 0; --by) {
                const BlockId id = getBlock(bx, by, bz);
                if (!getBlockProps(id).solid) continue;
                const float top = static_cast<float>(by + 1);
                if (top <= bottomY + margin) {
                    support = std::max(support, top);
                    break;
                }
            }
        }
    }
    return support;
}

inline bool shouldAutoJump(bool enabled, bool onGround, bool movementBlocked,
                           bool currentHeadroomClear, bool targetHeadroomClear) {
    return enabled && onGround && movementBlocked &&
           currentHeadroomClear && targetHeadroomClear;
}

} // namespace PlayerPhysics
