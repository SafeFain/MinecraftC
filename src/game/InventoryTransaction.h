#pragma once

#include "game/InventoryModel.h"
#include "world/BlockEntity.h"
#include <array>
#include <cstdint>
#include <vector>

// Gameplay-level inventory commands are independent of pointer coordinates and
// UI widgets. The authority owns cursor/crafting contents as well as storage.
enum class InventoryArea : uint8_t { Storage, Armor, Offhand, Crafting, Container, Output };
struct InventorySlot {
    InventoryArea area=InventoryArea::Storage;
    uint8_t index=0;
    bool operator==(const InventorySlot& other) const { return area==other.area && index==other.index; }
};
enum class InventoryOperation : uint8_t {
    Click, QuickMove, SwapHotbar, SwapOffhand, Drop, Gather, Distribute,
    Craft, FillRecipe, CreativeGrant, Close
};
struct InventoryAction {
    uint64_t sequence=0;
    uint64_t revision=0;
    uint64_t containerRevision=0;
    InventoryOperation operation=InventoryOperation::Click;
    InventorySlot slot;
    uint16_t argument=0;
    bool alternate=false;
    std::vector<InventorySlot> targets;
};
struct InventoryContext {
    bool creative=false;
    bool craftingTable=false;
    // The caller validates world, dimension, distance and current window before
    // providing this pointer, and serializes all actions on the main thread.
    BlockEntity* container=nullptr;
    uint64_t containerRevision=0;
};
struct InventoryOutcome {
    bool accepted=false;
    bool containerChanged=false;
    std::vector<ItemStack> drops;
};
class InventoryTransaction {
public:
    uint64_t revision() const { return m_revision; }
    const ItemStack& cursor() const { return m_cursor; }
    const std::array<ItemStack,9>& crafting() const { return m_crafting; }
    InventoryOutcome apply(InventoryModel& inventory,const InventoryAction& action,const InventoryContext& context);
    // Authority-side disconnect/death cleanup does not depend on a client's
    // last sequence or a container that may already have been destroyed.
    std::vector<ItemStack> close(InventoryModel& inventory);
private:
    uint64_t m_revision=0;
    uint64_t m_sequence=0;
    ItemStack m_cursor;
    std::array<ItemStack,9> m_crafting{};
};
