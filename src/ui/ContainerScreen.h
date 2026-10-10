#pragma once

#include <array>
#include <functional>
#include <vector>
#include <glm/glm.hpp>

#include "game/InventoryModel.h"
#include "game/InventoryCommands.h"
#include "core/InputCodes.h"
#include "ui/UILayout.h"

class UIRenderer;
class IContainerAccess;

class ContainerScreen {
public:
    explicit ContainerScreen(InventoryModel& inventory) : m_inventory(inventory) {}
    bool open(IContainerAccess& access, const glm::ivec3& position);
    bool valid() const;
    void render(UIRenderer& ui, int width, int height, int mouseX, int mouseY);
    void onMouseButton(int button, ButtonAction action, int mouseX, int mouseY, int mods = 0);
    void onMouseMove(int mouseX, int mouseY);
    void onPointerCancel();
    void onGamepadNavigate(int dx, int dy);
    void onGamepadAction(int action);
    bool swapHoveredWithHotbar(int hotbarSlot);
    bool swapHoveredWithOffhand();
    ItemStack dropHovered(bool entireStack);
    void close(const std::function<void(ItemStack)>& drop);

void setInventoryCommands(IInventoryCommands* commands) { m_commands = commands; }

private:
    IInventoryCommands* m_commands = nullptr;
    bool commandsEnabled() const { return m_commands && m_commands->usesInventoryCommands(); }
    void syncCommands();
    std::optional<InventorySlot> logicalSlot(const ItemStack* stack) const;
    bool command(InventoryOperation operation, ItemStack* stack, uint16_t argument = 0, bool alternate = false);
    void gesture(InventoryOperation operation, const std::vector<ItemStack*>& targets, bool alternate = false);
    struct Rect { float x = 0, y = 0, w = 44, h = 44; };
    InventoryModel& m_inventory;
    IContainerAccess* m_access = nullptr;
    glm::ivec3 m_position{0};
    ItemStack m_cursor;
    std::array<Rect, 27> m_containerRects{};
    std::array<Rect, InventoryModel::STORAGE_SIZE> m_inventoryRects{};
    bool m_pressed = false;
    int m_button = -1;
    int m_pressX = 0, m_pressY = 0;
    int m_pressMods = 0;
    double m_lastClickSeconds = -1.0;
    std::vector<ItemStack*> m_dragTargets;
    bool m_cursorHeldAtPress = false;
    int m_focusX = 0, m_focusY = 0;
    int m_focusIndex = 0;
    bool m_gamepadFocus = false;
    std::array<UiFeedback, 64> m_slotFeedback{};
    std::vector<Rect> focusRects() const;
    void updateFocusPosition();
    int m_pointerX = 0, m_pointerY = 0;

    float m_layoutScale = 1.0f;
    Rect m_panelRect{};
    void layout(int width, int height);
    void click(int button, int x, int y);
    void quickMove(int x, int y);
    ItemStack* hoveredStack(int x, int y);
    static bool contains(const Rect& rect, int x, int y);
    static void drawStack(UIRenderer& ui, const Rect& rect,
                          const ItemStack& stack, bool hovered, bool selected = false, UiFeedback* feedback = nullptr);
    static void moveStack(ItemStack& cursor, ItemStack& slot, bool right);
};
