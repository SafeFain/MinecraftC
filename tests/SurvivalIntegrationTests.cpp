#include "game/SurvivalRules.h"
#include "game/SurvivalSession.h"
#include "game/SaveStore.h"
#include "game/InventoryTransaction.h"
#include "world/WorldGenContext.h"
#include "world/BlockEntityLogic.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
namespace {
void require(bool value,const char* message) {
    if(!value) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
struct Progression {
    InventoryModel inventory;
    InventoryTransaction window;
    uint64_t sequence=0;
    void action(InventoryOperation operation, InventorySlot slot={}, bool alternate=false, bool table=false) {
        InventoryAction request;
        request.sequence=++sequence; request.revision=window.revision();
        request.operation=operation; request.slot=slot; request.alternate=alternate;
        InventoryContext context; context.craftingTable=table;
        const auto result=window.apply(inventory,request,context);
        require(result.accepted && result.drops.empty(),"progression transaction accepted without dropped materials");
    }
    void craft(std::initializer_list<ItemId> grid, ItemId output, int outputCount, bool table=false) {
        std::map<ItemId,uint32_t> before, consumed;
        for(const auto id:grid) if(id!=ItemId::EMPTY) {before[id]=inventory.count(id); ++consumed[id];}
        const auto previousOutput=inventory.count(output);
        uint8_t index=0;
        for(const auto id:grid) {
            if(id!=ItemId::EMPTY) {
                size_t storage=0;
                while(storage<InventoryModel::STORAGE_SIZE && inventory.slot(storage).id!=id) ++storage;
                require(storage<InventoryModel::STORAGE_SIZE,"crafting uses harvested or previously crafted materials");
                action(InventoryOperation::Click,{InventoryArea::Storage,static_cast<uint8_t>(storage)},false,table);
                action(InventoryOperation::Click,{InventoryArea::Crafting,index},true,table);
                if(!window.cursor().empty()) action(InventoryOperation::Click,{InventoryArea::Storage,static_cast<uint8_t>(storage)},false,table);
            }
            ++index;
        }
        action(InventoryOperation::Craft,{},false,table);
        require(window.cursor().id==output && window.cursor().count==outputCount,"craft produces the expected item quantity");
        require(window.close(inventory).empty(),"crafted output returns to real inventory");
        for(const auto& entry:consumed)
            require(inventory.count(entry.first)==before[entry.first]-entry.second,"craft consumes exactly one material per occupied slot");
        require(inventory.count(output)==previousOutput+outputCount,"craft does not duplicate or lose output");
    }
    void harvest(BlockId block, ItemId tool, int count) {
        ItemStack held;
        if(tool!=ItemId::EMPTY) {
            for(const auto& slot:inventory.storage()) if(slot.id==tool) held=slot;
            require(!held.empty(),"harvesting requires a tool actually crafted into storage");
        }
        for(int i=0;i<count;++i) {
            const auto drops=getBlockDrops(block,held);
            require(!drops.empty(),"progression tool yields harvest drops");
            for(const auto& drop:drops) require(inventory.add(drop)==0,"harvest fits inventory");
        }
    }
};
}
int main() {
    Progression game;
    game.harvest(BlockId::WOOD,ItemId::EMPTY,6);
    for(int i=0;i<6;++i) game.craft({ItemId::OAK_LOG},ItemId::OAK_PLANKS,4);
    for(int i=0;i<3;++i) game.craft({ItemId::OAK_PLANKS,ItemId::EMPTY,ItemId::OAK_PLANKS},ItemId::STICK,4);
    game.craft({ItemId::OAK_PLANKS,ItemId::OAK_PLANKS,ItemId::OAK_PLANKS,ItemId::OAK_PLANKS},ItemId::CRAFTING_TABLE,1);
    game.craft({ItemId::OAK_PLANKS,ItemId::OAK_PLANKS,ItemId::OAK_PLANKS,ItemId::EMPTY,ItemId::STICK,ItemId::EMPTY,ItemId::EMPTY,ItemId::STICK},ItemId::WOODEN_PICKAXE,1,true);
    game.harvest(BlockId::STONE,ItemId::WOODEN_PICKAXE,11);
    game.craft({ItemId::COBBLESTONE,ItemId::COBBLESTONE,ItemId::COBBLESTONE,ItemId::EMPTY,ItemId::STICK,ItemId::EMPTY,ItemId::EMPTY,ItemId::STICK},ItemId::STONE_PICKAXE,1,true);
    game.craft({ItemId::COBBLESTONE,ItemId::COBBLESTONE,ItemId::COBBLESTONE,ItemId::COBBLESTONE,ItemId::EMPTY,ItemId::COBBLESTONE,ItemId::COBBLESTONE,ItemId::COBBLESTONE,ItemId::COBBLESTONE},ItemId::FURNACE,1,true);
    game.harvest(BlockId::IRON_ORE,ItemId::STONE_PICKAXE,3);
    game.harvest(BlockId::COAL_ORE,ItemId::STONE_PICKAXE,1);
    require(game.inventory.remove(ItemId::RAW_IRON,3) && game.inventory.remove(ItemId::COAL,1),"furnace inputs leave inventory");
    PersistedBlockEntity furnace; furnace.localIndex=22; furnace.value.type=BlockEntityType::Furnace;
    furnace.value.input={ItemId::RAW_IRON,3,0}; furnace.value.fuel={ItemId::COAL,1,0};
    for(int i=0;i<100;++i) tickFurnace(furnace.value);
    require(furnace.value.cookProgress==100 && furnace.value.output.empty(),"live furnace is saved halfway through its first recipe");
    const auto root=std::filesystem::temp_directory_path()/("minecraftc-progression-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {std::filesystem::path root; ~Cleanup(){std::error_code error;std::filesystem::remove_all(root,error);}} cleanup{root};
    SaveStore store(root);
    store.saveBlockEntities(-1,2,{furnace});
    auto loaded=store.loadBlockEntities(-1,2);
    require(loaded.size()==1 && loaded[0].localIndex==22 && loaded[0].value.cookProgress==100 &&
            loaded[0].value.burnRemaining==furnace.value.burnRemaining && loaded[0].value.input.count==3 && loaded[0].value.fuel.empty(),"reload preserves actual cook and fuel state");
    for(int i=0;i<500;++i) tickFurnace(loaded[0].value);
    require(loaded[0].value.input.empty() && loaded[0].value.output.id==ItemId::IRON_INGOT && loaded[0].value.output.count==3,"reloaded furnace continues all three smelts");
    require(game.inventory.add(loaded[0].value.output)==0,"smelted output enters real storage"); loaded[0].value.output.clear();
    game.craft({ItemId::IRON_INGOT,ItemId::IRON_INGOT,ItemId::IRON_INGOT,ItemId::EMPTY,ItemId::STICK,ItemId::EMPTY,ItemId::EMPTY,ItemId::STICK},ItemId::IRON_PICKAXE,1,true);
    game.harvest(BlockId::DIAMOND_ORE,ItemId::IRON_PICKAXE,3);
    game.inventory.armor()[1]={ItemId::IRON_CHESTPLATE,1,7};
    game.inventory.offhand()={ItemId::SHIELD,1,2};
    const auto original=game.inventory;
    const auto drops=takeDeathDrops(game.inventory);
    for(const auto& slot:game.inventory.storage()) require(slot.empty(),"death drains every storage slot");
    for(const auto& slot:game.inventory.armor()) require(slot.empty(),"death drains armor");
    require(game.inventory.offhand().empty(),"death drains offhand");
    require(chooseRespawnPosition({1,70,2},glm::ivec3{9,65,-4},true)==glm::ivec3(9,65,-4) &&
            chooseRespawnPosition({1,70,2},glm::ivec3{9,65,-4},false)==glm::ivec3(1,70,2),
            "valid bed wins and invalid bed falls back to world spawn after death");
    InventoryModel recovered;
    for(auto drop:drops) require(pickupItemStack(recovered,drop) && drop.empty(),"every actual death drop is picked up completely");
    for(uint16_t raw=0;raw<static_cast<uint16_t>(ItemId::COUNT);++raw) {
        const auto id=static_cast<ItemId>(raw);
        uint32_t expected=original.count(id);
        for(const auto& armor:original.armor()) if(armor.id==id) expected+=armor.count;
        if(original.offhand().id==id) expected+=original.offhand().count;
        require(recovered.count(id)==expected,"death and pickup conserve all item quantities");
    }
    bool chestplate=false,shield=false;
    for(const auto& slot:recovered.storage()) {if(slot.id==ItemId::IRON_CHESTPLATE) chestplate=slot.damage==7; if(slot.id==ItemId::SHIELD) shield=slot.damage==2;}
    require(chestplate && shield,"death pickup preserves equipment durability");
    WorldMetadata metadata; metadata.displayName="Progression"; metadata.seed=42;
    metadata.generationVersion=WorldGenContext::GENERATION_VERSION; metadata.inventory=recovered;
    store.saveMetadata(metadata);
    require(store.loadMetadata().inventory.count(ItemId::DIAMOND)==3 && store.loadMetadata().inventory.count(ItemId::IRON_PICKAXE)==1,"recovered crafted progression survives reload");
    std::cout<<"Survival progression integration tests passed\n";
}
