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
        Config::WORLD_MAX_Y - 1,
        static_cast<int>(std::floor(bottomY + margin)));

    float support = static_cast<float>(Config::WORLD_MIN_Y);
    for (int bx = minX; bx <= maxX; ++bx) {
        for (int bz = minZ; bz <= maxZ; ++bz) {
            for (int by = scanTop; by >= Config::WORLD_MIN_Y; --by) {
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

inline float waterVerticalVelocity(float current, bool rise, bool dive, float dt) {
    if (rise != dive)
        return rise ? Config::WATER_RISE_SPEED : -Config::WATER_DIVE_SPEED;
    return current + (-Config::WATER_SINK_SPEED - current) *
                     std::clamp(dt * 3.0f, 0.0f, 1.0f);
}

} // namespace PlayerPhysics
