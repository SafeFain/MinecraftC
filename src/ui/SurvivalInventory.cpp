#include "ui/SurvivalInventory.h"

#include "game/SurvivalRules.h"
#include "ui/UIRenderer.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <string>

void SurvivalInventoryScreen::layout(int screenWidth, int screenHeight) {
    constexpr float slot = 44.0f;
    constexpr float gap = 4.0f;
    const float width = 9 * slot + 8 * gap;
    const float originX = (screenWidth - width) * 0.5f;
    const float originY = (screenHeight - (4 * slot + 3 * gap)) * 0.5f - 55.0f;

    for (size_t i = 0; i < InventoryModel::STORAGE_SIZE; ++i) {
        const int row = i < 9 ? 0 : 1 + static_cast<int>((i - 9) / 9);
        const int col = i < 9 ? static_cast<int>(i) : static_cast<int>((i - 9) % 9);
        const int visualRow = row == 0 ? 0 : 4 - row;
        m_inventoryRects[i] = {
            originX + col * (slot + gap),
            originY + visualRow * (slot + gap), slot, slot
        };
    }
    const int craftSize = m_craftingTable ? 3 : 2;
    const float craftX = screenWidth * 0.5f - (m_craftingTable ? 150.0f : 118.0f);
    const float craftY = originY + 4 * (slot + gap) + 28.0f;
    for (auto& rect : m_craftingRects) rect = {};
    for (int i = 0; i < craftSize * craftSize; ++i) {
        m_craftingRects[i] = {
            craftX + (i % craftSize) * (slot + gap),
            craftY + (craftSize - 1 - i / craftSize) * (slot + gap), slot, slot
        };
    }
    m_outputRect = {craftX + craftSize * (slot + gap) + 42.0f,
                    craftY + (craftSize - 1) * 24.0f, slot, slot};
    for (size_t i = 0; i < m_armorRects.size(); ++i)
        m_armorRects[i] = {originX - 64.0f, originY + (3 - i) * (slot + gap),
                           slot, slot};
    m_offhandRect = {originX + width + 20.0f, originY, slot, slot};
}

bool SurvivalInventoryScreen::contains(const Rect& rect, int x, int y) {
    return x >= rect.x && x <= rect.x + rect.w &&
           y >= rect.y && y <= rect.y + rect.h;
}

void SurvivalInventoryScreen::drawStack(
    UIRenderer& ui, const Rect& rect, const ItemStack& stack, bool hovered) {
    ui.drawRect(rect.x, rect.y, rect.w, rect.h,
                hovered ? glm::vec4(0.34f, 0.34f, 0.38f, 0.98f)
                        : glm::vec4(0.18f, 0.18f, 0.21f, 0.96f));
    if (stack.empty()) return;
    const auto& props = getItemProps(stack.id);
    if (props.placedBlock) {
        ui.drawBlockIcon(rect.x + 4.0f, rect.y + 4.0f, rect.w - 8.0f,
                         rect.h - 8.0f, *props.placedBlock);
    } else {
        const std::string initial(1, props.name.empty() ? '?' : props.name[0]);
        ui.renderText(initial, rect.x + 15.0f, rect.y + 13.0f, 1.5f,
                      glm::vec3(0.95f));
    }
    if (stack.count > 1) {
        const std::string text = std::to_string(stack.count);
        const auto size = ui.measureText(text, 0.9f);
        ui.renderText(text, rect.x + rect.w - size.x - 2.0f, rect.y + 2.0f,
                      0.9f, glm::vec3(1.0f));
    }
}

ItemStack SurvivalInventoryScreen::craftingOutput() const {
    std::array<ItemId, 9> grid{};
    grid.fill(ItemId::EMPTY);
    const size_t count = m_craftingTable ? 9 : 4;
    for (size_t i = 0; i < count; ++i)
        grid[i] = m_crafting[i].empty() ? ItemId::EMPTY : m_crafting[i].id;
    const auto* recipe = findCraftingRecipe(
        grid, m_craftingTable ? 3 : 2, m_craftingTable ? 3 : 2);
    return recipe ? recipe->output : ItemStack{};
}

void SurvivalInventoryScreen::render(
    UIRenderer& ui, int screenWidth, int screenHeight, int mouseX, int mouseY) {
    layout(screenWidth, screenHeight);
    ui.drawRect(0, 0, static_cast<float>(screenWidth), static_cast<float>(screenHeight),
                glm::vec4(0, 0, 0, 0.62f));
    ui.renderText("SURVIVAL INVENTORY", screenWidth * 0.5f - 126.0f,
                  screenHeight * 0.78f, 2.0f, glm::vec3(1.0f, 0.85f, 0.3f));
    ui.renderText("CRAFTING", screenWidth * 0.5f - 118.0f,
                  m_craftingRects[0].y + 54.0f, 1.1f, glm::vec3(0.85f));

    for (size_t i = 0; i < m_inventoryRects.size(); ++i)
        drawStack(ui, m_inventoryRects[i], m_inventory.slot(i),
                  contains(m_inventoryRects[i], mouseX, mouseY));
    const size_t craftSlots = m_craftingTable ? 9 : 4;
    for (size_t i = 0; i < craftSlots; ++i)
        drawStack(ui, m_craftingRects[i], m_crafting[i],
                  contains(m_craftingRects[i], mouseX, mouseY));
    drawStack(ui, m_outputRect, craftingOutput(),
              contains(m_outputRect, mouseX, mouseY));
    for (size_t i = 0; i < m_armorRects.size(); ++i)
        drawStack(ui, m_armorRects[i], m_inventory.armor()[i],
                  contains(m_armorRects[i], mouseX, mouseY));
    drawStack(ui, m_offhandRect, m_inventory.offhand(),
              contains(m_offhandRect, mouseX, mouseY));

    if (!m_cursor.empty()) {
        Rect cursor{static_cast<float>(mouseX + 8), static_cast<float>(mouseY + 8), 38, 38};
        drawStack(ui, cursor, m_cursor, true);
    }
}

void SurvivalInventoryScreen::clickStack(ItemStack& stack, bool rightClick) {
    if (m_cursor.empty()) {
        if (stack.empty()) return;
        if (rightClick && stack.count > 1) {
            const uint8_t taken = static_cast<uint8_t>((stack.count + 1) / 2);
            m_cursor = {stack.id, taken, stack.damage};
            stack.count -= taken;
        } else {
            std::swap(m_cursor, stack);
        }
        return;
    }
    if (stack.empty()) {
        if (rightClick) {
            stack = {m_cursor.id, 1, m_cursor.damage};
            if (--m_cursor.count == 0) m_cursor.clear();
        } else {
            std::swap(m_cursor, stack);
        }
        return;
    }
    if (stack.id == m_cursor.id && stack.damage == m_cursor.damage) {
        const uint8_t maximum = getItemProps(stack.id).maxStack;
        const uint8_t requested = rightClick ? 1 : m_cursor.count;
        const uint8_t moved = static_cast<uint8_t>(
            std::min<int>(requested, maximum - stack.count));
        stack.count += moved;
        m_cursor.count -= moved;
        if (m_cursor.count == 0) m_cursor.clear();
    } else if (!rightClick) {
        std::swap(m_cursor, stack);
    }
}

void SurvivalInventoryScreen::takeCraftingOutput() {
    const ItemStack output = craftingOutput();
    if (output.empty()) return;
    if (!m_cursor.empty() &&
        (m_cursor.id != output.id || m_cursor.damage != output.damage ||
         m_cursor.count + output.count > getItemProps(output.id).maxStack)) return;
    if (m_cursor.empty()) m_cursor = output;
    else m_cursor.count += output.count;
    for (auto& ingredient : m_crafting) {
        if (!ingredient.empty() && --ingredient.count == 0) ingredient.clear();
    }
}

void SurvivalInventoryScreen::performClick(int button, int mouseX, int mouseY) {
    if (button != GLFW_MOUSE_BUTTON_LEFT && button != GLFW_MOUSE_BUTTON_RIGHT) return;
    const bool right = button == GLFW_MOUSE_BUTTON_RIGHT;
    if (contains(m_outputRect, mouseX, mouseY)) {
        if (!right) takeCraftingOutput();
        return;
    }
    for (size_t i = 0; i < m_inventoryRects.size(); ++i) {
        if (contains(m_inventoryRects[i], mouseX, mouseY)) {
            clickStack(m_inventory.slot(i), right);
            return;
        }
    }
    for (size_t i = 0; i < m_armorRects.size(); ++i) {
        if (contains(m_armorRects[i], mouseX, mouseY)) {
            if (m_cursor.empty() || acceptsArmor(i, m_cursor.id))
                clickStack(m_inventory.armor()[i], right);
            return;
        }
    }
    if (contains(m_offhandRect, mouseX, mouseY)) {
        clickStack(m_inventory.offhand(), right);
        return;
    }
    const size_t craftSlots = m_craftingTable ? 9 : 4;
    for (size_t i = 0; i < craftSlots; ++i) {
        if (contains(m_craftingRects[i], mouseX, mouseY)) {
            clickStack(m_crafting[i], right);
            return;
        }
    }
}

void SurvivalInventoryScreen::onMouseButton(
    int button, int action, int mouseX, int mouseY) {
    if (button != GLFW_MOUSE_BUTTON_LEFT && button != GLFW_MOUSE_BUTTON_RIGHT) return;
    if (action == GLFW_PRESS) {
        m_pointerPressed = true;
        m_pressedButton = button;
        m_pressX = mouseX;
        m_pressY = mouseY;
        return;
    }
    if (action != GLFW_RELEASE || !m_pointerPressed || button != m_pressedButton)
        return;

    const int deltaX = mouseX - m_pressX;
    const int deltaY = mouseY - m_pressY;
    const bool dragged = deltaX * deltaX + deltaY * deltaY >= 16;
    if (dragged) {
        // Pick up from the press position, then place at the release position.
        performClick(button, m_pressX, m_pressY);
        performClick(button, mouseX, mouseY);
    } else {
        // A normal click changes the cursor stack exactly once.
        performClick(button, mouseX, mouseY);
    }
    m_pointerPressed = false;
    m_pressedButton = -1;
}

bool SurvivalInventoryScreen::acceptsArmor(size_t slot, ItemId item) {
    if (getItemProps(item).kind != ItemKind::Armor) return false;
    const uint16_t relative = static_cast<uint16_t>(item) -
                              static_cast<uint16_t>(ItemId::LEATHER_HELMET);
    return relative % 4 == slot;
}

void SurvivalInventoryScreen::onClose() {
    m_pointerPressed = false;
    m_pressedButton = -1;
    for (auto& stack : m_crafting) {
        if (!stack.empty() && m_inventory.add(stack) == 0) stack.clear();
    }
    if (!m_cursor.empty() && m_inventory.add(m_cursor) == 0) m_cursor.clear();
}
