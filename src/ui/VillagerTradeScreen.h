#pragma once

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

#include "core/InputCodes.h"
#include "game/InventoryModel.h"

class EntityManager;
class UIRenderer;

class VillagerTradeScreen {
public:
    explicit VillagerTradeScreen(InventoryModel& inventory)
        : m_inventory(inventory) {}

    bool open(EntityManager& entities, uint64_t entityId);
    bool valid(const glm::dvec3& eye, const glm::vec3& direction) const;
    void close();
    void render(UIRenderer& ui, int width, int height, int mouseX, int mouseY);
    void onMouseButton(int button, ButtonAction action, int mouseX, int mouseY);
    void onGamepadNavigate(int dx, int dy);
    void onGamepadAction();

private:
    struct Rect { float x = 0, y = 0, w = 44, h = 44; };
    InventoryModel& m_inventory;
    EntityManager* m_entities = nullptr;
    uint64_t m_entityId = 0;
    uint8_t m_selected = 0;
    std::array<Rect, 5> m_rows{};
    std::array<Rect, 5> m_outputs{};

    void layout(int width, int height);
    void executeSelected();
    static bool contains(const Rect& rect, int x, int y);
    static void drawStack(UIRenderer& ui, const Rect& rect,
                          const ItemStack& stack, bool highlighted);
};
