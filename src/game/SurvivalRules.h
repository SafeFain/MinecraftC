#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "game/Item.h"

class InventoryModel;

struct BlockSurvivalProperties {
    float hardness = 1.0f;
    ToolKind preferredTool = ToolKind::None;
    ToolTier minimumHarvestTier = ToolTier::None;
    bool unbreakable = false;
};

struct CraftingRecipe {
    uint8_t width = 0;
    uint8_t height = 0;
    std::array<ItemId, 9> ingredients{};
    ItemStack output;
    bool allowMirror = true;
};

struct SmeltingRecipe {
    ItemId input = ItemId::EMPTY;
    ItemStack output;
    uint16_t cookTicks = 200;
};

const BlockSurvivalProperties& getBlockSurvivalProps(BlockId block);
std::vector<ItemStack> getBlockDrops(BlockId block, const ItemStack& tool,
                                     uint32_t randomValue = 0);
float miningSeconds(BlockId block, const ItemStack& tool, bool underwater = false,
                    bool airborne = false);

const std::vector<CraftingRecipe>& craftingRecipes();
const CraftingRecipe* findCraftingRecipe(const std::array<ItemId, 9>& grid,
                                         uint8_t gridWidth, uint8_t gridHeight);
const SmeltingRecipe* findSmeltingRecipe(ItemId input);
uint16_t fuelTicks(ItemId fuel);

int armorPointsForItem(ItemId item);
int totalArmorPoints(const InventoryModel& inventory);
bool canTillBlock(ItemId tool, BlockId target, int faceNormalY);
float durabilityRemaining(const ItemStack& stack);
