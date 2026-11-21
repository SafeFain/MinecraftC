#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <glm/glm.hpp>

enum class BlockId : uint16_t;
struct ItemStack;
class Localization;

struct UiFrameStats {
    std::size_t batches = 0;
    std::size_t vertices = 0;
    std::size_t indices = 0;
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
    virtual void drawAtlasIcon(float, float, float, float, uint16_t, const glm::vec4&) {}
    virtual void drawBlockIcon(float x, float y, float width, float height,
                               BlockId block) = 0;
    virtual void setAnimationTime(float) {}
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
