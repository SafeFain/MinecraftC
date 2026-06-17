#include "renderer/Camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

Camera::Camera(float fovDeg, float nearPlane, float farPlane)
    : m_fovDeg(fovDeg), m_near(nearPlane), m_far(farPlane)
{
}

void Camera::updateVectors(float yawDeg, float pitchDeg) {
    m_yaw = yawDeg;
    // Clamp pitch to avoid gimbal lock at ±90° where forward ∥ worldUp,
    // causing cross(worldUp, forward) to be zero (→ NaN from normalize)
    m_pitch = glm::clamp(pitchDeg, -89.9f, 89.9f);

    float yawRad   = glm::radians(m_yaw);
    float pitchRad = glm::radians(m_pitch);

    forward = glm::vec3(
        std::cos(pitchRad) * std::sin(yawRad),
        std::sin(pitchRad),
        std::cos(pitchRad) * std::cos(yawRad)
    );
    forward = glm::normalize(forward);

    // right = worldUp × forward
    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    glm::vec3 crossResult = glm::cross(worldUp, forward);
    // Guard against degenerate cross product (shouldn't happen with clamped pitch)
    if (glm::length(crossResult) < 0.0001f) {
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
        right = glm::normalize(crossResult);
    }

    m_viewDirty = true;  // orientation changed → invalidate cached view matrix
}

glm::mat4 Camera::getViewMatrix() const {
    if (m_viewDirty) {
        m_cachedView = glm::lookAt(m_position, m_position + forward,
                                   glm::vec3(0.0f, 1.0f, 0.0f));
        m_viewDirty = false;
    }
    return m_cachedView;
}

glm::mat4 Camera::getProjectionMatrix(float aspectRatio) const {
    if (m_projDirty || aspectRatio != m_cachedAspectRatio) {
        m_cachedProjection = glm::perspective(glm::radians(m_fovDeg), aspectRatio,
                                              m_near, m_far);
        m_cachedAspectRatio = aspectRatio;
        m_projDirty = false;
    }
    return m_cachedProjection;
}

glm::mat4 Camera::getViewProjectionMatrix(float aspectRatio) const {
    return getProjectionMatrix(aspectRatio) * getViewMatrix();
}

void Camera::setAspectRatio(float aspect) {
    m_projection = glm::perspective(glm::radians(m_fovDeg), aspect, m_near, m_far);
    m_projDirty = true;  // explicit set invalidates cached projection
}
