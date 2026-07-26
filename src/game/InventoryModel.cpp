#include "game/InventoryModel.h"

#include <algorithm>

uint32_t InventoryModel::count(ItemId id) const {
    uint32_t total = 0;
    for (const auto& stack : m_storage) {
        if (stack.id == id) total += stack.count;
    }
    return total;
}

uint32_t InventoryModel::add(ItemStack incoming) {
    if (incoming.empty() || !isValidItemId(incoming.id)) return 0;
    const uint8_t maxStack = getItemProps(incoming.id).maxStack;
    if (maxStack == 0) return incoming.count;

    for (auto& stack : m_storage) {
        if (incoming.count == 0) break;
        if (stack.id != incoming.id || stack.damage != incoming.damage ||
            stack.count >= maxStack) continue;
        const uint8_t moved = static_cast<uint8_t>(
            std::min<int>(incoming.count, maxStack - stack.count));
        stack.count += moved;
        incoming.count -= moved;
    }
    for (auto& stack : m_storage) {
        if (incoming.count == 0) break;
        if (!stack.empty()) continue;
        const uint8_t moved = std::min(incoming.count, maxStack);
        stack = {incoming.id, moved, incoming.damage};
        incoming.count -= moved;
    }
    return incoming.count;
}

bool InventoryModel::remove(ItemId id, uint32_t requested) {
    if (count(id) < requested) return false;
    for (auto& stack : m_storage) {
        if (requested == 0) break;
        if (stack.id != id) continue;
        const uint8_t removed = static_cast<uint8_t>(
            std::min<uint32_t>(stack.count, requested));
        stack.count -= removed;
        requested -= removed;
        if (stack.count == 0) stack.clear();
    }
    return true;
}

void InventoryModel::clear() {
    for (auto& stack : m_storage) stack.clear();
    for (auto& stack : m_armor) stack.clear();
    m_offhand.clear();
}
