#pragma once

#include <algorithm>
#include <cmath>

#include "Config.h"
#include "world/Block.h"

namespace PlayerPhysics {

struct VerticalMotion {
    float displacement = 0.0f;
    float velocity = 0.0f;
};

inline VerticalMotion integrateGravity(float velocity, float gravity, float dt) {
    dt = std::max(dt, 0.0f);
    gravity = std::max(gravity, 0.0f);
    const float nextVelocity = velocity - gravity * dt;
    return {(velocity + nextVelocity) * 0.5f * dt, nextVelocity};
}

inline glm::vec2 horizontalVelocity(const glm::dvec3& previous,
                                    const glm::dvec3& current, float dt) {
    if (dt <= 0.00001f) return glm::vec2(0.0f);
    return glm::vec2(current.x - previous.x, current.z - previous.z) / dt;
}

struct HurtImmunity {
    float remaining = 0.0f;
    float lastDamage = 0.0f;
};

inline void tickHurtImmunity(HurtImmunity& immunity, float dt) {
    immunity.remaining = std::max(0.0f, immunity.remaining - std::max(dt, 0.0f));
    if (immunity.remaining == 0.0f) immunity.lastDamage = 0.0f;
}

inline float damageAfterImmunity(HurtImmunity& immunity, float damage,
                                 float immunitySeconds) {
    if (damage <= 0.0f) return 0.0f;
    float accepted = damage;
    if (immunity.remaining > 0.0f) {
        if (damage <= immunity.lastDamage) return 0.0f;
        accepted = damage - immunity.lastDamage;
    }
    immunity.remaining = std::max(immunitySeconds, 0.0f);
    immunity.lastDamage = damage;
    return accepted;
}

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
                const float top = static_cast<float>(by) +
                                  blockCollisionHeight(id);
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
