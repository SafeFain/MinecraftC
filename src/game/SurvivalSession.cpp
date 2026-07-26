#include "game/SurvivalSession.h"

std::vector<ItemStack> takeDeathDrops(InventoryModel& inventory) {
    std::vector<ItemStack> result;
    for (const auto& stack : inventory.storage()) if (!stack.empty()) result.push_back(stack);
    for (const auto& stack : inventory.armor()) if (!stack.empty()) result.push_back(stack);
    if (!inventory.offhand().empty()) result.push_back(inventory.offhand());
    inventory.clear();
    return result;
}

glm::ivec3 chooseRespawnPosition(const glm::ivec3& worldSpawn,
                                 const std::optional<glm::ivec3>& bedSpawn,
                                 bool bedValid) {
    return bedValid && bedSpawn ? *bedSpawn : worldSpawn;
}

bool pickupItemStack(InventoryModel& inventory, ItemStack& stack) {
    if (stack.empty()) return true;
    const uint32_t remaining = inventory.add(stack);
    stack.count = static_cast<uint8_t>(remaining);
    if (remaining == 0) stack.clear();
    return remaining == 0;
}
