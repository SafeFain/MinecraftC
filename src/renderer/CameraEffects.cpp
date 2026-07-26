#include "renderer/CameraEffects.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace {
constexpr float PI = 3.14159265358979323846f;
}

void CameraEffects::reset(const glm::dvec3& position) {
    m_previousPosition = position;
    m_translation = glm::vec3(0.0f);
    m_rotationDegrees = glm::vec3(0.0f);
    m_walkPhase = 0.0f;
    m_movementBlend = 0.0f;
    m_trauma = 0.0f;
    m_hurtTime = 0.0f;
    m_initialized = true;
}

void CameraEffects::update(const glm::dvec3& position, bool grounded,
                           bool flying, float dt) {
    dt = std::clamp(dt, 0.0f, 0.1f);
    if (!m_initialized) reset(position);

    const glm::dvec2 delta(position.x - m_previousPosition.x,
                           position.z - m_previousPosition.z);
    const float distance = static_cast<float>(glm::length(delta));
    m_previousPosition = position;
    const float speed = dt > 0.00001f ? distance / dt : 0.0f;
    const float targetBlend = grounded && !flying
        ? std::clamp((speed - 0.05f) / 4.2f, 0.0f, 1.0f) : 0.0f;
    const float smoothing = 1.0f - std::exp(-dt * 12.0f);
    m_movementBlend += (targetBlend - m_movementBlend) * smoothing;
    if (grounded && !flying) m_walkPhase += distance * PI * 0.735f;

    const float step = std::sin(m_walkPhase);
    const float doubleStep = std::cos(m_walkPhase * 2.0f);
    m_translation.x = step * 0.026f * m_movementBlend;
    m_translation.y = -0.018f * m_movementBlend +
                      doubleStep * 0.022f * m_movementBlend;

    m_hurtTime += dt;
    m_trauma = std::max(0.0f, m_trauma - dt * 1.35f);
    const float shake = m_trauma * m_trauma;
    const float hurtPitch = std::sin(m_hurtTime * 13.23f) * 2.8f * shake;
    const float hurtRoll = std::sin(m_hurtTime * 9.8f + 0.8f) * 3.8f * shake;
    m_rotationDegrees = {
        hurtPitch,
        std::sin(m_hurtTime * 8.33f) * 0.7f * shake,
        step * 0.48f * m_movementBlend + hurtRoll
    };
}

void CameraEffects::onDamage(float amount) {
    if (amount <= 0.0f) return;
    m_trauma = std::clamp(std::max(m_trauma, 0.28f + amount * 0.065f),
                          0.0f, 1.0f);
    m_hurtTime = 0.0f;
}

glm::mat4 CameraEffects::viewTransform() const {
    glm::mat4 effect(1.0f);
    effect = glm::rotate(effect, glm::radians(-m_rotationDegrees.z),
                         glm::vec3(0.0f, 0.0f, 1.0f));
    effect = glm::rotate(effect, glm::radians(-m_rotationDegrees.x),
                         glm::vec3(1.0f, 0.0f, 0.0f));
    effect = glm::rotate(effect, glm::radians(-m_rotationDegrees.y),
                         glm::vec3(0.0f, 1.0f, 0.0f));
    return glm::translate(effect, -m_translation);
}
