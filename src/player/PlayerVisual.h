#pragma once

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include "player/PlayerPhysics.h"

class World;

enum class CameraPerspective : uint8_t {
    FirstPerson,
    ThirdPersonBack,
    ThirdPersonFront
};

struct PlayerVisualState {
    glm::vec3 velocity{0.0f};
    bool grounded = false;
    bool sprinting = false;
    uint32_t swingSequence = 0;
    float swingProgress = 1.0f;
    bool sleeping = false;
    float sleepProgress = 0.0f;
    float attackStrength = 1.0f;
    PlayerPhysics::Pose pose = PlayerPhysics::Pose::Standing;
    bool bowCharging = false;
    float bowCharge = 0.0f;
    bool blocking = false;
};

// The authored 0.72-block arm extends down from its shoulder pivot.
// This point lies inside the hand end, rather than halfway along the forearm.
inline glm::vec3 playerWristOffset() { return {0.0f,-0.64f,0.0f}; }
struct PlayerArmGripPose {
    glm::quat rotation{1,0,0,0};
    glm::vec3 scale{1};
};
inline PlayerArmGripPose playerArmGripPose(glm::vec3 shoulder,glm::vec3 grip) {
    const glm::vec3 delta=grip-shoulder;
    const float length=glm::length(delta);
    if(length<0.0001f)return {};
    const glm::vec3 d=delta/length;
    const glm::quat q= d.y>.9999f ? glm::angleAxis(glm::radians(180.0f),glm::vec3(1,0,0)) :
        glm::normalize(glm::quat(1.0f-d.y,-d.z,0.0f,d.x));
    return {q,{1,length/.64f,1}};
}

enum class PlayerLocomotion : uint8_t {
    Idle, Walk, Run, Jump, Fall, SneakIdle, SneakWalk, Swim, Crawl
};

inline PlayerLocomotion playerLocomotion(const PlayerVisualState& state) {
    if (state.pose == PlayerPhysics::Pose::Swimming)
        return PlayerLocomotion::Swim;
    if (state.pose == PlayerPhysics::Pose::Crawling)
        return PlayerLocomotion::Crawl;
    if (state.pose == PlayerPhysics::Pose::Crouching)
        return std::hypot(state.velocity.x, state.velocity.z) < 0.08f
            ? PlayerLocomotion::SneakIdle : PlayerLocomotion::SneakWalk;
    if (!state.grounded)
        return state.velocity.y >= 0.0f
            ? PlayerLocomotion::Jump : PlayerLocomotion::Fall;
    if (std::hypot(state.velocity.x, state.velocity.z) < 0.08f)
        return PlayerLocomotion::Idle;
    return state.sprinting ? PlayerLocomotion::Run : PlayerLocomotion::Walk;
}

inline bool sprintViewEffectActive(const PlayerVisualState& state,
                                   CameraPerspective perspective,
                                   bool flying) {
    return perspective == CameraPerspective::FirstPerson && !flying &&
        state.pose != PlayerPhysics::Pose::Crouching &&
        state.pose != PlayerPhysics::Pose::Crawling &&
        state.sprinting && std::hypot(state.velocity.x, state.velocity.z) >= 0.08f;
}

inline float dynamicViewFov(float baseFov, float sprintBoost,
                            float bowReduction,
                            const PlayerVisualState& state,
                            CameraPerspective perspective, bool flying,
                            float bowCharge) {
    float target = baseFov;
    if (sprintViewEffectActive(state, perspective, flying))
        target += sprintBoost;
    if (perspective == CameraPerspective::FirstPerson)
        target -= bowReduction * std::clamp(bowCharge, 0.0f, 1.0f);
    return target;
}

inline CameraPerspective nextPerspective(CameraPerspective perspective) {
    switch (perspective) {
        case CameraPerspective::FirstPerson: return CameraPerspective::ThirdPersonBack;
        case CameraPerspective::ThirdPersonBack: return CameraPerspective::ThirdPersonFront;
        case CameraPerspective::ThirdPersonFront: return CameraPerspective::FirstPerson;
    }
    return CameraPerspective::FirstPerson;
}

// Resolves a camera from the eye toward the requested third-person offset.
// A small multi-ray footprint prevents the camera center from slipping through
// wall corners; returned coordinates remain in world space.
glm::dvec3 resolveThirdPersonCamera(const World& world,
                                    const glm::dvec3& eye,
                                    const glm::vec3& lookDirection,
                                    CameraPerspective perspective,
                                    float distance = 4.0f);

// Shared first-person arm/held-item swing. Zero is the resting pose and the
// curve returns exactly to zero at progress 1.
inline glm::mat4 firstPersonSwingTransform(float progress) {
    progress = std::clamp(progress, 0.0f, 1.0f);
    if (progress <= 0.0f || progress >= 1.0f) return glm::mat4(1.0f);
    const float swing = std::sin(std::sqrt(progress) * 3.14159265358979323846f);
    const float dip = std::sin(progress * 3.14159265358979323846f);
    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, {-0.20f * swing, -0.28f * dip,
                                            -0.10f * swing});
    transform = glm::rotate(transform, glm::radians(-58.0f * swing),
                            glm::vec3(0, 1, 0));
    return glm::rotate(transform, glm::radians(-34.0f * dip),
                       glm::vec3(1, 0, 0));
}
