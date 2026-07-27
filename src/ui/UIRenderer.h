#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glad/glad.h>

#include "ui/FontRenderer.h"
#include "world/Block.h"
#include "game/Item.h"

class Shader;

class UIRenderer {
public:
    UIRenderer() = default;
    ~UIRenderer();

    UIRenderer(const UIRenderer&) = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;

    void initialize(GLuint blockAtlasTexture, bool framebufferSrgb);

    void beginUIFrame(int screenWidth, int screenHeight);
    void endUIFrame();

    void drawRect(float x, float y, float w, float h, const glm::vec4& color);
    void drawBlockIcon(float x, float y, float w, float h, BlockId block);
    void drawItemIcon(float x, float y, float w, float h, const ItemStack& stack);
    void drawDurability(float x, float y, float w, const ItemStack& stack);
    void drawPanel(float x, float y, float w, float h,
                   const glm::vec4& fill = glm::vec4(0.10f, 0.10f, 0.12f, 0.94f));
    void drawTooltip(float x, float y, const ItemStack& stack);

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
    GLuint m_blockAtlasTexture = 0; // shared, owned by Renderer
    GLuint m_itemAtlasTexture = 0;  // owned by UIRenderer
    int m_itemAtlasColumns = 0;
    int m_itemAtlasRows = 0;
    std::unordered_map<std::string, int> m_itemAtlasIndices;
    bool m_manualGamma = false;

    bool drawGeneratedItemIcon(float x, float y, float w, float h, ItemId item);

    // Saved GL state
    GLboolean m_prevDepthTest = GL_TRUE;
    GLboolean m_prevCullFace = GL_TRUE;
    GLboolean m_prevBlend = GL_FALSE;
    GLint m_prevBlendSrc = 0;
    GLint m_prevBlendDst = 0;
    GLint m_prevActiveTexture = 0;
};
