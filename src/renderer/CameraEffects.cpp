#include "renderer/CameraEffects.h"
#include "Config.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace {
constexpr float PI = 3.14159265358979323846f;
constexpr float FOOTSTEPS_PER_BLOCK = 0.44f;
constexpr float LANDING_DURATION = 0.40f;
constexpr float WALK_BOB_HORIZONTAL = 0.035f;
constexpr float WALK_BOB_VERTICAL = 0.060f;
constexpr float WALK_BOB_PITCH_DEGREES = 0.40f;
constexpr float WALK_BOB_ROLL_DEGREES = 0.65f;
}

void CameraEffects::reset(const glm::dvec3& position) {
    m_previousPosition = position;
    m_motionTranslation = glm::vec3(0.0f);
    m_motionRotationDegrees = glm::vec3(0.0f);
    m_hurtRotationDegrees = glm::vec3(0.0f);
    m_walkPhase = 0.0f;
    m_movementBlend = 0.0f;
    m_fallBlend = 0.0f;
    m_landingStrength = 0.0f;
    m_landingTime = LANDING_DURATION;
    m_trauma = 0.0f;
    m_hurtTime = 0.0f;
    m_initialized = true;
}

void CameraEffects::update(const glm::dvec3& position, bool grounded,
                           bool flying, float verticalVelocity,
                           float landingSpeed, float dt) {
    dt = std::clamp(dt, 0.0f, 0.1f);
    if (!m_initialized) reset(position);

    const glm::dvec3 worldDelta = position - m_previousPosition;
    if (std::hypot(worldDelta.x, worldDelta.z) > 8.0 ||
        std::abs(worldDelta.y) > 32.0) {
        reset(position);
        return;
    }
    const float distance = static_cast<float>(
        std::hypot(worldDelta.x, worldDelta.z));
    m_previousPosition = position;
    const float speed = dt > 0.00001f ? distance / dt : 0.0f;

    const float targetMovement = grounded && !flying
        ? std::clamp(speed / Config::PLAYER_SPEED, 0.0f, 1.0f) : 0.0f;
    const float movementResponse = targetMovement > m_movementBlend ? 10.0f : 14.0f;
    m_movementBlend += (targetMovement - m_movementBlend) *
        (1.0f - std::exp(-dt * movementResponse));
    if (grounded && !flying)
        m_walkPhase += distance * 2.0f * PI * FOOTSTEPS_PER_BLOCK;

    const float footPlant = 0.5f - 0.5f * std::cos(m_walkPhase);
    const float stride = std::sin(m_walkPhase * 0.5f);
    const float bobX = stride * WALK_BOB_HORIZONTAL * m_movementBlend;
    const float bobY = -footPlant * WALK_BOB_VERTICAL * m_movementBlend;
    const float bobPitch = std::cos(m_walkPhase) *
        WALK_BOB_PITCH_DEGREES * m_movementBlend;
    const float bobRoll = stride * WALK_BOB_ROLL_DEGREES * m_movementBlend;

    const float targetFall = !grounded && !flying
        ? std::clamp((-verticalVelocity - 4.0f) / 14.0f, 0.0f, 1.0f)
        : 0.0f;
    const float fallResponse = targetFall > m_fallBlend ? 7.0f : 12.0f;
    m_fallBlend += (targetFall - m_fallBlend) *
        (1.0f - std::exp(-dt * fallResponse));

    if (!flying && landingSpeed > 3.5f) {
        m_landingStrength = std::clamp(
            (landingSpeed - 3.5f) / (18.0f - 3.5f), 0.0f, 1.0f);
        m_landingTime = 0.0f;
    } else {
        m_landingTime = std::min(LANDING_DURATION, m_landingTime + dt);
        if (m_landingTime >= LANDING_DURATION) m_landingStrength = 0.0f;
    }
    const float landingWave = m_landingStrength *
        std::exp(-8.0f * m_landingTime) * std::cos(12.0f * m_landingTime);

    m_motionTranslation = {
        bobX,
        bobY + m_fallBlend * 0.045f - landingWave * 0.060f,
        0.0f
    };
    m_motionRotationDegrees = {
        bobPitch + m_fallBlend * 0.65f + landingWave * 1.0f,
        0.0f,
        bobRoll
    };

    m_hurtTime += dt;
    m_trauma = std::max(0.0f, m_trauma - dt * 1.35f);
    const float shake = m_trauma * m_trauma;
    m_hurtRotationDegrees = {
        std::sin(m_hurtTime * 13.23f) * 2.8f * shake,
        std::sin(m_hurtTime * 8.33f) * 0.7f * shake,
        std::sin(m_hurtTime * 9.8f + 0.8f) * 3.8f * shake
    };
}

void CameraEffects::onDamage(float amount) {
    if (amount <= 0.0f) return;
    m_trauma = std::clamp(std::max(m_trauma, 0.28f + amount * 0.065f),
                          0.0f, 1.0f);
    m_hurtTime = 0.0f;
}

glm::mat4 CameraEffects::viewTransform(bool includeMovement) const {
    const glm::vec3 rotation = m_hurtRotationDegrees +
        (includeMovement ? m_motionRotationDegrees : glm::vec3(0.0f));
    glm::mat4 effect(1.0f);
    effect = glm::rotate(effect, glm::radians(-rotation.z),
                         glm::vec3(0.0f, 0.0f, 1.0f));
    effect = glm::rotate(effect, glm::radians(-rotation.x),
                         glm::vec3(1.0f, 0.0f, 0.0f));
    effect = glm::rotate(effect, glm::radians(-rotation.y),
                         glm::vec3(0.0f, 1.0f, 0.0f));
    return glm::translate(effect, includeMovement
        ? -m_motionTranslation : glm::vec3(0.0f));
}

glm::mat4 CameraEffects::viewModelTransform() const {
    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, {
        -m_motionTranslation.x * 0.45f,
        -m_motionTranslation.y * 0.32f,
        0.0f});
    transform = glm::rotate(
        transform, glm::radians(-m_motionRotationDegrees.x * 0.30f),
        glm::vec3(1.0f, 0.0f, 0.0f));
    return glm::rotate(
        transform, glm::radians(-m_motionRotationDegrees.z * 0.40f),
        glm::vec3(0.0f, 0.0f, 1.0f));
}
