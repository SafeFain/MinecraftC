#pragma once
#include <optional>
#include <vector>
#include <glm/glm.hpp>
#include "game/InventoryModel.h"

std::vector<ItemStack> takeDeathDrops(InventoryModel& inventory);
glm::ivec3 chooseRespawnPosition(const glm::ivec3& worldSpawn,
                                 const std::optional<glm::ivec3>& bedSpawn,
                                 bool bedValid);
bool pickupItemStack(InventoryModel& inventory, ItemStack& entityStack);
