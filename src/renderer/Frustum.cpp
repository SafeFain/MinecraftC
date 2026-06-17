#include "renderer/Frustum.h"
#include <glm/gtc/matrix_access.hpp>
#include <cmath>

void Frustum::extractFromVP(const glm::mat4& vp) {
    // Gribb/Hartmann method: extract planes from combined VP matrix
    // Left plane:   row4 + row1
    // Right plane:  row4 - row1
    // Bottom plane: row4 + row2
    // Top plane:    row4 - row2
    // Near plane:   row4 + row3
    // Far plane:    row4 - row3

    auto row = [&](int i) -> glm::vec4 {
        return glm::row(vp, i);
    };

    glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);

    m_planes[0] = {glm::vec3(r3 + r0), r3.w + r0.w};  // left
    m_planes[1] = {glm::vec3(r3 - r0), r3.w - r0.w};  // right
    m_planes[2] = {glm::vec3(r3 + r1), r3.w + r1.w};  // bottom
    m_planes[3] = {glm::vec3(r3 - r1), r3.w - r1.w};  // top
    m_planes[4] = {glm::vec3(r3 + r2), r3.w + r2.w};  // near
    m_planes[5] = {glm::vec3(r3 - r2), r3.w - r2.w};  // far

    // Normalize all planes
    for (auto& p : m_planes) {
        float len = glm::length(p.normal);
        if (len > 1e-10f) {
            p.normal   /= len;
            p.distance /= len;
        }
    }
}

bool Frustum::intersectsAABB(const glm::vec3& min, const glm::vec3& max) const {
    // For each plane, compute the "p-vertex" (most-positive corner along the normal)
    // If the p-vertex is on the negative side of the plane, the AABB is outside.
    for (const auto& plane : m_planes) {
        glm::vec3 pVertex(
            plane.normal.x > 0.0f ? max.x : min.x,
            plane.normal.y > 0.0f ? max.y : min.y,
            plane.normal.z > 0.0f ? max.z : min.z
        );

        if (plane.signedDist(pVertex) < 0.0f) {
            return false;  // Outside this plane → outside frustum
        }
    }
    return true;  // Intersects or inside
}
