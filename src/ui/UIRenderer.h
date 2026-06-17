#pragma once

#include <memory>
#include <glm/glm.hpp>
#include <glad/glad.h>

#include "ui/FontRenderer.h"

class Shader;

class UIRenderer {
public:
    UIRenderer() = default;
    ~UIRenderer();

    UIRenderer(const UIRenderer&) = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;

    void initialize();

    void beginUIFrame(int screenWidth, int screenHeight);
    void endUIFrame();

    void drawRect(float x, float y, float w, float h, const glm::vec4& color);

    void renderText(const std::string& text, float x, float y,
                    float scale, const glm::vec3& color);
    glm::vec2 measureText(const std::string& text, float scale);

    FontRenderer& getFontRenderer() { return m_fontRenderer; }

private:
    std::unique_ptr<Shader> m_uiShader;
    FontRenderer m_fontRenderer;

    glm::mat4 m_projection{1.0f};

    GLuint m_quadVAO = 0;
    GLuint m_quadVBO = 0;
    GLuint m_quadEBO = 0;

    // Saved GL state
    GLboolean m_prevDepthTest = GL_TRUE;
    GLboolean m_prevCullFace = GL_TRUE;
    GLboolean m_prevBlend = GL_FALSE;
    GLint m_prevBlendSrc = 0;
    GLint m_prevBlendDst = 0;
    GLint m_prevActiveTexture = 0;
};
