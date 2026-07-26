#pragma once

#include <array>
#include <functional>
#include <glm/glm.hpp>

#include "game/InventoryModel.h"

class UIRenderer;
class World;

class ContainerScreen {
public:
    explicit ContainerScreen(InventoryModel& inventory) : m_inventory(inventory) {}
    bool open(World& world, const glm::ivec3& position);
    bool valid() const;
    void render(UIRenderer& ui, int width, int height, int mouseX, int mouseY);
    void onMouseButton(int button, int action, int mouseX, int mouseY);
    void close(const std::function<void(ItemStack)>& drop);

private:
    struct Rect { float x = 0, y = 0, w = 44, h = 44; };
    InventoryModel& m_inventory;
    World* m_world = nullptr;
    glm::ivec3 m_position{0};
    ItemStack m_cursor;
    std::array<Rect, 27> m_containerRects{};
    std::array<Rect, InventoryModel::STORAGE_SIZE> m_inventoryRects{};
    bool m_pressed = false;
    int m_button = -1;
    int m_pressX = 0, m_pressY = 0;

    void layout(int width, int height);
    void click(int button, int x, int y);
    static bool contains(const Rect& rect, int x, int y);
    static void drawStack(UIRenderer& ui, const Rect& rect,
                          const ItemStack& stack, bool hovered);
    static void moveStack(ItemStack& cursor, ItemStack& slot, bool right);
};
