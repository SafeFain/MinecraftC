#include "game/SurvivalRules.h"
#include "game/SurvivalSession.h"
#include "game/SaveStore.h"
#include "world/WorldGenContext.h"
#include "game/SurvivalBlockLogic.h"
#include "world/BlockEntityLogic.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace { void require(bool v,const char* m){if(!v){std::cerr<<"FAILED: "<<m<<'\n';std::exit(1);}} }

int main(){
    InventoryModel inventory;
    inventory.slot(0)={ItemId::OAK_LOG,4,0};
    std::array<ItemId,9> grid{};grid.fill(ItemId::EMPTY);grid[0]=ItemId::OAK_LOG;
    const auto* planks=findCraftingRecipe(grid,2,2);
    require(planks&&planks->output.id==ItemId::OAK_PLANKS,"logs begin progression");
    inventory.add({ItemId::OAK_PLANKS,16,0});
    grid.fill(ItemId::EMPTY);grid[0]=grid[1]=grid[2]=grid[3]=ItemId::OAK_PLANKS;
    const auto* table=findCraftingRecipe(grid,2,2);
    require(table&&table->output.id==ItemId::CRAFTING_TABLE,
            "crafting table progression recipe works");
    grid.fill(ItemId::EMPTY);grid[0]=grid[1]=grid[2]=ItemId::OAK_PLANKS;
    grid[4]=grid[7]=ItemId::STICK;
    const auto* woodenPick=findCraftingRecipe(grid,3,3);
    require(woodenPick&&woodenPick->output.id==ItemId::WOODEN_PICKAXE,
            "wooden pickaxe progression recipe works");
    require(getBlockDrops(BlockId::STONE,{ItemId::WOODEN_PICKAXE,1,0})[0].id==ItemId::COBBLESTONE,
            "wooden pick obtains cobblestone");
    require(getBlockDrops(BlockId::IRON_ORE,{ItemId::STONE_PICKAXE,1,0})[0].id==ItemId::RAW_IRON,
            "stone pick obtains raw iron");
    require(findSmeltingRecipe(ItemId::RAW_IRON)->output.id==ItemId::IRON_INGOT&&
            fuelTicks(ItemId::COAL)>=200,"iron can be smelted with coal");
    require(nextFarmlandState(BlockId::FARMLAND_2,BlockId::AIR,true,0)==BlockId::FARMLAND_7&&
            nextFarmlandState(BlockId::FARMLAND,BlockId::AIR,false,0)==BlockId::DIRT,
            "water hydrates farmland and dry unused soil reverts");
    require(nextCropState(BlockId::WHEAT_3,BlockId::FARMLAND_7,30)==BlockId::WHEAT_4&&
            nextCropState(BlockId::WHEAT_3,BlockId::FARMLAND,30)==BlockId::WHEAT_3,
            "hydrated crops use the faster deterministic growth interval");
    require(getBlockDrops(BlockId::DIAMOND_ORE,{ItemId::IRON_PICKAXE,1,0})[0].id==ItemId::DIAMOND,
            "iron pick completes diamond progression");

    inventory.slot(1)={ItemId::DIAMOND,3,0};
    inventory.armor()[1]={ItemId::IRON_CHESTPLATE,1,7};
    inventory.offhand()={ItemId::SHIELD,1,2};
    const auto drops=takeDeathDrops(inventory);
    require(drops.size()>=3&&inventory.count(ItemId::DIAMOND)==0&&inventory.offhand().empty(),
            "death drains storage armor and offhand into drops");
    InventoryModel recovered;ItemStack itemEntity{ItemId::DIAMOND,3,0};
    require(pickupItemStack(recovered,itemEntity)&&itemEntity.empty()&&
            recovered.count(ItemId::DIAMOND)==3,"item entity pickup restores its complete stack");
    require(chooseRespawnPosition({1,70,2},glm::ivec3{9,65,-4},true)==glm::ivec3(9,65,-4)&&
            chooseRespawnPosition({1,70,2},glm::ivec3{9,65,-4},false)==glm::ivec3(1,70,2),
            "valid beds win and invalid beds fall back to world spawn");

    const auto root=std::filesystem::temp_directory_path()/"minecraftc-progression-integration";
    std::filesystem::remove_all(root);SaveStore store(root);
    WorldMetadata metadata;metadata.displayName="Progression";metadata.seed=42;
    metadata.generationVersion=WorldGenContext::GENERATION_VERSION;
    for(const auto& drop:drops)metadata.inventory.add(drop);
    store.saveMetadata(metadata);
    PersistedBlockEntity furnace;furnace.localIndex=22;furnace.value.type=BlockEntityType::Furnace;
    furnace.value.input={ItemId::RAW_IRON,2,0};furnace.value.fuel={ItemId::COAL,1,0};
    furnace.value.output={ItemId::IRON_INGOT,1,0};furnace.value.cookProgress=100;
    BlockEntity liveFurnace=furnace.value;
    for(int tick=0;tick<100;++tick)tickFurnace(liveFurnace);
    require(liveFurnace.output.count==2&&liveFurnace.input.count==1,
            "loaded furnace completes a live smelting cycle");
    store.saveBlockEntities(-1,2,{furnace});
    store.saveChunkOverrides(-1,2,{{0,BlockId::AIR},{513,BlockId::FARMLAND_7},{514,BlockId::WHEAT_4}});
    require(store.loadMetadata().inventory.count(ItemId::DIAMOND)==3,
            "recovered progression inventory survives reload");
    require(store.loadBlockEntities(-1,2)[0].value.cookProgress==100,
            "live furnace state survives negative chunk reload");
    const auto overrides=store.loadChunkOverrides(-1,2);
    require(overrides.size()==3&&overrides[0].block==BlockId::AIR&&overrides[2].block==BlockId::WHEAT_4,
            "farm and explicit air overrides survive reload");
    std::filesystem::remove_all(root);
    std::cout<<"Survival progression integration tests passed\n";
}
