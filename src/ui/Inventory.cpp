#include "ui/Inventory.h"
#include "ui/UIRenderer.h"
#include "Config.h"

#include <GLFW/glfw3.h>

CreativeInventory::CreativeInventory() {
    // Populate slots with all non-AIR blocks (in BLOCK_TABLE order)
    for (uint8_t i = 1; i < static_cast<uint8_t>(BlockId::COUNT); ++i) {
        BlockId id = static_cast<BlockId>(i);
        m_slots.push_back({id, 0.0f, 0.0f, false});
    }
}

void CreativeInventory::layoutSlots(int screenWidth, int screenHeight) {
    const float slotSize   = Config::INV_SLOT_SIZE;
    const float padding    = Config::INV_PADDING;
    const float labelH     = Config::INV_LABEL_HEIGHT;
    const int   cols       = Config::INV_COLS;
    const int   numSlots   = static_cast<int>(m_slots.size());
    const int   rows       = (numSlots + cols - 1) / cols;

    const float gridW = cols * slotSize + (cols - 1) * padding;
    const float gridH = rows * (slotSize + labelH + padding) - padding;  // no gap after last row

    const float originX = (static_cast<float>(screenWidth) - gridW) * 0.5f;
    const float originY = (static_cast<float>(screenHeight) - gridH) * 0.5f;

    for (int i = 0; i < numSlots; ++i) {
        int col = i % cols;
        int row = i / cols;

        // Rows top-to-bottom: row 0 = top row
        m_slots[i].x = originX + col * (slotSize + padding);
        m_slots[i].y = originY + (rows - 1 - row) * (slotSize + labelH + padding);
    }
}

void CreativeInventory::render(UIRenderer& ui, int screenWidth, int screenHeight,
                               int /*mouseX*/, int /*mouseY*/) {
    // Recompute layout each frame
    layoutSlots(screenWidth, screenHeight);

    const float slotSize = Config::INV_SLOT_SIZE;

    // Full-screen semi-transparent overlay
    ui.drawRect(0.0f, 0.0f, static_cast<float>(screenWidth),
                static_cast<float>(screenHeight),
                glm::vec4(0.0f, 0.0f, 0.0f, 0.55f));

    // Title
    const char* title = "CREATIVE INVENTORY";
    float titleScale = Config::INV_TITLE_SCALE;
    auto titleSize = ui.measureText(title, titleScale);
    float titleX = (screenWidth - titleSize.x) * 0.5f;

    // Find top of grid for title placement
    float gridTop = 0.0f;
    for (const auto& slot : m_slots) {
        float top = slot.y + slotSize;
        if (top > gridTop) gridTop = top;
    }
    float titleY = gridTop + Config::INV_LABEL_HEIGHT;
    ui.renderText(title, titleX, titleY, titleScale,
                  glm::vec3(1.0f, 0.85f, 0.3f));

    // Instruction text at bottom
    const char* instr = "Click to select  |  E to close";
    float instrScale = Config::INV_INSTR_SCALE;
    auto instrSize = ui.measureText(instr, instrScale);
    float instrX = (screenWidth - instrSize.x) * 0.5f;
    float instrY = 20.0f;
    ui.renderText(instr, instrX, instrY, instrScale,
                  glm::vec3(0.6f, 0.6f, 0.7f));

    // Draw each slot
    for (const auto& slot : m_slots) {
        const BlockProperties& props = getBlockProps(slot.id);

        // Slot background
        glm::vec4 bgColor = slot.hovered
            ? glm::vec4(props.color * 0.6f, 1.0f)
            : glm::vec4(props.color * 0.35f, 0.9f);
        ui.drawRect(slot.x, slot.y, slotSize, slotSize, bgColor);

        // Inner colored square
        float innerMargin = 4.0f;
        ui.drawRect(slot.x + innerMargin, slot.y + innerMargin,
                    slotSize - innerMargin * 2.0f, slotSize - innerMargin * 2.0f,
                    glm::vec4(props.color, 0.85f));

        // Hover border
        if (slot.hovered) {
            float bw = 2.5f;
            glm::vec4 borderCol(1.0f, 1.0f, 1.0f, 0.9f);
            ui.drawRect(slot.x, slot.y, slotSize, bw, borderCol);
            ui.drawRect(slot.x, slot.y + slotSize - bw, slotSize, bw, borderCol);
            ui.drawRect(slot.x, slot.y, bw, slotSize, borderCol);
            ui.drawRect(slot.x + slotSize - bw, slot.y, bw, slotSize, borderCol);
        }

        // Block name label below slot
        float labelScale = 1.0f;
        auto labelSize = ui.measureText(props.name, labelScale);
        ui.renderText(props.name,
                      slot.x + (slotSize - labelSize.x) * 0.5f,
                      slot.y - labelSize.y - 2.0f,
                      labelScale,
                      glm::vec3(0.9f, 0.9f, 0.9f));
    }
}

void CreativeInventory::onMouseMove(int mouseX, int mouseY) {
    const float slotSize = Config::INV_SLOT_SIZE;
    for (auto& slot : m_slots) {
        slot.hovered = (mouseX >= slot.x && mouseX <= slot.x + slotSize &&
                        mouseY >= slot.y && mouseY <= slot.y + slotSize);
    }
}

void CreativeInventory::onMouseClick(int button, int mouseX, int mouseY,
                                     std::function<void(BlockId)> onSelectBlock) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;

    const float slotSize = Config::INV_SLOT_SIZE;
    for (const auto& slot : m_slots) {
        if (mouseX >= slot.x && mouseX <= slot.x + slotSize &&
            mouseY >= slot.y && mouseY <= slot.y + slotSize) {
            if (onSelectBlock) {
                onSelectBlock(slot.id);
            }
            return;
        }
    }
}
