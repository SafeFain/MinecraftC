#pragma once

#include "renderer/RenderDevice.h"
#include <algorithm>
#include <array>
#include <cmath>

// Backend-neutral rounded geometry. Fixed subdivision bounds CPU/GPU work;
// the outer transparent ring provides coverage without another material.
inline void appendRoundedRect(std::vector<UiMeshVertex>& vertices,
                              std::vector<uint32_t>& indices,
                              float x, float y, float w, float h, float radius,
                              const glm::vec4& color, float fringe = 0.75f) {
    if (!(w > 0 && h > 0) || color.a <= 0) return;
    radius = std::clamp(radius, 0.0f, std::min(w, h) * 0.5f);
    fringe = std::clamp(fringe, 0.0f, std::min(w, h) * 0.25f);
    constexpr int segments = 8;
    constexpr int count = 4 * (segments + 1);
    constexpr float pi = 3.14159265358979323846f;
    const std::array<glm::vec2, 4> centers{{
        {x+w-radius,y+radius}, {x+w-radius,y+h-radius},
        {x+radius,y+h-radius}, {x+radius,y+radius}}};
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    vertices.push_back({{x+w*.5f,y+h*.5f},{.5f,.5f},color});
    for (int corner = 0; corner < 4; ++corner) {
        for (int i = 0; i <= segments; ++i) {
            const float angle = (-.5f + corner*.5f + i*.5f/segments)*pi;
            const glm::vec2 normal{std::cos(angle),std::sin(angle)};
            const glm::vec2 edge = centers[corner] + normal*radius;
            glm::vec4 transparent = color; transparent.a = 0;
            vertices.push_back({edge,{.5f,.5f},color});
            vertices.push_back({edge+normal*fringe,{.5f,.5f},transparent});
        }
    }
    for (int i = 0; i < count; ++i) {
        const uint32_t a = base+1+static_cast<uint32_t>(i)*2;
        const uint32_t b = base+1+static_cast<uint32_t>((i+1)%count)*2;
        indices.insert(indices.end(),{base,a,b,a,a+1,b+1,a,b+1,b});
    }
}
