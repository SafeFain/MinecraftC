#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <glm/glm.hpp>

#include "world/Block.h"
#include "game/Item.h"
#include "game/Localization.h"
#include "renderer/UIRenderBackend.h"

class UIRenderer {
public:
    UIRenderer() = default;
    ~UIRenderer();
    UIRenderer(const UIRenderer&) = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;

    void initialize(std::unique_ptr<IUIRenderBackend> backend);
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
    void advanceTime(float dt) { m_frameDelta = std::max(0.0f, dt); ++m_frameSerial; m_animationTime += m_frameDelta; if (m_backend) m_backend->setAnimationTime(static_cast<float>(m_animationTime)); }
    double animationTime() const { return m_animationTime; }
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
    double m_animationTime = 0;
    float m_frameDelta = 0.0f;
    uint64_t m_frameSerial = 0;
    std::unique_ptr<IUIRenderBackend> m_backend;
    const Localization* m_localization = nullptr;
};
