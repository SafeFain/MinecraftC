#include "game/InventoryTransaction.h"
#include "game/SurvivalRules.h"
#include <cstdlib>
#include <iostream>

namespace {
void require(bool value,const char* reason) { if(!value) {std::cerr<<reason<<'\n';std::exit(1);} }
InventoryAction action(const InventoryTransaction& window,uint64_t sequence,InventoryOperation operation,InventorySlot slot={}) {
    InventoryAction result;result.sequence=sequence;result.revision=window.revision();result.operation=operation;result.slot=slot;return result;
}
}
int main() {
    InventoryModel inventory;inventory.slot(0)={ItemId::STONE,32,0};InventoryTransaction window;InventoryContext context;
    auto take=action(window,1,InventoryOperation::Click);require(window.apply(inventory,take,context).accepted,"Pick up failed");
    require(window.cursor().count==32 && inventory.slot(0).empty(),"Cursor is not authoritative");
    require(!window.apply(inventory,take,context).accepted,"Replayed action accepted");
    auto invalid=action(window,2,InventoryOperation::Click,{InventoryArea::Armor,0});
    require(!window.apply(inventory,invalid,context).accepted && window.cursor().count==32 && inventory.armor()[0].empty(),"Invalid armor insertion was not atomic");
    auto distribute=action(window,3,InventoryOperation::Distribute);distribute.targets={{InventoryArea::Storage,1},{InventoryArea::Storage,1}};
    require(!window.apply(inventory,distribute,context).accepted && inventory.slot(1).empty(),"Repeated drag slot accepted");
    auto close=action(window,4,InventoryOperation::Close);require(window.apply(inventory,close,context).accepted,"Close failed");
    require(inventory.count(ItemId::STONE)==32 && window.cursor().empty(),"Closing duplicated/lost cursor items");

    BlockEntity chest;chest.chest[0]={ItemId::DIAMOND,1,0};InventoryContext shared{false,false,&chest,7};
    InventoryModel one,two;InventoryTransaction first,second;
    auto pickup=action(first,1,InventoryOperation::Click,{InventoryArea::Container,0});pickup.containerRevision=7;
    const auto taken=first.apply(one,pickup,shared);require(taken.accepted && taken.containerChanged,"Chest extraction failed");
    shared.containerRevision=8;auto stale=action(second,1,InventoryOperation::Click,{InventoryArea::Container,0});stale.containerRevision=7;
    require(!second.apply(two,stale,shared).accepted && second.cursor().empty(),"Concurrent chest request duplicated item");
    pickup=action(second,2,InventoryOperation::Click,{InventoryArea::Container,0});pickup.containerRevision=8;
    require(second.apply(two,pickup,shared).accepted && second.cursor().empty(),"Chest resync failed");
    close=action(first,2,InventoryOperation::Close);close.containerRevision=8;
    require(first.apply(one,close,shared).accepted && one.count(ItemId::DIAMOND)==1 && two.count(ItemId::DIAMOND)==0,"Shared chest conservation failed");

    BlockEntity furnace;furnace.type=BlockEntityType::Furnace;furnace.output={ItemId::IRON_INGOT,3,0};InventoryContext furnaceContext{false,false,&furnace,1};
    inventory.slot(0)={ItemId::STONE,10,0};auto swap=action(window,5,InventoryOperation::SwapHotbar,{InventoryArea::Container,2});swap.containerRevision=1;
    require(!window.apply(inventory,swap,furnaceContext).accepted && furnace.output.count==3 && inventory.slot(0).count==10,"Furnace output accepts inserted items");
    InventoryModel furnaceInventory;InventoryTransaction furnaceWindow;
    auto furnaceTake=action(furnaceWindow,1,InventoryOperation::Click,{InventoryArea::Container,2});
    furnaceTake.containerRevision=1;furnaceTake.alternate=true;
    require(furnaceWindow.apply(furnaceInventory,furnaceTake,furnaceContext).accepted && furnaceWindow.cursor().count==2 && furnace.output.count==1,
            "Right-click furnace output did not preserve half-stack behavior");
    furnaceWindow.close(furnaceInventory);
    auto grant=action(window,6,InventoryOperation::CreativeGrant);grant.argument=static_cast<uint16_t>(ItemId::DIAMOND);
    require(!window.apply(inventory,grant,context).accepted,"Survival creative grant accepted");
    context.creative=true;grant.sequence=7;require(window.apply(inventory,grant,context).accepted && inventory.slot(0).count==64,"Creative grant failed");

    InventoryModel craftingInventory;InventoryTransaction crafting;craftingInventory.slot(0)={ItemId::OAK_LOG,2,0};context.creative=false;
    auto pick=action(crafting,1,InventoryOperation::Click);require(crafting.apply(craftingInventory,pick,context).accepted,"Crafting pick failed");
    auto place=action(crafting,2,InventoryOperation::Click,{InventoryArea::Crafting,0});require(crafting.apply(craftingInventory,place,context).accepted,"Crafting insert failed");
    auto craft=action(crafting,3,InventoryOperation::Craft);require(crafting.apply(craftingInventory,craft,context).accepted,"Log recipe failed");
    require(crafting.cursor().id==ItemId::OAK_PLANKS && crafting.cursor().count==4 && crafting.crafting()[0].count==1,"Crafting did not consume exactly one ingredient");
    InventoryModel fullInventory;InventoryTransaction disconnectWindow;
    fullInventory.slot(0)={ItemId::DIAMOND,2,0};
    auto hold=action(disconnectWindow,UINT64_MAX,InventoryOperation::Click);
    require(disconnectWindow.apply(fullInventory,hold,context).accepted,"Cleanup setup failed");
    for(size_t i=0;i<36;++i) fullInventory.slot(i)={ItemId::STONE,64,0};
    const auto cleanupDrops=disconnectWindow.close(fullInventory);
    require(cleanupDrops.size()==1 && cleanupDrops[0].id==ItemId::DIAMOND && cleanupDrops[0].count==2 && disconnectWindow.cursor().empty(),
            "Disconnect cleanup lost/duplicated held items or depended on exhausted sequence");
    require(fullInventory.count(ItemId::STONE)==36*64,"Cleanup changed unrelated full storage");
    auto armorMove = action(window, 8, InventoryOperation::QuickMove, {InventoryArea::Storage, 2});
    inventory.slot(2) = {ItemId::IRON_HELMET, 1, 0};
    require(window.apply(inventory, armorMove, context).accepted && inventory.armor()[0].id == ItemId::IRON_HELMET && inventory.slot(2).empty(),
            "Quick move did not equip matching empty armor slot");
    auto clone = action(window, 9, InventoryOperation::CreativeClone, {InventoryArea::Storage, 0});
    require(!window.apply(inventory, clone, context).accepted, "Survival creative clone accepted");
    context.creative = true; clone.sequence = 10;
    require(window.apply(inventory, clone, context).accepted && window.cursor().id == ItemId::DIAMOND && window.cursor().count == 64,
            "Creative clone did not create an authority-owned cursor");
    InventoryModel packed; InventoryTransaction bulk;
    packed.slot(0) = {ItemId::OAK_LOG, 1, 0};
    require(bulk.apply(packed, action(bulk, 1, InventoryOperation::Click), {}).accepted, "Bulk crafting pickup failed");
    require(bulk.apply(packed, action(bulk, 2, InventoryOperation::Click, {InventoryArea::Crafting, 0}), {}).accepted, "Bulk crafting insertion failed");
    for (size_t i = 0; i < 36; ++i) packed.slot(i) = {ItemId::STONE, 64, 0};
    packed.slot(0) = {ItemId::OAK_PLANKS, 63, 0};
    require(!bulk.apply(packed, action(bulk, 3, InventoryOperation::QuickMove, {InventoryArea::Output, 0}), {}).accepted &&
            packed.slot(0).count == 63 && bulk.crafting()[0].count == 1,
            "Partial craft output insertion duplicated output without consuming ingredients");
    packed.slot(0).count = 60;
    require(bulk.apply(packed, action(bulk, 4, InventoryOperation::QuickMove, {InventoryArea::Output, 0}), {}).accepted &&
            packed.slot(0).count == 64 && bulk.crafting()[0].empty(), "Bulk craft did not consume output atomically");
    std::cout<<"Authoritative inventory transaction tests passed\n";
}
