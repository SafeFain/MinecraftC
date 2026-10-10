#pragma once

#include "game/InventoryTransaction.h"
#include <optional>
#include <glm/glm.hpp>

enum class InventoryWindowKind : uint8_t { Closed, Player, CraftingTable, Container };
struct InventoryWindowView {
    uint64_t id = 0, revision = 0, containerRevision = 0;
    InventoryWindowKind kind = InventoryWindowKind::Closed;
    glm::ivec3 position{0};
    ItemStack cursor;
    std::array<ItemStack, 9> crafting{};
    std::optional<BlockEntity> container;
};

// Screens read snapshots and submit logical gestures. Authority owns all stacks,
// including the cursor; screen coordinates never cross the network.
class IInventoryCommands {
public:
    virtual ~IInventoryCommands() = default;
    virtual bool usesInventoryCommands() const = 0;
    virtual InventoryWindowView inventoryWindow() const = 0;
    virtual bool inventoryWindowPending() const { return false; }
    virtual void openInventoryWindow(InventoryWindowKind kind, glm::ivec3 position = {}) = 0;
    virtual void submitInventoryAction(InventoryAction action) = 0;
    virtual void closeInventoryWindow() = 0;
};
