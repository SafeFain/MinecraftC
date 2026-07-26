#pragma once
#include <vector>
#include "game/Item.h"
namespace InventoryInteraction {
void click(ItemStack& cursor, ItemStack& slot, bool rightClick);
uint32_t transfer(ItemStack& source, const std::vector<ItemStack*>& targets);
void gather(ItemStack& cursor, const std::vector<ItemStack*>& sources);
void distribute(ItemStack& cursor, const std::vector<ItemStack*>& targets, bool oneEach);
}
