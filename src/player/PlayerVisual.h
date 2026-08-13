#pragma once

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
};

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
