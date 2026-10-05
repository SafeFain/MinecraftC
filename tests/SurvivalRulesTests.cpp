#include "game/SurvivalRules.h"
#include "game/InventoryModel.h"
#include "world/FluidLogic.h"
#include "world/BiomeBlockLogic.h"

#include <cstdlib>
#include <algorithm>
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
    for(uint8_t material=0;material<7;++material) {
        const ItemId ingredient=material==6?ItemId::IRON_INGOT:plankItem(static_cast<DoorMaterial>(material));
        std::array<ItemId,9> grid{};
        for(int y=0;y<3;++y)for(int x=0;x<2;++x)grid[y*3+x]=ingredient;
        const auto* recipe=findCraftingRecipe(grid,3,3);
        require(recipe && recipe->output.id==static_cast<ItemId>(static_cast<uint16_t>(ItemId::OAK_DOOR)+material) &&
            recipe->output.count==3,"door recipe has wrong variant or count");
        if(material<6) {
            grid[0]=plankItem(static_cast<DoorMaterial>((material+1)%6));
            require(!findCraftingRecipe(grid,3,3),"mixed wood types crafted a door");
        }
        grid.fill(ItemId::EMPTY);grid[0]=material==6?ItemId::STONE:ingredient;
        recipe=findCraftingRecipe(grid,2,2);
        require(recipe && recipe->output.id==static_cast<ItemId>(static_cast<uint16_t>(ItemId::OAK_BUTTON)+material),
            "button recipe has wrong material");
    }

    const auto oreDrops=getBlockDrops(BlockId::STAR_CRYSTAL_ORE,
        {ItemId::WOODEN_PICKAXE,1,0});
    require(oreDrops.size()==1 && oreDrops[0].id==ItemId::STAR_CRYSTAL_SHARD &&
            oreDrops[0].count==2 && getBlockDrops(BlockId::STAR_CRYSTAL_ORE,{}).empty(),
            "star ore requires a pickaxe and drops two shards");
    const auto berries=getBlockDrops(BlockId::CLOUDBERRY_BUSH,{});
    require(berries.size()==1 && berries[0].id==ItemId::CLOUDBERRY && berries[0].count==2,
            "cloudberry bushes provide food rather than renewable bushes");
    require(getItemProps(ItemId::CLOUDBERRY).food==2 &&
            getItemProps(ItemId::ROASTED_CLOUDBERRY).food==4 &&
            getItemProps(ItemId::CLOUDBERRY_BREAD).food==7 &&
            getItemProps(ItemId::ROASTED_GLOWSHROOM).food==4,
            "Heaven food values match the resource package");
    require(findSmeltingRecipe(ItemId::CLOUDBERRY)->output.id==ItemId::ROASTED_CLOUDBERRY &&
            findSmeltingRecipe(ItemId::GLOWSHROOM)->output.id==ItemId::ROASTED_GLOWSHROOM,
            "Heaven foods use existing furnace recipes");
    require(supportsBiomePlant(BlockId::SKY_FERN,BlockId::AETHER_MOSS) &&
            supportsBiomePlant(BlockId::MOONFLOWER,BlockId::MOONSTONE) &&
            !supportsBiomePlant(BlockId::SKY_FERN,BlockId::STONE),
            "Heaven planting uses matching substrates");
    require(getLightEmission(BlockId::STAR_CRYSTAL_LAMP)==15 &&
            getLightEmission(BlockId::MOONFLOWER)==4,
            "Heaven lamp and moonflower emit registered light");
    std::array<ItemId,9> skyGrid{};
    skyGrid[0]=ItemId::SKYROOT_PLANKS; skyGrid[1]=ItemId::SKYROOT_PLANKS;
    skyGrid[2]=ItemId::SKYROOT_PLANKS; skyGrid[3]=ItemId::SKYROOT_PLANKS;
    const auto skyRecipe=findCraftingRecipe(skyGrid,2,2);
    require(skyRecipe && skyRecipe->output.id==ItemId::CRAFTING_TABLE,
            "skyroot planks support foundation crafting");
    InventoryModel mixedWood;
    mixedWood.add({ItemId::OAK_PLANKS,2,0});
    mixedWood.add({ItemId::SKYROOT_PLANKS,2,0});
    std::array<ItemStack,9> mixedGrid{};
    require(fillCraftingRecipe(*skyRecipe,mixedWood,mixedGrid,2,2),
            "recipe guide supports mixed normal and skyroot planks");
    int retainedSky=0, retainedOak=0;
    for (const auto& stack:mixedGrid) {
        retainedSky += stack.id==ItemId::SKYROOT_PLANKS ? stack.count : 0;
        retainedOak += stack.id==ItemId::OAK_PLANKS ? stack.count : 0;
    }
    require(retainedSky==2 && retainedOak==2,
            "recipe guide preserves wood identity while moving ingredients");
    for (uint16_t raw=225; raw<=252; ++raw) {
        const auto block=static_cast<BlockId>(raw);
        const auto item=itemForBlock(block);
        const bool decoration=isNaturalDecoration(block);
        const bool rock=!decoration && !isBiomeSoil(block);
        const auto drops=getBlockDrops(block,rock ? ItemStack{ItemId::WOODEN_PICKAXE,1,0} : ItemStack{});
        require(drops.size()==1 && drops[0].id==item && drops[0].count==1,
                "v16 blocks drop themselves with suitable tools");
        if (rock) require(getBlockDrops(block,{}).empty(),"new rock requires a pickaxe");
    }

    static_assert(static_cast<uint16_t>(BlockId::FLOWING_LAVA_7) == 88);
    static_assert(static_cast<uint16_t>(BlockId::LIMESTONE) == 89);
    static_assert(static_cast<uint16_t>(BlockId::BASALT) == 90);
    static_assert(static_cast<uint16_t>(BlockId::TUFF) == 91);
    static_assert(static_cast<uint16_t>(BlockId::COARSE_DIRT) == 92);
    static_assert(static_cast<uint16_t>(BlockId::MUD) == 93);
    static_assert(static_cast<uint16_t>(BlockId::PACKED_ICE) == 94);
    static_assert(static_cast<uint16_t>(BlockId::BLACK_SAND) == 95);
    static_assert(static_cast<uint16_t>(BlockId::GRANITE) == 96);
    static_assert(static_cast<uint16_t>(ItemId::BLASTLING_SPAWN_EGG) == 134);
    static_assert(static_cast<uint16_t>(ItemId::LIMESTONE) == 135);
    static_assert(static_cast<uint16_t>(ItemId::GRANITE) == 142);
    static_assert(static_cast<uint16_t>(ItemId::AETHER_GRASS) == 143);
    static_assert(static_cast<uint16_t>(ItemId::STARFLOWER) == 150);
    static_assert(static_cast<uint16_t>(ItemId::CLOUD_BLOOM) == 151);
    static_assert(static_cast<uint16_t>(ItemId::GLOWSHROOM) == 152);
    static_assert(static_cast<uint16_t>(ItemId::DRIPSTONE_BLOCK) == 175);
    static_assert(static_cast<uint16_t>(ItemId::POINTED_DRIPSTONE) == 176);
    static_assert(static_cast<uint16_t>(ItemId::SULFUR_CRUST) == 181);
    static_assert(static_cast<uint16_t>(ItemId::STARSTEP_SCEPTER) == 182);
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
    for (const auto& mapping : {
             std::pair{BlockId::DRIPSTONE_BLOCK, ItemId::DRIPSTONE_BLOCK},
             std::pair{BlockId::POINTED_DRIPSTONE_UP,
                       ItemId::POINTED_DRIPSTONE},
             std::pair{BlockId::POINTED_DRIPSTONE_DOWN,
                       ItemId::POINTED_DRIPSTONE},
             std::pair{BlockId::CALCITE, ItemId::CALCITE},
             std::pair{BlockId::HANGING_ROOTS, ItemId::HANGING_ROOTS},
             std::pair{BlockId::GLOW_FERN, ItemId::GLOW_FERN},
             std::pair{BlockId::RESONANT_CRYSTAL, ItemId::RESONANT_CRYSTAL},
             std::pair{BlockId::SULFUR_CRUST, ItemId::SULFUR_CRUST}}) {
        require(itemForBlock(mapping.first) == mapping.second,
                "v14 cave block did not map to its shared item");
        const auto drops = getBlockDrops(
            mapping.first, {ItemId::WOODEN_PICKAXE, 1, 0}, 0);
        require(drops.size() == 1 && drops.front().id == mapping.second,
                "v14 cave block did not drop its expected item");
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
    require(static_cast<uint16_t>(BlockId::WHEAT_7) == 51 &&
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

    InventoryModel guidedInventory;
    guidedInventory.add({ItemId::OAK_PLANKS, 4, 0});
    std::array<ItemStack, 9> guidedGrid{};
    const auto guided2x2 = availableCraftingRecipes(
        guidedInventory, guidedGrid, 2, 2);
    const auto hasGuidedOutput = [](const auto& recipes, ItemId output) {
        return std::any_of(recipes.begin(), recipes.end(), [output](const auto* recipe) {
            return recipe->output.id == output;
        });
    };
    require(hasGuidedOutput(guided2x2, ItemId::CRAFTING_TABLE) &&
            hasGuidedOutput(guided2x2, ItemId::STICK) &&
            !hasGuidedOutput(guided2x2, ItemId::WOODEN_PICKAXE),
            "recipe guide filters by materials and the active grid size");
    const auto* tableRecipe = *std::find_if(
        guided2x2.begin(), guided2x2.end(), [](const auto* recipe) {
            return recipe->output.id == ItemId::CRAFTING_TABLE;
        });
    require(fillCraftingRecipe(*tableRecipe, guidedInventory, guidedGrid, 2, 2) &&
            guidedInventory.count(ItemId::OAK_PLANKS) == 0 &&
            guidedGrid[0].id == ItemId::OAK_PLANKS &&
            guidedGrid[1].id == ItemId::OAK_PLANKS &&
            guidedGrid[2].id == ItemId::OAK_PLANKS &&
            guidedGrid[3].id == ItemId::OAK_PLANKS,
            "recipe guide consumes one set and fills the compact crafting grid");

    InventoryModel fullInventory;
    for (size_t i = 0; i < InventoryModel::STORAGE_SIZE; ++i)
        fullInventory.slot(i) = {ItemId::WOODEN_PICKAXE, 1,
                                 static_cast<uint16_t>(i)};
    std::array<ItemStack, 9> blockedGrid{};
    blockedGrid[0] = blockedGrid[1] = blockedGrid[3] = blockedGrid[4] =
        {ItemId::OAK_PLANKS, 1, 0};
    blockedGrid[8] = {ItemId::COBBLESTONE, 1, 0};
    const InventoryModel originalFullInventory = fullInventory;
    const auto originalBlockedGrid = blockedGrid;
    const auto stacksEqual = [](const auto& left, const auto& right) {
        return std::equal(left.begin(), left.end(), right.begin(),
            [](const ItemStack& a, const ItemStack& b) {
                return a.id == b.id && a.count == b.count &&
                       a.damage == b.damage;
            });
    };
    require(!fillCraftingRecipe(*tableRecipe, fullInventory, blockedGrid, 3, 3) &&
            stacksEqual(fullInventory.storage(), originalFullInventory.storage()) &&
            stacksEqual(blockedGrid, originalBlockedGrid),
            "recipe guide is atomic when unrelated grid items cannot be returned");

    grid.fill(ItemId::EMPTY);
    grid[4] = ItemId::BIRCH_LOG;
    const auto* planks = findCraftingRecipe(grid, 3, 3);
    require(planks && planks->output.id == ItemId::BIRCH_PLANKS &&
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
    grid.fill(ItemId::EMPTY);
    grid[0] = grid[1] = grid[2] = grid[3] = ItemId::POINTED_DRIPSTONE;
    const auto* dripstoneBlock = findCraftingRecipe(grid, 2, 2);
    require(dripstoneBlock &&
                dripstoneBlock->output.id == ItemId::DRIPSTONE_BLOCK &&
                dripstoneBlock->output.count == 1,
            "four pointed dripstones do not craft a dripstone block");
    grid.fill(ItemId::EMPTY);
    grid[0] = ItemId::DRIPSTONE_BLOCK;
    const auto* pointedDripstone = findCraftingRecipe(grid, 1, 1);
    require(pointedDripstone &&
                pointedDripstone->output.id == ItemId::POINTED_DRIPSTONE &&
                pointedDripstone->output.count == 4,
            "dripstone block does not unpack into four pointed dripstones");
    grid.fill(ItemId::EMPTY);
    grid[1] = ItemId::STAR_CRYSTAL;
    grid[3] = ItemId::STAR_CRYSTAL;
    grid[4] = ItemId::SUNSTONE;
    grid[5] = ItemId::STAR_CRYSTAL;
    grid[7] = ItemId::CLOUDSTONE;
    const auto* starstep = findCraftingRecipe(grid, 3, 3);
    require(starstep && starstep->output.id == ItemId::STARSTEP_SCEPTER &&
                starstep->output.count == 1,
            "starstep scepter Heaven-material recipe is missing");
    grid.fill(ItemId::EMPTY);
    grid[0] = ItemId::SKYROOT_LOG;
    const auto* skyrootPlanks = findCraftingRecipe(grid, 1, 1);
    require(skyrootPlanks && skyrootPlanks->output.id == ItemId::SKYROOT_PLANKS &&
                skyrootPlanks->output.count == 4,
            "skyroot logs do not provide Heaven-local crafting progression");
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

    // New IDs must remain complete across placement, mining, light and fire.
    static_assert(static_cast<uint16_t>(BlockId::STONE_BRICKS) == 183);
    static_assert(static_cast<uint16_t>(BlockId::BLACK_WOOL) == 200);
    static_assert(static_cast<uint16_t>(ItemId::STONE_BRICKS) == 183);
    static_assert(static_cast<uint16_t>(ItemId::BONE_MEAL) == 208);
    for (uint16_t raw = 183; raw <= 200; ++raw) {
        const auto block = static_cast<BlockId>(raw);
        const auto item = static_cast<ItemId>(raw);
        require(getItemProps(item).placedBlock == block && itemForBlock(block) == item,
                "crafted blocks have bidirectional item mappings");
        require(getBlockProps(block).shape == RenderShape::Cube &&
                isFullCollisionBlock(block) && getLightDampening(block) == 15 &&
                getLightEmission(block) == 0,
                "crafted decoration is a full opaque non-emissive cube");
        const auto drops = getBlockDrops(block, {ItemId::WOODEN_PICKAXE, 1, 0});
        require(drops.size() == 1 && drops[0].id == item && drops[0].count == 1,
                "crafted block drops itself with appropriate tool");
        if (raw < 196)
            require(getBlockDrops(block, {}).empty(), "masonry requires a pickaxe");
        else
            require(getBlockDrops(block, {}).size() == 1 &&
                    fireEncouragement(block) == fireEncouragement(BlockId::WHITE_WOOL) &&
                    burnOdds(block) == burnOdds(BlockId::WHITE_WOOL),
                    "colored wool retains white wool harvesting and flammability");
    }
    auto expectRecipe = [&](std::initializer_list<ItemId> ingredients,
                            uint8_t width, ItemId output, uint8_t count) {
        std::array<ItemId, 9> grid{};
        size_t i = 0;
        for (ItemId item : ingredients) { grid[(i/width)*3+i%width] = item; ++i; }
        const auto* recipe = findCraftingRecipe(grid, 3, 3);
        require(recipe && recipe->output.id == output && recipe->output.count == count,
                "decoration recipe resolves with intended quantity");
    };
    for (const auto& pair : {
        std::pair{ItemId::STONE, ItemId::STONE_BRICKS},
        std::pair{ItemId::GRANITE, ItemId::POLISHED_GRANITE},
        std::pair{ItemId::BASALT, ItemId::POLISHED_BASALT},
        std::pair{ItemId::LIMESTONE, ItemId::POLISHED_LIMESTONE},
        std::pair{ItemId::TUFF, ItemId::POLISHED_TUFF},
        std::pair{ItemId::DEEPSLATE, ItemId::DEEPSLATE_BRICKS},
        std::pair{ItemId::SANDSTONE, ItemId::CUT_SANDSTONE}})
        expectRecipe({pair.first,pair.first,pair.first,pair.first}, 2, pair.second, 4);
    expectRecipe({ItemId::STONE_BRICKS, ItemId::MOSS}, 2, ItemId::MOSSY_STONE_BRICKS, 1);
    expectRecipe({ItemId::MOSS, ItemId::STONE_BRICKS}, 2, ItemId::MOSSY_STONE_BRICKS, 1);
    expectRecipe({ItemId::STONE_BRICKS, ItemId::STONE_BRICKS}, 1, ItemId::CHISELED_STONE_BRICKS, 2);
    expectRecipe({ItemId::SAND,ItemId::SAND,ItemId::SAND,ItemId::SAND}, 2, ItemId::SANDSTONE, 1);
    expectRecipe({ItemId::CLAY}, 1, ItemId::CLAY_BALL, 4);
    expectRecipe({ItemId::CLAY_BALL,ItemId::CLAY_BALL,ItemId::CLAY_BALL,ItemId::CLAY_BALL}, 2, ItemId::CLAY, 1);
    expectRecipe({ItemId::BRICK,ItemId::BRICK,ItemId::BRICK,ItemId::BRICK}, 2, ItemId::BRICKS, 1);
    expectRecipe({ItemId::POPPY}, 1, ItemId::RED_DYE, 1);
    expectRecipe({ItemId::DANDELION}, 1, ItemId::YELLOW_DYE, 1);
    expectRecipe({ItemId::BLUE_ORCHID}, 1, ItemId::BLUE_DYE, 1);
    expectRecipe({ItemId::COAL}, 1, ItemId::BLACK_DYE, 1);
    expectRecipe({ItemId::BONE}, 1, ItemId::BONE_MEAL, 3);
    for (uint16_t color = 0; color < 5; ++color) {
        const auto dye = static_cast<ItemId>(201 + color + 2);
        const auto wool = static_cast<ItemId>(196 + color);
        expectRecipe({ItemId::WHITE_WOOL,dye}, 2, wool, 1);
        expectRecipe({dye,ItemId::WHITE_WOOL}, 2, wool, 1);
        expectRecipe({wool,ItemId::BONE_MEAL}, 2, ItemId::WHITE_WOOL, 1);
        expectRecipe({ItemId::BONE_MEAL,wool}, 2, ItemId::WHITE_WOOL, 1);
    }
    for (const auto& pair : {
        std::pair{ItemId::COBBLESTONE, ItemId::STONE},
        std::pair{ItemId::STONE_BRICKS, ItemId::CRACKED_STONE_BRICKS},
        std::pair{ItemId::SANDSTONE, ItemId::SMOOTH_SANDSTONE},
        std::pair{ItemId::CLAY_BALL, ItemId::BRICK},
        std::pair{ItemId::CACTUS, ItemId::GREEN_DYE}}) {
        const auto* recipe = findSmeltingRecipe(pair.first);
        require(recipe && recipe->output.id == pair.second && recipe->output.count == 1 &&
                recipe->cookTicks == 200, "decoration smelts using standard furnace timing");
    }
    InventoryModel insufficient;
    insufficient.add({ItemId::STONE,3,0});
    std::array<ItemStack,9> craftGrid{};
    const CraftingRecipe* brickRecipe = nullptr;
    for (const auto& recipe : craftingRecipes())
        if (recipe.output.id == ItemId::STONE_BRICKS) brickRecipe = &recipe;
    require(brickRecipe && !fillCraftingRecipe(*brickRecipe, insufficient, craftGrid, 2, 2) &&
            insufficient.count(ItemId::STONE) == 3,
            "insufficient crafting ingredients leave inventory intact");
    insufficient.add({ItemId::STONE,1,0});
    const auto decorationRecipes = availableCraftingRecipes(insufficient,craftGrid,2,2);
    require(std::find(decorationRecipes.begin(),decorationRecipes.end(),brickRecipe) != decorationRecipes.end() &&
            fillCraftingRecipe(*brickRecipe, insufficient, craftGrid, 2, 2),
            "new masonry recipe participates in guided crafting");

    std::cout << "Survival rules tests passed\n";
    return 0;
}
