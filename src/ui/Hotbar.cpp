#include "ui/Hotbar.h"
#include "ui/UIRenderer.h"
#include "Config.h"

#include <algorithm>

Hotbar::Hotbar() = default;

void Hotbar::selectSlot(int index) {
    if (index >= 0 && index < static_cast<int>(InventoryModel::HOTBAR_SIZE)) {
        m_selectedSlot = index;
    }
}

void Hotbar::onScroll(double yoffset) {
    if (yoffset > 0.0) {
        selectSlot((m_selectedSlot - 1 + 9) % 9);
    } else if (yoffset < 0.0) {
        selectSlot((m_selectedSlot + 1) % 9);
    }
}

void Hotbar::onKeyPress(int key) {
    if (key >= Key::Num1 && key <= Key::Num9) {
        selectSlot(key - Key::Num1);
    }
}

void Hotbar::render(UIRenderer& ui, int screenWidth, int /*screenHeight*/) {
    const float slotSize = Config::HOTBAR_SLOT_SIZE;
    const float gap     = Config::HOTBAR_GAP;
    const float padX    = Config::HOTBAR_PAD_X;
    const float padY    = Config::HOTBAR_PAD_Y;
    constexpr int numSlots = static_cast<int>(InventoryModel::HOTBAR_SIZE);

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

        const ItemStack emptyStack{};
        BlockId id = BlockId::AIR;
        const ItemStack* shownStack = m_inventory
            ? &m_inventory->slot(static_cast<size_t>(i)) : &emptyStack;
        const ItemProperties* itemProps = shownStack && !shownStack->empty()
            ? &getItemProps(shownStack->id) : nullptr;
        if (itemProps && itemProps->placedBlock) id = *itemProps->placedBlock;
        const glm::vec3 slotColor = id == BlockId::AIR
            ? glm::vec3(.12f) : getBlockProps(id).color * .35f;

        // Slot background (darker)
        ui.drawRect(sx, sy, slotSize, slotSize,
                    glm::vec4(slotColor, 0.9f));

        // Material thumbnail from the same atlas used by world rendering.
        float innerMargin = 4.0f;
        if (!shownStack->empty()) {
            ui.drawItemIcon(sx + innerMargin, sy + innerMargin,
                            slotSize - innerMargin * 2.0f,
                            slotSize - innerMargin * 2.0f, *shownStack);
        }

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

        ui.drawDurability(sx + 4.0f, sy + 3.0f,
                          slotSize - 8.0f, *shownStack);
        if (shownStack->count > 1) {
            const std::string countLabel = std::to_string(shownStack->count);
            auto countSize = ui.measureText(countLabel, 1.0f);
            ui.renderText(countLabel, sx + slotSize - countSize.x - 3.0f,
                          sy + 3.0f, 1.0f, glm::vec3(1.0f));
        }
    }
}
