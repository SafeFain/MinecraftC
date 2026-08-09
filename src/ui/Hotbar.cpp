#include "ui/Hotbar.h"
#include "ui/UIRenderer.h"
#include "Config.h"

#include <algorithm>

Hotbar::Hotbar() {
    initDefaultSlots();
}

void Hotbar::initDefaultSlots() {
    const BlockId defaults[] = {BlockId::GRASS, BlockId::DIRT, BlockId::STONE,
        BlockId::WOOD, BlockId::PLANKS, BlockId::LEAVES, BlockId::SAND,
        BlockId::SNOW, BlockId::WATER};
    for (size_t i=0;i<m_slots.size();++i) m_slots[i]={itemForBlock(defaults[i]),1,0};
}

BlockId Hotbar::getSelectedBlock() const {
    const auto& props=getItemProps(getSelectedItem());
    return props.placedBlock.value_or(BlockId::AIR);
}

void Hotbar::selectSlot(int index) {
    if (index >= 0 && index < static_cast<int>(m_slots.size())) {
        m_selectedSlot = index;
    }
}

void Hotbar::setSlotItem(int index, ItemId id) {
    if (index >= 0 && index < static_cast<int>(m_slots.size())) {
        m_slots[index] = {id,1,0};
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
    if (key >= Key::Num1 && key <= Key::Num9) {
        selectSlot(key - Key::Num1);
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

        ItemStack creativeStack = m_slots[i];
        BlockId id = BlockId::AIR;
        const ItemStack* survivalStack = m_survivalInventory
            ? &m_survivalInventory->slot(static_cast<size_t>(i)) : nullptr;
        const ItemStack* shownStack = survivalStack ? survivalStack : &creativeStack;
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
        if (!survivalStack || !survivalStack->empty()) {
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

        if (survivalStack) ui.drawDurability(sx + 4.0f, sy + 3.0f,
                                              slotSize - 8.0f, *survivalStack);
        if (survivalStack && survivalStack->count > 1) {
            const std::string countLabel = std::to_string(survivalStack->count);
            auto countSize = ui.measureText(countLabel, 1.0f);
            ui.renderText(countLabel, sx + slotSize - countSize.x - 3.0f,
                          sy + 3.0f, 1.0f, glm::vec3(1.0f));
        }
    }
}
