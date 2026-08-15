#include "renderer/backend/opengl/OpenGLShadowRenderer.h"
#include "renderer/Renderer.h"
#include "renderer/backend/opengl/OpenGLDebug.h"
#include "renderer/backend/opengl/OpenGLProcs.h"
#include "renderer/backend/opengl/OpenGLSceneTarget.h"
#include "debug/Log.h"
#include "Config.h"
#include "core/RuntimeClock.h"

#include <algorithm>
#include <stdexcept>

OpenGLShadowRenderer::OpenGLShadowRenderer(Renderer& renderer) : m_renderer(renderer) {}

void OpenGLShadowRenderer::render(ShadowQuality quality,
                                  const glm::mat4& inverseViewProjection,
                                  const glm::mat4& view,
                                  const glm::dvec3& worldOrigin,
                                  const std::vector<ShadowChunkSubmission>& chunks) {
    const bool enabled = quality != ShadowQuality::Off &&
        m_renderer.m_environment.daylight >= 0.12f &&
        m_renderer.m_environment.directIntensity >= 0.08f;
    if (!enabled || chunks.empty()) {
        m_shadowCascades = {};
        m_shadowBaseCascades = {};
        return;
    }
    const ShadowConfig config = shadowConfig(quality);
    auto bindFramebuffer = glProc<BindFramebufferFn>("glBindFramebuffer");
    auto framebufferTexture = glProc<FramebufferTexture2DFn>("glFramebufferTexture2D");
    auto checkFramebuffer = glProc<CheckFramebufferStatusFn>("glCheckFramebufferStatus");
    auto genFramebuffers = glProc<GenFramebuffersFn>("glGenFramebuffers");
    if (!bindFramebuffer || !framebufferTexture || !checkFramebuffer || !genFramebuffers) {
        m_shadowCascades = {};
        return;
    }
    const bool qualityChanged = quality != m_shadowQuality;
    if (qualityChanged) {
        m_shadowTexture.reset();
        if (!m_shadowFramebuffer) m_shadowFramebuffer.allocate();
        const int columns = config.cascadeCount == 1 ? 1 : 2;
        const int rows = (config.cascadeCount + columns - 1) / columns;
        m_shadowTexture.allocate();
        GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_shadowTexture.name()));
        GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24_VALUE,
            config.resolution * columns, config.resolution * rows, 0,
            GL_DEPTH_COMPONENT_VALUE, GL_UNSIGNED_INT, nullptr));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_VALUE));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST_VALUE));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE_VALUE));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE_VALUE));
        bindFramebuffer(GL_FRAMEBUFFER_VALUE, m_shadowFramebuffer.name());
        framebufferTexture(GL_FRAMEBUFFER_VALUE, GL_DEPTH_ATTACHMENT_VALUE,
                           GL_TEXTURE_2D, m_shadowTexture.name(), 0);
        if (auto drawBuffer = glProc<DrawBufferFn>("glDrawBuffer")) drawBuffer(GL_NONE_VALUE);
        if (auto readBuffer = glProc<ReadBufferFn>("glReadBuffer")) readBuffer(GL_NONE_VALUE);
        if (checkFramebuffer(GL_FRAMEBUFFER_VALUE) != GL_FRAMEBUFFER_COMPLETE_VALUE)
            throw std::runtime_error("OpenGL shadow framebuffer is incomplete");
        bindFramebuffer(GL_FRAMEBUFFER_VALUE, 0);
        m_shadowQuality = quality;
    }
    const double nowSeconds = RuntimeClock::seconds(RuntimeClock{}.now());
    const bool moved = glm::distance(worldOrigin, m_lastShadowWorldOrigin) >=
        shadowMovementThreshold(quality);
    const float lightDelta = glm::length(
        glm::normalize(m_renderer.m_environment.lightDirection) -
        glm::normalize(m_lastShadowDirection));
    const bool timeDue = nowSeconds - m_lastShadowUpdateSeconds >=
        (1.0 / shadowUpdateHz(quality));
    const bool updateShadow = qualityChanged || m_lastShadowUpdateSeconds < 0.0 ||
        moved || lightDelta >= 0.01f || (timeDue && lightDelta >= 0.0002f);
    if (!updateShadow) {
        m_shadowCascades = m_shadowBaseCascades;
        const glm::dvec3 delta = worldOrigin - m_lastShadowWorldOrigin;
        const glm::mat4 translation = glm::translate(glm::mat4(1.0f), glm::vec3(
            static_cast<float>(delta.x), 0.0f, static_cast<float>(delta.z)));
        for (int i = 0; i < m_shadowCascades.count; ++i)
            m_shadowCascades.lightViewProjection[i] *= translation;
        return;
    }
    const float fogDistance = (static_cast<float>(Config::RENDER_DISTANCE) + 0.5f) *
                              Config::CHUNK_SIZE_X;
    m_shadowCascades = buildShadowCascades(quality, inverseViewProjection, view,
        m_renderer.m_environment.lightDirection, Config::NEAR_PLANE, fogDistance);
    m_shadowBaseCascades = m_shadowCascades;
    m_lastShadowUpdateSeconds = nowSeconds;
    m_lastShadowWorldOrigin = worldOrigin;
    m_lastShadowDirection = m_renderer.m_environment.lightDirection;
    bindFramebuffer(GL_FRAMEBUFFER_VALUE, m_shadowFramebuffer.name());
    auto colorMask = glProc<ColorMaskFn>("glColorMask");
    if (!colorMask) {
        m_shadowCascades = {};
        bindFramebuffer(GL_FRAMEBUFFER_VALUE, m_renderer.m_sceneTarget->framebuffer());
        return;
    }
    colorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    GL_CHECK(glEnable(GL_DEPTH_TEST));
    GL_CHECK(glDepthMask(GL_TRUE));
    GL_CHECK(glEnable(GL_CULL_FACE));
    GL_CHECK(glEnable(GL_POLYGON_OFFSET_FILL));
    GL_CHECK(glPolygonOffset(2.0f, 4.0f));
    GL_CHECK(glClear(GL_DEPTH_BUFFER_BIT));
    m_renderer.m_shadowShader->bind();
    m_renderer.m_shadowShader->setInt("uBlockAtlas", 0);
    m_renderer.m_shadowShader->setFloat("uAtlasTiles",
        static_cast<float>(BlockTextureAtlas::tilesPerSide()));
    m_renderer.m_blockAtlas.bind();
    for (int cascade = 0; cascade < m_shadowCascades.count; ++cascade) {
        const int columns = m_shadowCascades.count == 1 ? 1 : 2;
        GL_CHECK(glViewport((cascade % columns) * config.resolution,
                            (cascade / columns) * config.resolution,
                            config.resolution, config.resolution));
        for (const ShadowChunkSubmission& submission : chunks) {
            if (!submission.mesh || !submission.mesh->gpuReady ||
                submission.mesh->shadowCasterIndexCount == 0) continue;
            if (!shadowIntersectsAabb(m_shadowCascades.lightViewProjection[cascade],
                                      submission.aabbMin, submission.aabbMax, false)) continue;
            const auto found = m_renderer.m_chunkMeshes.find(
                submission.mesh->renderHandle.value);
            if (found == m_renderer.m_chunkMeshes.end()) continue;
            m_renderer.m_shadowShader->setMat4("uLightMVP",
                m_shadowCascades.lightViewProjection[cascade] * submission.model);
            GL_CHECK(glBindVertexArray(found->second.vao));
            GL_CHECK(glDrawElements(GL_TRIANGLES,
                static_cast<GLsizei>(submission.mesh->shadowCasterIndexCount), GL_UNSIGNED_INT,
                reinterpret_cast<void*>(submission.mesh->shadowCasterIndexOffset *
                                        sizeof(unsigned int))));
            ++m_renderer.m_performanceStats.drawCalls;
        }
    }
    GL_CHECK(glDisable(GL_POLYGON_OFFSET_FILL));
    colorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    bindFramebuffer(GL_FRAMEBUFFER_VALUE, m_renderer.m_sceneTarget->framebuffer());
    GL_CHECK(glViewport(0, 0, std::max(1, m_renderer.m_window->width()),
                        std::max(1, m_renderer.m_window->height())));
}
