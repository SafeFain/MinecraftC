#pragma once

#include <array>
#include <vector>

#include "game/InventoryModel.h"
#include "core/InputCodes.h"

class UIRenderer;

class SurvivalInventoryScreen {
public:
    explicit SurvivalInventoryScreen(InventoryModel& inventory)
        : m_inventory(inventory) {}

    void render(UIRenderer& ui, int screenWidth, int screenHeight,
                int mouseX, int mouseY);
    void onMouseButton(int button, ButtonAction action, int mouseX, int mouseY, int mods = 0);
    void onMouseMove(int mouseX, int mouseY);
    void onGamepadNavigate(int dx, int dy);
    void onGamepadAction(int action);
    void onClose();
    void setCraftingTable(bool enabled) { m_craftingTable = enabled; }
    void setCreativeAccess(bool enabled) { m_creativeAccess = enabled; }
    bool creativeCatalogButtonContains(int x, int y) const;

private:
    struct Rect { float x = 0, y = 0, w = 44, h = 44; };

    InventoryModel& m_inventory;
    std::array<ItemStack, 9> m_crafting{};
    ItemStack m_cursor;
    std::array<Rect, InventoryModel::STORAGE_SIZE> m_inventoryRects{};
    std::array<Rect, 9> m_craftingRects{};
    std::array<Rect, InventoryModel::ARMOR_SIZE> m_armorRects{};
    Rect m_offhandRect{};
    Rect m_outputRect{};
    Rect m_creativeCatalogRect{};
    bool m_craftingTable = false;
    bool m_creativeAccess = false;
    bool m_pointerPressed = false;
    int m_pressedButton = -1;
    int m_pressX = 0;
    int m_pressY = 0;
    int m_pressMods = 0;
    double m_lastClickSeconds = -1.0;
    std::vector<ItemStack*> m_dragTargets;
    bool m_cursorHeldAtPress = false;
    int m_focusX = 0, m_focusY = 0;

    void layout(int screenWidth, int screenHeight);
    ItemStack craftingOutput() const;
    void takeCraftingOutput();
    void clickStack(ItemStack& stack, bool rightClick);
    void performClick(int button, int mouseX, int mouseY);
    void quickMove(int mouseX, int mouseY);
    static bool contains(const Rect& rect, int x, int y);
    static void drawStack(UIRenderer& ui, const Rect& rect,
                          const ItemStack& stack, bool hovered);
    static bool acceptsArmor(size_t slot, ItemId item);
};
