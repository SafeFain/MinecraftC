#pragma once

#include "world/Block.h"
#include "game/InventoryModel.h"
#include <array>

class UIRenderer;

class Hotbar {
public:
    Hotbar();

    void render(UIRenderer& ui, int screenWidth, int screenHeight);

    void onScroll(double yoffset);
    void onKeyPress(int key);

    int getSelectedSlot() const { return m_selectedSlot; }
    ItemId getSelectedItem() const { return m_slots[m_selectedSlot].id; }
    BlockId getSelectedBlock() const;
    void setSlotItem(int index, ItemId id);
    void selectSlot(int index);
    void setSurvivalInventory(const InventoryModel* inventory) {
        m_survivalInventory = inventory;
    }

private:
    std::array<ItemStack, 9> m_slots;
    int m_selectedSlot = 0;
    const InventoryModel* m_survivalInventory = nullptr;

    void initDefaultSlots();
};
