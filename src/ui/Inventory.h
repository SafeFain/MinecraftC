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

private:
    struct Slot {
        BlockId id;
        float x = 0.0f, y = 0.0f;  // screen-space bottom-left
        bool hovered = false;
    };

    std::vector<Slot> m_slots;

    void layoutSlots(int screenWidth, int screenHeight);
};
