#include "game/SurvivalRules.h"
#include "game/InventoryModel.h"
#include "world/FluidLogic.h"

#include <cstdlib>
#include <cmath>
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
    static_assert(static_cast<uint8_t>(BlockId::FLOWING_LAVA_7) == 88);
    static_assert(static_cast<uint8_t>(BlockId::LIMESTONE) == 89);
    static_assert(static_cast<uint8_t>(BlockId::BASALT) == 90);
    static_assert(static_cast<uint8_t>(BlockId::TUFF) == 91);
    static_assert(static_cast<uint8_t>(BlockId::COARSE_DIRT) == 92);
    static_assert(static_cast<uint8_t>(BlockId::MUD) == 93);
    static_assert(static_cast<uint8_t>(BlockId::PACKED_ICE) == 94);
    static_assert(static_cast<uint8_t>(BlockId::BLACK_SAND) == 95);
    static_assert(static_cast<uint8_t>(BlockId::GRANITE) == 96);
    static_assert(static_cast<uint16_t>(ItemId::BLASTLING_SPAWN_EGG) == 134);
    static_assert(static_cast<uint16_t>(ItemId::LIMESTONE) == 135);
    static_assert(static_cast<uint16_t>(ItemId::GRANITE) == 142);
    static_assert(static_cast<uint16_t>(ItemId::AETHER_GRASS) == 143);
    static_assert(static_cast<uint16_t>(ItemId::STARFLOWER) == 150);
    static_assert(static_cast<uint16_t>(ItemId::CLOUD_BLOOM) == 151);
    static_assert(static_cast<uint16_t>(ItemId::GLOWSHROOM) == 152);
    for (const auto& mapping : {
             std::pair{BlockId::LIMESTONE, ItemId::LIMESTONE},
             std::pair{BlockId::BASALT, ItemId::BASALT},
             std::pair{BlockId::TUFF, ItemId::TUFF},
             std::pair{BlockId::COARSE_DIRT, ItemId::COARSE_DIRT},
             std::pair{BlockId::MUD, ItemId::MUD},
             std::pair{BlockId::PACKED_ICE, ItemId::PACKED_ICE},
             std::pair{BlockId::BLACK_SAND, ItemId::BLACK_SAND},
             std::pair{BlockId::GRANITE, ItemId::GRANITE}}) {
        require(itemForBlock(mapping.first) == mapping.second,
                "v7 natural block did not map to its appended item");
        const auto drops = getBlockDrops(
            mapping.first, {ItemId::WOODEN_PICKAXE, 1, 0}, 0);
        require(drops.size() == 1 && drops.front().id == mapping.second,
                "v7 natural block did not drop itself");
    }
    const ItemStack hand{};
    const ItemStack woodenShovel{ItemId::WOODEN_SHOVEL, 1, 0};
    const ItemStack woodenAxe{ItemId::WOODEN_AXE, 1, 0};
    const ItemStack stoneHarvest{ItemId::STONE_PICKAXE, 1, 0};
    for (const auto& mapping : {
             std::pair{BlockId::AETHER_GRASS, ItemId::AETHER_GRASS},
             std::pair{BlockId::AETHER_SOIL, ItemId::AETHER_SOIL},
             std::pair{BlockId::CLOUDSTONE, ItemId::CLOUDSTONE},
             std::pair{BlockId::SUNSTONE, ItemId::SUNSTONE},
             std::pair{BlockId::SKYROOT_WOOD, ItemId::SKYROOT_LOG},
             std::pair{BlockId::SKYROOT_LEAVES, ItemId::SKYROOT_LEAVES},
             std::pair{BlockId::STAR_CRYSTAL, ItemId::STAR_CRYSTAL},
             std::pair{BlockId::STARFLOWER, ItemId::STARFLOWER},
             std::pair{BlockId::CLOUD_BLOOM, ItemId::CLOUD_BLOOM},
             std::pair{BlockId::GLOWSHROOM, ItemId::GLOWSHROOM}}) {
        const ItemStack& tool =
            mapping.first == BlockId::AETHER_GRASS ||
            mapping.first == BlockId::AETHER_SOIL ? woodenShovel :
            mapping.first == BlockId::SKYROOT_WOOD ? woodenAxe :
            (mapping.first == BlockId::STARFLOWER ||
             mapping.first == BlockId::CLOUD_BLOOM ||
             mapping.first == BlockId::GLOWSHROOM ||
             mapping.first == BlockId::SKYROOT_LEAVES) ? hand : stoneHarvest;
        const auto drops = getBlockDrops(mapping.first, tool, 0);
        require(drops.size() == 1 && drops.front().id == mapping.second,
                "Heaven block did not drop its appended inventory item");
    }
    require(canTillBlock(ItemId::WOODEN_HOE,BlockId::GRASS,1)&&
            canTillBlock(ItemId::DIAMOND_HOE,BlockId::DIRT,1),
            "hoes can till the top face of grass and dirt in shared gameplay logic");
    require(!canTillBlock(ItemId::WOODEN_PICKAXE,BlockId::DIRT,1)&&
            !canTillBlock(ItemId::WOODEN_HOE,BlockId::STONE,1)&&
            !canTillBlock(ItemId::WOODEN_HOE,BlockId::DIRT,0),
            "tilling rejects non-hoes, invalid blocks, and side faces");
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
    require(getBlockDrops(BlockId::WHITE_BED_HEAD_WEST, hand).size() == 1 &&
            getBlockDrops(BlockId::WHITE_BED_HEAD_WEST, hand)[0].id ==
                ItemId::WHITE_BED,
            "either serialized bed half drops one bed item");
    require(miningSeconds(BlockId::STONE, ironPick) <
            miningSeconds(BlockId::STONE, woodenPick),
            "higher tier pickaxes mine faster");
    require(miningSeconds(BlockId::STONE, ironPick, true, false) >
            miningSeconds(BlockId::STONE, ironPick, false, false),
            "underwater mining penalty is represented");
    require(static_cast<uint8_t>(BlockId::WHEAT_7) == 51 &&
            static_cast<uint16_t>(ItemId::FLINT) < static_cast<uint16_t>(ItemId::OAK_SAPLING),
            "new serialized ids append after existing values");
    for (uint8_t moisture = 0; moisture <= 7; ++moisture) {
        const BlockId farmland = farmlandForMoisture(moisture);
        require(isFarmland(farmland) && farmlandMoisture(farmland) == moisture,
                "farmland moisture state did not round trip");
    }
    require(itemForBlock(BlockId::BIRCH_SAPLING) == ItemId::BIRCH_SAPLING,
            "birch sapling block maps to its stable item");
    require(getBlockDrops(BlockId::SPRUCE_LEAVES, hand, 0)[0].id ==
                ItemId::SPRUCE_SAPLING &&
            getBlockDrops(BlockId::SPRUCE_LEAVES, hand, 1).empty(),
            "leaves use the five-percent matching sapling drop");

    InventoryModel armorInventory;
    armorInventory.armor()[0] = {ItemId::DIAMOND_HELMET, 1, 0};
    armorInventory.armor()[1] = {ItemId::DIAMOND_CHESTPLATE, 1, 0};
    require(totalArmorPoints(armorInventory) == 11,
            "shared armor points match combat values");
    require(std::abs(durabilityRemaining({ItemId::WOODEN_PICKAXE, 1, 0}) - 1.0f) < 0.001f &&
            durabilityRemaining({ItemId::WOODEN_PICKAXE, 1, 59}) == 0.0f,
            "durability bar fraction handles full and exhausted tools");

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
    require(findSmeltingRecipe(ItemId::SAND)->output.id == ItemId::GLASS,
            "sand smelts into generated glass");
    grid.fill(ItemId::EMPTY);
    for (size_t i = 0; i < grid.size(); ++i)
        grid[i] = (i % 2 == 0) ? ItemId::GUNPOWDER : ItemId::SAND;
    const auto* tnt = findCraftingRecipe(grid, 3, 3);
    require(tnt && tnt->output.id == ItemId::TNT,
            "alternating gunpowder and sand crafts TNT");
    grid.fill(ItemId::EMPTY);
    grid[0] = ItemId::FLINT;
    grid[1] = ItemId::IRON_INGOT;
    const auto* igniter = findCraftingRecipe(grid, 2, 1);
    require(igniter && igniter->output.id == ItemId::FLINT_AND_STEEL,
            "flint and iron craft flint and steel");
    struct ArchitecturalRecipeCase {
        ItemId material;
        ItemId slab;
        ItemId stairs;
    };
    for (const ArchitecturalRecipeCase recipeCase : {
             ArchitecturalRecipeCase{ItemId::OAK_PLANKS, ItemId::OAK_PLANKS_SLAB,
                                     ItemId::OAK_PLANKS_STAIRS},
             {ItemId::COBBLESTONE, ItemId::COBBLESTONE_SLAB,
              ItemId::COBBLESTONE_STAIRS},
             {ItemId::TERRACOTTA, ItemId::TERRACOTTA_SLAB,
              ItemId::TERRACOTTA_STAIRS},
             {ItemId::SUNSTONE, ItemId::SUNSTONE_SLAB, ItemId::SUNSTONE_STAIRS},
             {ItemId::CLOUDSTONE, ItemId::CLOUDSTONE_SLAB,
              ItemId::CLOUDSTONE_STAIRS}}) {
        grid.fill(ItemId::EMPTY);
        grid[3] = grid[4] = grid[5] = recipeCase.material;
        const auto* slabRecipe = findCraftingRecipe(grid, 3, 3);
        require(slabRecipe && slabRecipe->output.id == recipeCase.slab &&
                    slabRecipe->output.count == 6,
                "architectural material does not craft six slabs");
        grid.fill(ItemId::EMPTY);
        grid[0] = recipeCase.material;
        grid[3] = grid[4] = recipeCase.material;
        grid[6] = grid[7] = grid[8] = recipeCase.material;
        const auto* stairRecipe = findCraftingRecipe(grid, 3, 3);
        require(stairRecipe && stairRecipe->output.id == recipeCase.stairs &&
                    stairRecipe->output.count == 4,
                "architectural material does not craft four stairs");
    }
    require(isWater(BlockId::FLOWING_WATER_7) && fluidLevel(BlockId::WATER) == 0 &&
            fluidLevel(BlockId::FLOWING_LAVA_4) == 4,
            "serialized fluid states retain material and level semantics");
    require(nextFluidLevel(false,0)==1&&nextFluidLevel(false,6)==7&&
            nextFluidLevel(true,0)==2&&nextFluidLevel(true,6)==8,
            "water spreads seven flat cells while overworld lava spreads three");
    require(isDerivedFluidState(BlockId::FLOWING_WATER_3)&&
            isDerivedFluidState(BlockId::FLOWING_LAVA_7)&&
            !isDerivedFluidState(BlockId::WATER)&&!isDerivedFluidState(BlockId::LAVA),
            "legacy cleanup distinguishes rebuildable flows from persisted sources");
    require(fluidTickDelay(false)==5&&fluidTickDelay(true)==30,
            "water and overworld lava use Minecraft tick delays");
    const FluidAvailable available=[](const glm::ivec3&){return true;};
    const FluidSample nearestDrop=[](const glm::ivec3& p){
        if(p==glm::ivec3(1,0,0))return BlockId::AIR;
        if(p.y==1&&std::abs(p.x)+std::abs(p.z)==1)return BlockId::AIR;
        return BlockId::STONE;
    };
    const auto preferred=preferredFluidDirections(
        {0,1,0},false,0,nearestDrop,available);
    require(preferred.size()==1&&preferred[0]==glm::ivec3(1,0,0),
            "fluid routing chooses the nearest reachable downward path");
    const FluidSample flat=[](const glm::ivec3& p){
        return p.y==1&&std::abs(p.x)+std::abs(p.z)==1
            ? BlockId::AIR : BlockId::STONE;
    };
    require(preferredFluidDirections({0,1,0},false,0,flat,available).size()==4,
            "fluid spreads evenly when no direction has a nearer drop");
    const FluidSample enclosed=[](const glm::ivec3&){return BlockId::STONE;};
    require(preferredFluidDirections({0,1,0},false,0,enclosed,available).empty(),
            "enclosed fluids do not report blocked spread directions");
    require(fuelTicks(ItemId::COAL) == 1600, "coal smelts eight items");
    require(fuelTicks(ItemId::DIAMOND) == 0, "non-fuels are rejected");

    std::cout << "Survival rules tests passed\n";
    return 0;
}
