#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera {
public:
    Camera(float fovDeg, float nearPlane, float farPlane);

    void updateVectors(float yawDeg, float pitchDeg);

    // Matrices (cached — only recomputed when camera moves/resizes)
    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix(float aspectRatio) const;
    glm::mat4 getViewProjectionMatrix(float aspectRatio) const;

    // View matrix + projection combined (when aspect is fixed)
    void setAspectRatio(float aspect);
    const glm::mat4& getProjection() const { return m_projection; }

    // Position setter (marks view dirty)
    void setPosition(const glm::vec3& pos) { m_position = pos; m_viewDirty = true; }

    // Position and orientation
    glm::vec3 m_position{0.0f, 50.0f, 0.0f};
    glm::vec3 forward{0.0f, 0.0f, 1.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};

    float yawDeg() const   { return m_yaw; }
    float pitchDeg() const { return m_pitch; }

private:
    float m_fovDeg, m_near, m_far;
    float m_yaw = 0.0f, m_pitch = 0.0f;
    glm::mat4 m_projection{1.0f};

    // Cached matrices (mutable for lazy recomputation in const getters)
    mutable glm::mat4 m_cachedView{1.0f};
    mutable glm::mat4 m_cachedProjection{1.0f};
    mutable float m_cachedAspectRatio = -1.0f;
    mutable bool m_viewDirty = true;
    mutable bool m_projDirty = true;
};
