#pragma once

#include "game/Item.h"
#include <array>
#include <vector>
#include <functional>

class UIRenderer;

class CreativeInventory {
public:
    CreativeInventory();

    void render(UIRenderer& ui, int screenWidth, int screenHeight,
                int mouseX, int mouseY);
    void onMouseClick(int button, int mouseX, int mouseY,
                      std::function<void(ItemId)> onSelectItem,
                      std::function<void()> onOpenPlayerInventory = {});
    void onMouseMove(int mouseX, int mouseY);
    void onScroll(double yOffset);
    void onGamepadNavigate(int dx, int dy);
    void onGamepadAction(bool select, std::function<void(ItemId)> onSelectItem);

private:
    struct Slot {
        ItemId id;
        float x = 0.0f, y = 0.0f;  // screen-space bottom-left
        bool hovered = false;
        bool visible = false;
    };
    struct Tab {
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        bool hovered = false;
    };

    std::vector<Slot> m_slots;
    std::array<Tab, static_cast<size_t>(CreativeItemCategory::Count)> m_tabs{};
    CreativeItemCategory m_activeCategory = CreativeItemCategory::BuildingBlocks;
    int m_tabFocus = 0;
    bool m_tabMode = false;
    int m_columns = 5;
    int m_visibleRows = 4;
    int m_scrollRow = 0;
    int m_totalRows = 0;
    ItemId m_selected = ItemId::EMPTY;
    int m_focus = 0;
    float m_panelX = 0.0f, m_panelY = 0.0f, m_panelW = 0.0f, m_panelH = 0.0f;
    float m_playerButtonX = 0.0f, m_playerButtonY = 0.0f;
    float m_playerButtonW = 0.0f, m_playerButtonH = 0.0f;

    void layoutSlots(int screenWidth, int screenHeight);
    void selectCategory(CreativeItemCategory category);
    void updateSlotHover();
    void updateTabHover();
};
