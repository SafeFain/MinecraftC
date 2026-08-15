#pragma once

#include "world/Block.h"
#include "game/InventoryModel.h"

class UIRenderer;

class Hotbar {
public:
    Hotbar();

    void render(UIRenderer& ui, int screenWidth, int screenHeight);

    void onScroll(double yoffset);
    void onKeyPress(int key);

    int getSelectedSlot() const { return m_selectedSlot; }
    ItemId getSelectedItem() const {
        return m_inventory ? m_inventory->slot(static_cast<size_t>(m_selectedSlot)).id
                           : ItemId::EMPTY;
    }
    void selectSlot(int index);
    void setInventory(const InventoryModel* inventory) {
        m_inventory = inventory;
    }
    const InventoryModel* inventory() const { return m_inventory; }

private:
    int m_selectedSlot = 0;
    const InventoryModel* m_inventory = nullptr;
};
