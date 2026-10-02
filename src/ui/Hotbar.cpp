#include "ui/Hotbar.h"
#include "ui/UIRenderer.h"
#include "ui/UIStyle.h"
#include "ui/UILayout.h"
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
    const UiHotbarLayout layout(screenWidth);
    const float slotSize=layout.slot,gap=layout.gap,padX=layout.padX,padY=layout.padY;
    constexpr int numSlots=static_cast<int>(InventoryModel::HOTBAR_SIZE);
    const float totalW=layout.width,totalH=layout.height,barX=layout.x,barY=layout.y;
    // Bar background: raised pixel panel with an ink frame.
    UiTheme::panel(ui, barX, barY, totalW, totalH, UiTheme::PANEL, {}, 1.0f,
                   0.94f);

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
            ? glm::vec3(UiTheme::SLOT) : glm::mix(glm::vec3(UiTheme::SLOT),getBlockProps(id).color,.12f);

        // Recessed slot with a per-block ambient tint.
        UiTheme::slot(ui, sx, sy, slotSize, slotSize,
                      i == m_selectedSlot ? UiTheme::WidgetState::Selected
                                          : UiTheme::WidgetState::Normal,
                      glm::vec4(slotColor, 0.95f));

        // Material thumbnail from the same atlas used by world rendering.
        float innerMargin = 6.0f*layout.scale;
        if (!shownStack->empty()) {
            ui.drawItemIcon(sx + innerMargin, sy + innerMargin,
                            slotSize - innerMargin * 2.0f,
                            slotSize - innerMargin * 2.0f, *shownStack);
        }

        // Slot number
        std::string numLabel = std::to_string(i + 1);
        float labelScale = .65f*layout.scale;
        auto labelSize = ui.measureText(numLabel, labelScale);
        UiTheme::textWithShadow(ui, numLabel,
                      sx+4*layout.scale,
                      sy+slotSize-labelSize.y-2*layout.scale,
                      labelScale,
                      glm::vec3(0.7f, 0.7f, 0.7f));

        ui.drawDurability(sx+4*layout.scale,sy+3*layout.scale,
                          slotSize-8*layout.scale, *shownStack);
        if (shownStack->count > 1) {
            const std::string countLabel = std::to_string(shownStack->count);
            auto countSize = ui.measureText(countLabel,layout.scale);
            UiTheme::textWithShadow(ui, countLabel,
                          sx + slotSize - countSize.x - 3.0f,
                          sy+3*layout.scale,layout.scale, glm::vec3(1.0f));
        }
    }
}
