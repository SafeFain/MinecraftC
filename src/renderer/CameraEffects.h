#pragma once

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

class CameraEffects {
public:
    void reset(const glm::dvec3& position = glm::dvec3(0.0));
    void update(const glm::dvec3& position, bool grounded, bool flying,
                float verticalVelocity, float landingSpeed, float dt);
    void onDamage(float amount);

    glm::mat4 viewTransform(bool includeMovement = true) const;
    glm::mat4 viewModelTransform() const;
    glm::vec3 translation() const { return m_motionTranslation; }
    glm::vec3 rotationDegrees() const {
        return m_motionRotationDegrees + m_hurtRotationDegrees;
    }
    float movementBlend() const { return m_movementBlend; }
    float walkPhase() const { return m_walkPhase; }
    float landingStrength() const { return m_landingStrength; }
    float trauma() const { return m_trauma; }

private:
    glm::dvec3 m_previousPosition{0.0};
    glm::vec3 m_motionTranslation{0.0f};
    glm::vec3 m_motionRotationDegrees{0.0f};
    glm::vec3 m_hurtRotationDegrees{0.0f};
    float m_walkPhase = 0.0f;
    float m_movementBlend = 0.0f;
    float m_fallBlend = 0.0f;
    float m_landingStrength = 0.0f;
    float m_landingTime = 1.0f;
    float m_trauma = 0.0f;
    float m_hurtTime = 0.0f;
    bool m_initialized = false;
};
