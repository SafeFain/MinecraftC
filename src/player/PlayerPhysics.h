#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Config.h"
#include "world/Block.h"

namespace PlayerPhysics {

enum class Pose : uint8_t { Standing, Crouching, Swimming, Crawling };

struct Dimensions {
    float width = Config::PLAYER_WIDTH;
    float height = Config::PLAYER_HEIGHT;
    float eyeHeight = Config::EYE_HEIGHT;
};

inline Dimensions dimensions(Pose pose) {
    switch (pose) {
        case Pose::Crouching:
            return {Config::PLAYER_WIDTH, Config::PLAYER_CROUCH_HEIGHT,
                    Config::PLAYER_CROUCH_EYE_HEIGHT};
        case Pose::Swimming:
        case Pose::Crawling:
            return {Config::PLAYER_WIDTH, Config::PLAYER_SWIM_HEIGHT,
                    Config::PLAYER_SWIM_EYE_HEIGHT};
        case Pose::Standing: break;
    }
    return {};
}

inline Pose resolvePose(Pose desired, bool desiredFits, bool crouchingFits) {
    if (desiredFits) return desired;
    return crouchingFits ? Pose::Crouching : Pose::Crawling;
}

inline float approachEyeHeight(float current, float target, float dt) {
    const float ticks = std::max(0.0f, dt * 20.0f);
    return target + (current - target) * std::pow(0.5f, ticks);
}

inline glm::vec2 javaMovementInput(glm::vec2 input, bool movingSlowly) {
    const float initialLength = glm::length(input);
    if (initialLength > 1.0f) input /= initialLength;
    if (movingSlowly) input *= Config::SNEAK_INPUT_FACTOR;
    const float length = glm::length(input);
    if (length <= 0.0f) return input;
    const glm::vec2 direction = input / length;
    const float ax = std::abs(direction.x);
    const float ay = std::abs(direction.y);
    const float ratio = ay > ax ? ax / ay : (ax > 0.0f ? ay / ax : 0.0f);
    const float squareDistance = std::sqrt(1.0f + ratio * ratio);
    return direction * std::min(1.0f, length * squareDistance);
}

template<typename Supported>
glm::vec2 backOffFromEdge(glm::vec2 movement, float stepHeight,
                          Supported&& supported) {
    constexpr float increment = 0.05f;
    const float stepX = std::copysign(increment, movement.x);
    const float stepZ = std::copysign(increment, movement.y);
    while (movement.x != 0.0f && !supported(movement.x, 0.0f, stepHeight))
        movement.x = std::abs(movement.x) <= increment ? 0.0f : movement.x - stepX;
    while (movement.y != 0.0f && !supported(0.0f, movement.y, stepHeight))
        movement.y = std::abs(movement.y) <= increment ? 0.0f : movement.y - stepZ;
    while (movement.x != 0.0f && movement.y != 0.0f &&
           !supported(movement.x, movement.y, stepHeight)) {
        movement.x = std::abs(movement.x) <= increment ? 0.0f : movement.x - stepX;
        movement.y = std::abs(movement.y) <= increment ? 0.0f : movement.y - stepZ;
    }
    return movement;
}

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
                const BlockCollisionBoxes boxes = blockCollisionBoxes(id);
                for (uint8_t i = 0; i < boxes.count; ++i) {
                    const BlockCollisionBox& box = boxes.boxes[i];
                    if (px + halfWidth <= bx + box.min.x ||
                        px - halfWidth >= bx + box.max.x ||
                        pz + halfWidth <= bz + box.min.z ||
                        pz - halfWidth >= bz + box.max.z) continue;
                    const float top = static_cast<float>(by) + box.max.y;
                    if (top <= bottomY + margin)
                        support = std::max(support, top);
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
