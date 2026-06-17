#include "ui/Hotbar.h"
#include "ui/UIRenderer.h"
#include "Config.h"

#include <GLFW/glfw3.h>
#include <algorithm>

Hotbar::Hotbar() {
    initDefaultSlots();
}

void Hotbar::initDefaultSlots() {
    m_slots[0] = BlockId::GRASS;
    m_slots[1] = BlockId::DIRT;
    m_slots[2] = BlockId::STONE;
    m_slots[3] = BlockId::WOOD;
    m_slots[4] = BlockId::PLANKS;
    m_slots[5] = BlockId::LEAVES;
    m_slots[6] = BlockId::SAND;
    m_slots[7] = BlockId::SNOW;
    m_slots[8] = BlockId::WATER;
}

void Hotbar::selectSlot(int index) {
    if (index >= 0 && index < static_cast<int>(m_slots.size())) {
        m_selectedSlot = index;
    }
}

void Hotbar::setSlotBlock(int index, BlockId id) {
    if (index >= 0 && index < static_cast<int>(m_slots.size())) {
        m_slots[index] = id;
    }
}

void Hotbar::onScroll(double yoffset) {
    if (yoffset > 0.0) {
        selectSlot((m_selectedSlot - 1 + static_cast<int>(m_slots.size())) % static_cast<int>(m_slots.size()));
    } else if (yoffset < 0.0) {
        selectSlot((m_selectedSlot + 1) % static_cast<int>(m_slots.size()));
    }
}

void Hotbar::onKeyPress(int key) {
    if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) {
        selectSlot(key - GLFW_KEY_1);
    }
}

void Hotbar::render(UIRenderer& ui, int screenWidth, int /*screenHeight*/) {
    const float slotSize = Config::HOTBAR_SLOT_SIZE;
    const float gap     = Config::HOTBAR_GAP;
    const float padX    = Config::HOTBAR_PAD_X;
    const float padY    = Config::HOTBAR_PAD_Y;
    const int   numSlots = static_cast<int>(m_slots.size());

    const float totalW = numSlots * slotSize + (numSlots - 1) * gap + padX * 2.0f;
    const float totalH = slotSize + padY * 2.0f;

    const float barX = (static_cast<float>(screenWidth) - totalW) * 0.5f;
    const float barY = 4.0f;

    // Bar background
    ui.drawRect(barX, barY, totalW, totalH,
                glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));

    // Draw each slot
    for (int i = 0; i < numSlots; ++i) {
        float sx = barX + padX + static_cast<float>(i) * (slotSize + gap);
        float sy = barY + padY;

        BlockId id = m_slots[i];
        const BlockProperties& props = getBlockProps(id);

        // Slot background (darker)
        ui.drawRect(sx, sy, slotSize, slotSize,
                    glm::vec4(props.color * 0.35f, 0.9f));

        // Inner colored square
        float innerMargin = 4.0f;
        ui.drawRect(sx + innerMargin, sy + innerMargin,
                    slotSize - innerMargin * 2.0f, slotSize - innerMargin * 2.0f,
                    glm::vec4(props.color, 0.85f));

        // Selection highlight
        if (i == m_selectedSlot) {
            float bw = 2.5f;
            glm::vec4 borderCol(1.0f, 1.0f, 1.0f, 0.9f);
            ui.drawRect(sx, sy, slotSize, bw, borderCol);                      // bottom
            ui.drawRect(sx, sy + slotSize - bw, slotSize, bw, borderCol);      // top
            ui.drawRect(sx, sy, bw, slotSize, borderCol);                      // left
            ui.drawRect(sx + slotSize - bw, sy, bw, slotSize, borderCol);      // right
        }

        // Slot number
        std::string numLabel = std::to_string(i + 1);
        float labelScale = 1.0f;
        auto labelSize = ui.measureText(numLabel, labelScale);
        ui.renderText(numLabel,
                      sx + (slotSize - labelSize.x) * 0.5f,
                      sy - labelSize.y - 1.0f,
                      labelScale,
                      glm::vec3(0.7f, 0.7f, 0.7f));
    }
}
