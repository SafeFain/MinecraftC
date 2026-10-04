#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <glm/glm.hpp>

#include "world/Block.h"
#include "game/Item.h"
#include "game/Localization.h"
#include "renderer/RenderHandles.h"

class IGameRenderer;

struct UiFrameStats {
    size_t batches=0, vertices=0, indices=0;
};

class IUIRenderBackend {
public:
    virtual ~IUIRenderBackend() = default;
    virtual void beginUIFrame(int width, int height) = 0;
    virtual void setCanvas(float x, float y, float width, float height) = 0;
    virtual void endUIFrame() = 0;
    virtual UiFrameStats frameStats() const = 0;
    virtual void drawRect(float x, float y, float width, float height,
                          const glm::vec4& color) = 0;
    virtual void drawRoundedRect(float x, float y, float width, float height,
                                 float radius, const glm::vec4& color) = 0;
    virtual void setOpacity(float opacity) = 0;
    virtual void renderTextAlpha(const std::string& text, float x, float y,
                                 float scale, const glm::vec3& color, float alpha) = 0;
    virtual void drawAtlasIcon(float,float,float,float,uint16_t,const glm::vec4&) {}
    virtual void drawBlockIcon(float x, float y, float width, float height,
                               BlockId block) = 0;
    virtual void drawItemIcon(float x, float y, float width, float height,
                              const ItemStack& stack) = 0;
    virtual void drawDurability(float x, float y, float width,
                                const ItemStack& stack) = 0;
    virtual void drawPanel(float x, float y, float width, float height,
                           const glm::vec4& fill) = 0;
    virtual void drawTooltip(float x, float y, const ItemStack& stack) = 0;
    virtual void renderText(const std::string& text, float x, float y,
                            float scale, const glm::vec3& color) = 0;
    virtual glm::vec2 measureText(const std::string& text, float scale) = 0;
    virtual void setLocalization(const Localization* localization) = 0;
};

std::unique_ptr<IUIRenderBackend> createVulkanUIBackend(
    IGameRenderer& renderer, RenderTextureHandle blockAtlasTexture,
    const std::filesystem::path& assetRoot);

class UIRenderer {
public:
    UIRenderer() = default;
    ~UIRenderer();
    UIRenderer(const UIRenderer&) = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;

    void initialize(IGameRenderer& renderer, RenderTextureHandle blockAtlasTexture,
                    const std::filesystem::path& assetRoot);
    void reinitialize(IGameRenderer& renderer, RenderTextureHandle blockAtlasTexture,
                      const std::filesystem::path& assetRoot);
    void resetGraphics();
    void setLocalization(const Localization& localization);
    const Localization& localization() const { return *m_localization; }
    void beginUIFrame(int width, int height);
    void setCanvas(float x, float y, float width, float height);
    void endUIFrame();
    UiFrameStats frameStats() const;
    void drawRect(float,float,float,float,const glm::vec4&);
    void drawAtlasIcon(float x,float y,float w,float h,uint16_t tile,const glm::vec4& c) { m_backend->drawAtlasIcon(x,y,w,h,tile,c); }
    void drawRoundedRect(float,float,float,float,float,const glm::vec4&);
    void setOpacity(float);
    void renderTextAlpha(const std::string&,float,float,float,const glm::vec3&,float);
    void advanceTime(float dt) { m_frameDelta = std::max(0.0f, dt); ++m_frameSerial; }
    int canvasWidth() const { return m_uiWidth; }
    int canvasHeight() const { return m_uiHeight; }
    float frameDelta() const { return m_frameDelta; }
    uint64_t frameSerial() const { return m_frameSerial; }
    void drawBlockIcon(float,float,float,float,BlockId);
    void drawItemIcon(float,float,float,float,const ItemStack&);
    void drawDurability(float,float,float,const ItemStack&);
    void drawPanel(float,float,float,float,
                   const glm::vec4& fill = glm::vec4(.10f,.10f,.12f,.94f));
    void drawTooltip(float,float,const ItemStack&);
    void renderText(const std::string&,float,float,float,const glm::vec3&);
    glm::vec2 measureText(const std::string&,float);

private:
    int m_uiWidth = 1, m_uiHeight = 1;
    float m_frameDelta = 0.0f;
    uint64_t m_frameSerial = 0;
    std::unique_ptr<IUIRenderBackend> m_backend;
    const Localization* m_localization = nullptr;
};
