#include "ui/UIRenderer.h"
#include "renderer/Shader.h"
#include "debug/OpenGL.h"

#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cstdio>

// ── Constructor / Destructor ──────────────────────────────────────────────

UIRenderer::~UIRenderer() {
    if (m_quadEBO) GL_CHECK(glDeleteBuffers(1, &m_quadEBO));
    if (m_quadVBO) GL_CHECK(glDeleteBuffers(1, &m_quadVBO));
    if (m_quadVAO) GL_CHECK(glDeleteVertexArrays(1, &m_quadVAO));
}

// ── Initialization ────────────────────────────────────────────────────────

void UIRenderer::initialize() {
    // Compile UI rectangle shader
    m_uiShader = std::make_unique<Shader>(
        "assets/shaders/ui.vert",
        "assets/shaders/ui.frag"
    );

    // Create unit-square VAO for rectangles with index buffer
    const float quadVerts[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 1.0f
    };
    const unsigned int quadIndices[] = { 0, 1, 2, 0, 2, 3 };

    GL_CHECK(glGenVertexArrays(1, &m_quadVAO));
    GL_CHECK(glBindVertexArray(m_quadVAO));

    GL_CHECK(glGenBuffers(1, &m_quadVBO));
    GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO));
    GL_CHECK(glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW));
    GL_CHECK(glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr));
    GL_CHECK(glEnableVertexAttribArray(0));

    GL_CHECK(glGenBuffers(1, &m_quadEBO));
    GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_quadEBO));
    GL_CHECK(glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIndices), quadIndices, GL_STATIC_DRAW));

    GL_CHECK(glBindVertexArray(0));

    // Initialize font renderer
    m_fontRenderer.initialize();
}

// ── Frame management ──────────────────────────────────────────────────────

void UIRenderer::beginUIFrame(int screenWidth, int screenHeight) {
    // Save current GL state
    glGetBooleanv(GL_DEPTH_TEST, &m_prevDepthTest);
    glGetBooleanv(GL_CULL_FACE, &m_prevCullFace);
    glGetBooleanv(GL_BLEND, &m_prevBlend);
    glGetIntegerv(GL_BLEND_SRC, &m_prevBlendSrc);
    glGetIntegerv(GL_BLEND_DST, &m_prevBlendDst);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &m_prevActiveTexture);

    // Set UI state
    GL_CHECK(glDisable(GL_DEPTH_TEST));
    GL_CHECK(glDisable(GL_CULL_FACE));
    GL_CHECK(glEnable(GL_BLEND));
    GL_CHECK(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    // Orthographic projection: (0,0) at bottom-left, (w,h) at top-right
    m_projection = glm::ortho(0.0f, static_cast<float>(screenWidth),
                               0.0f, static_cast<float>(screenHeight));
}

void UIRenderer::endUIFrame() {
    // Restore previous GL state
    if (m_prevDepthTest) GL_CHECK(glEnable(GL_DEPTH_TEST)); else GL_CHECK(glDisable(GL_DEPTH_TEST));
    if (m_prevCullFace)  GL_CHECK(glEnable(GL_CULL_FACE));  else GL_CHECK(glDisable(GL_CULL_FACE));
    if (m_prevBlend)     GL_CHECK(glEnable(GL_BLEND));       else GL_CHECK(glDisable(GL_BLEND));
    GL_CHECK(glBlendFunc(m_prevBlendSrc, m_prevBlendDst));
}

// ── Rectangle drawing ─────────────────────────────────────────────────────

void UIRenderer::drawRect(float x, float y, float w, float h,
                           const glm::vec4& color) {
    m_uiShader->bind();
    m_uiShader->setMat4("uProjection", m_projection);

    // Set 4-component color uniform
    m_uiShader->setVec4("uColor", color);

    // Build 4 screen-space vertices using unit square + translation/scale
    // Since our shader takes screen-space positions directly and we use
    // orthographic projection, we need to upload dynamic vertices.
    float verts[] = {
        x,   y,
        x+w, y,
        x+w, y+h,
        x,   y+h
    };

    GL_CHECK(glBindVertexArray(m_quadVAO));

    GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO));
    GL_CHECK(glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW));

    GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_quadEBO));
    GL_CHECK(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr));

    GL_CHECK(glBindVertexArray(0));
}

// ── Text rendering (delegated to FontRenderer) ────────────────────────────

void UIRenderer::renderText(const std::string& text, float x, float y,
                              float scale, const glm::vec3& color) {
    m_fontRenderer.begin(m_projection);
    m_fontRenderer.renderText(text, x, y, scale, color);
    m_fontRenderer.end();
}

glm::vec2 UIRenderer::measureText(const std::string& text, float scale) {
    return m_fontRenderer.measureText(text, scale);
}
