#pragma once

#include "game/Item.h"
#include "game/StatusEffects.h"
#include "world/BlockEntity.h"

struct PotionDefinition {
    ActiveStatusEffect effect;
    bool splash=false;
};

std::optional<PotionDefinition> potionDefinition(ItemId);
std::optional<ItemId> brewPotion(ItemId bottle,ItemId ingredient);
bool isBrewingBottle(ItemId);
bool isBrewingIngredient(ItemId);
bool tickBrewingStand(BlockEntity&);
ItemStack smithingResult(const ItemStack& pattern,const ItemStack& equipment,const ItemStack& material);
bool takeSmithingResult(BlockEntity&,ItemStack& destination);
bool isNetheriteEquipment(ItemId);
