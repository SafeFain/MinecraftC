#include "player/PlayerVisual.h"

#include "world/Block.h"
#include "world/World.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

glm::dvec3 resolveThirdPersonCamera(const World& world,
                                    const glm::dvec3& eye,
                                    const glm::vec3& lookDirection,
                                    CameraPerspective perspective,
                                    float distance) {
    if (perspective == CameraPerspective::FirstPerson || distance <= 0.0f)
        return eye;
    glm::vec3 direction = lookDirection;
    if (glm::length(direction) < 0.0001f) direction = {0.0f, 0.0f, 1.0f};
    direction = glm::normalize(direction);
    if (perspective == CameraPerspective::ThirdPersonBack) direction = -direction;

    constexpr float radius = 0.18f;
    constexpr float step = 0.08f;
    const glm::vec3 right = glm::length(glm::cross(direction, glm::vec3(0, 1, 0))) > .001f
        ? glm::normalize(glm::cross(direction, glm::vec3(0, 1, 0)))
        : glm::vec3(1, 0, 0);
    const glm::vec3 up = glm::normalize(glm::cross(right, direction));
    const std::array<glm::vec3, 5> offsets{{
        glm::vec3(0), right * radius, -right * radius, up * radius, -up * radius}};
    float resolved = 0.0f;
    for (float current = step; current <= distance; current += step) {
        bool blocked = false;
        for (const glm::vec3& offset : offsets) {
            const glm::dvec3 sample = eye + glm::dvec3(direction * current + offset);
            const BlockId block = world.getBlock(
                static_cast<int>(std::floor(sample.x)),
                static_cast<int>(std::floor(sample.y)),
                static_cast<int>(std::floor(sample.z)));
            if (isSolid(block)) { blocked = true; break; }
        }
        if (blocked) break;
        resolved = current;
    }
    return eye + glm::dvec3(direction * resolved);
}
