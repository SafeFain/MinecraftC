#pragma once

#include "renderer/Shadow.h"
#include "renderer/GameRenderer.h"
#include "renderer/backend/opengl/OpenGLGlObjects.h"

#include <glm/glm.hpp>
#include <vector>

class Renderer;

// Owns the cascaded shadow mapping subsystem: the depth atlas texture, the
// shadow framebuffer, cascade state, and the quality/update tracking. render()
// performs the whole shadow pass (resource rebuild on quality change, cascade
// update, draw) and reads renderer state (environment, chunk mesh storage,
// shadow shader, block atlas, scene target, stats, window) through the
// Renderer& back-reference granted by the friend declaration.
//
// The texture and framebuffer are RAII-owned (GlTexture/GlFramebuffer) so the
// quality-change rebuild and destruction cannot leak GPU objects.

class OpenGLShadowRenderer {
public:
    explicit OpenGLShadowRenderer(Renderer& renderer);
    ~OpenGLShadowRenderer() = default;
    OpenGLShadowRenderer(const OpenGLShadowRenderer&) = delete;
    OpenGLShadowRenderer& operator=(const OpenGLShadowRenderer&) = delete;

    void render(ShadowQuality quality, const glm::mat4& inverseViewProjection,
                const glm::mat4& view, const glm::dvec3& worldOrigin,
                const std::vector<ShadowChunkSubmission>& chunks);

    unsigned int texture() const { return m_shadowTexture.name(); }
    const ShadowCascades& cascades() const { return m_shadowCascades; }

private:
    Renderer& m_renderer;
    GlTexture m_shadowTexture;
    GlFramebuffer m_shadowFramebuffer;
    ShadowQuality m_shadowQuality = ShadowQuality::Off;
    ShadowCascades m_shadowCascades{};
    ShadowCascades m_shadowBaseCascades{};
    double m_lastShadowUpdateSeconds = -1.0;
    glm::dvec3 m_lastShadowWorldOrigin{0.0};
    glm::vec3 m_lastShadowDirection{0.0f};
};
