#pragma once

#include <array>
#include <glm/glm.hpp>

class Frustum {
public:
    Frustum() = default;

    // Extract 6 planes from a combined View-Projection matrix
    void extractFromVP(const glm::mat4& vp);

    // Test if an axis-aligned bounding box intersects the frustum
    bool intersectsAABB(const glm::vec3& min, const glm::vec3& max) const;

private:
    struct Plane {
        glm::vec3 normal;
        float     distance;  // signed distance from origin along normal

        float signedDist(const glm::vec3& point) const {
            return glm::dot(normal, point) + distance;
        }
    };

    std::array<Plane, 6> m_planes{};
};
