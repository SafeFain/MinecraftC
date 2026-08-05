#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>

#include "ui/FontRenderer.h"
#include "world/Block.h"
#include "game/Item.h"
#include "game/Localization.h"
#include "core/GraphicsApi.h"
#include "renderer/RenderHandles.h"

class Shader;

class UIRenderer {
public:
    UIRenderer() = default;
    ~UIRenderer();

    UIRenderer(const UIRenderer&) = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;

    void initialize(RenderTextureHandle blockAtlasTexture, bool framebufferSrgb,
                    const std::filesystem::path& assetRoot, GraphicsApi api);
    void reinitialize(RenderTextureHandle blockAtlasTexture, bool framebufferSrgb,
                      const std::filesystem::path& assetRoot, GraphicsApi api);
    void resetGraphics();
    void setLocalization(const Localization& localization) {
        m_localization = &localization;
    }
    const Localization& localization() const { return *m_localization; }

    void beginUIFrame(int screenWidth, int screenHeight);
    void setCanvas(float originX, float originY, float fullWidth, float fullHeight) {
        m_canvasOrigin = {originX, originY};
        m_canvasSize = {fullWidth, fullHeight};
    }
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
    glm::vec2 m_canvasOrigin{0.0f};
    glm::vec2 m_canvasSize{0.0f};

    uint32_t m_quadVAO = 0;
    uint32_t m_quadVBO = 0;
    uint32_t m_quadEBO = 0;
    RenderTextureHandle m_blockAtlasTexture; // shared, owned by Renderer
    uint32_t m_itemAtlasTexture = 0;  // owned by UIRenderer
    int m_itemAtlasColumns = 0;
    int m_itemAtlasRows = 0;
    std::unordered_map<std::string, int> m_itemAtlasIndices;
    bool m_manualGamma = false;
    const Localization* m_localization = nullptr;

    bool drawGeneratedItemIcon(float x, float y, float w, float h, ItemId item);

    // Saved GL state
    uint8_t m_prevDepthTest = 1;
    uint8_t m_prevCullFace = 1;
    uint8_t m_prevBlend = 0;
    int m_prevBlendSrc = 0;
    int m_prevBlendDst = 0;
    int m_prevActiveTexture = 0;
};
