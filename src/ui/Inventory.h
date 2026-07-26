#pragma once

#include "world/Block.h"
#include <vector>
#include <functional>

class UIRenderer;

class CreativeInventory {
public:
    CreativeInventory();

    void render(UIRenderer& ui, int screenWidth, int screenHeight,
                int mouseX, int mouseY);
    void onMouseClick(int button, int mouseX, int mouseY,
                      std::function<void(BlockId)> onSelectBlock);
    void onMouseMove(int mouseX, int mouseY);
    void onScroll(double yOffset);

private:
    struct Slot {
        BlockId id;
        float x = 0.0f, y = 0.0f;  // screen-space bottom-left
        bool hovered = false;
        bool visible = false;
    };

    std::vector<Slot> m_slots;
    int m_columns = 5;
    int m_visibleRows = 4;
    int m_scrollRow = 0;
    int m_totalRows = 0;
    BlockId m_selected = BlockId::AIR;
    float m_panelX = 0.0f, m_panelY = 0.0f, m_panelW = 0.0f, m_panelH = 0.0f;

    void layoutSlots(int screenWidth, int screenHeight);
};
