#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <glm/glm.hpp>

#include "world/Block.h"
#include "game/Item.h"
#include "game/Localization.h"
#include "core/GraphicsApi.h"
#include "renderer/RenderHandles.h"

class IGameRenderer;

class IUIRenderBackend {
public:
    virtual ~IUIRenderBackend() = default;
    virtual void beginUIFrame(int width, int height) = 0;
    virtual void setCanvas(float x, float y, float width, float height) = 0;
    virtual void endUIFrame() = 0;
    virtual void drawRect(float x, float y, float width, float height,
                          const glm::vec4& color) = 0;
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

#if defined(MINECRAFTC_ENABLE_OPENGL)
std::unique_ptr<IUIRenderBackend> createOpenGLUIBackend(
    RenderTextureHandle blockAtlasTexture, bool framebufferSrgb,
    const std::filesystem::path& assetRoot, GraphicsApi api);
#endif
#if defined(MINECRAFTC_ENABLE_VULKAN)
std::unique_ptr<IUIRenderBackend> createVulkanUIBackend(
    IGameRenderer& renderer, RenderTextureHandle blockAtlasTexture,
    const std::filesystem::path& assetRoot);
#endif

class UIRenderer {
public:
    UIRenderer() = default;
    ~UIRenderer();
    UIRenderer(const UIRenderer&) = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;

    void initialize(IGameRenderer& renderer, RenderTextureHandle blockAtlasTexture,
                    bool framebufferSrgb, const std::filesystem::path& assetRoot,
                    GraphicsApi api);
    void reinitialize(IGameRenderer& renderer, RenderTextureHandle blockAtlasTexture,
                      bool framebufferSrgb, const std::filesystem::path& assetRoot,
                      GraphicsApi api);
    void resetGraphics();
    void setLocalization(const Localization& localization);
    const Localization& localization() const { return *m_localization; }
    void beginUIFrame(int width, int height);
    void setCanvas(float x, float y, float width, float height);
    void endUIFrame();
    void drawRect(float,float,float,float,const glm::vec4&);
    void drawBlockIcon(float,float,float,float,BlockId);
    void drawItemIcon(float,float,float,float,const ItemStack&);
    void drawDurability(float,float,float,const ItemStack&);
    void drawPanel(float,float,float,float,
                   const glm::vec4& fill = glm::vec4(.10f,.10f,.12f,.94f));
    void drawTooltip(float,float,const ItemStack&);
    void renderText(const std::string&,float,float,float,const glm::vec3&);
    glm::vec2 measureText(const std::string&,float);

private:
    std::unique_ptr<IUIRenderBackend> m_backend;
    const Localization* m_localization = nullptr;
};
