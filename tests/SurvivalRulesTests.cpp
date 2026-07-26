#include "game/SurvivalRules.h"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    const ItemStack hand{};
    const ItemStack woodenPick{ItemId::WOODEN_PICKAXE, 1, 0};
    const ItemStack stonePick{ItemId::STONE_PICKAXE, 1, 0};
    const ItemStack ironPick{ItemId::IRON_PICKAXE, 1, 0};

    require(getBlockDrops(BlockId::STONE, hand).empty(),
            "stone requires a pickaxe");
    require(getBlockDrops(BlockId::STONE, woodenPick)[0].id == ItemId::COBBLESTONE,
            "stone drops cobblestone with a wooden pickaxe");
    require(getBlockDrops(BlockId::IRON_ORE, woodenPick).empty(),
            "wood cannot harvest iron");
    require(getBlockDrops(BlockId::IRON_ORE, stonePick)[0].id == ItemId::RAW_IRON,
            "stone tier harvests raw iron");
    require(getBlockDrops(BlockId::DIAMOND_ORE, stonePick).empty(),
            "stone cannot harvest diamond");
    require(getBlockDrops(BlockId::DIAMOND_ORE, ironPick)[0].id == ItemId::DIAMOND,
            "iron tier harvests diamond");
    require(getBlockDrops(BlockId::BEDROCK, ironPick).empty(),
            "bedrock is unbreakable");
    require(miningSeconds(BlockId::STONE, ironPick) <
            miningSeconds(BlockId::STONE, woodenPick),
            "higher tier pickaxes mine faster");
    require(miningSeconds(BlockId::STONE, ironPick, true, false) >
            miningSeconds(BlockId::STONE, ironPick, false, false),
            "underwater mining penalty is represented");

    std::array<ItemId, 9> grid{};
    grid.fill(ItemId::EMPTY);
    grid[0] = ItemId::OAK_PLANKS;
    grid[1] = ItemId::OAK_PLANKS;
    grid[2] = ItemId::OAK_PLANKS;
    grid[4] = ItemId::STICK;
    grid[7] = ItemId::STICK;
    const auto* pickaxe = findCraftingRecipe(grid, 3, 3);
    require(pickaxe && pickaxe->output.id == ItemId::WOODEN_PICKAXE,
            "shaped wooden pickaxe recipe matches");

    grid.fill(ItemId::EMPTY);
    grid[4] = ItemId::BIRCH_LOG;
    const auto* planks = findCraftingRecipe(grid, 3, 3);
    require(planks && planks->output.id == ItemId::OAK_PLANKS &&
            planks->output.count == 4,
            "recipe matching permits offsets and log variants");

    require(findSmeltingRecipe(ItemId::RAW_IRON)->output.id == ItemId::IRON_INGOT,
            "raw iron smelts to an ingot");
    require(fuelTicks(ItemId::COAL) == 1600, "coal smelts eight items");
    require(fuelTicks(ItemId::DIAMOND) == 0, "non-fuels are rejected");

    std::cout << "Survival rules tests passed\n";
    return 0;
}

