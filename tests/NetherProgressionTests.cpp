#include "game/NetherProgression.h"
#include "game/InventoryTransaction.h"
#include "game/SurvivalRules.h"
#include "network/StatusEffectProtocol.h"
#include "world/BlockEntityLogic.h"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool passed,const char* message){if(!passed)throw std::runtime_error(message);}
}

int main() {
    for(const auto& pair:std::array<std::pair<ItemId,ItemId>,4>{{
        {ItemId::MAGMA_CREAM,ItemId::FIRE_RESISTANCE_POTION},
        {ItemId::BLAZE_POWDER,ItemId::STRENGTH_POTION},
        {ItemId::GHAST_TEAR,ItemId::REGENERATION_POTION},
        {ItemId::GLISTERING_MELON,ItemId::HEALING_POTION}}})
        check(brewPotion(ItemId::AWKWARD_POTION,pair.first)==pair.second,"awkward potion progression");
    check(brewPotion(ItemId::WATER_BOTTLE,ItemId::NETHER_WART)==ItemId::AWKWARD_POTION,"water to awkward");
    check(brewPotion(ItemId::WATER_BOTTLE,ItemId::FERMENTED_SPIDER_EYE)==ItemId::WEAKNESS_POTION,"weakness base");
    check(brewPotion(ItemId::HEALING_POTION,ItemId::FERMENTED_SPIDER_EYE)==ItemId::HARMING_POTION,"healing corruption");
    check(!brewPotion(ItemId::WATER_BOTTLE,ItemId::BLAZE_POWDER),"invalid brewing accepted");
    check(!brewPotion(ItemId::LONG_STRENGTH_POTION,ItemId::GLOWSTONE_DUST),"extended and strong potion combined");
    for(uint16_t raw=static_cast<uint16_t>(ItemId::FIRE_RESISTANCE_POTION);raw<=static_cast<uint16_t>(ItemId::LONG_WEAKNESS_POTION);++raw) {
        const auto item=static_cast<ItemId>(raw);
        const auto splash=brewPotion(item,ItemId::GUNPOWDER);
        check(splash&&potionDefinition(*splash)->splash,"splash conversion absent");
        check(potionDefinition(item)->effect.remaining==potionDefinition(*splash)->effect.remaining,"splash duration changed");
        check(!brewPotion(*splash,ItemId::GUNPOWDER),"splash potion duplicated");
    }
    check(brewPotion(ItemId::SPLASH_STRENGTH_POTION,ItemId::REDSTONE_DUST)==ItemId::SPLASH_LONG_STRENGTH_POTION,"splash extension");
    BlockEntity brewer;brewer.type=BlockEntityType::BrewingStand;
    for(int i=0;i<3;++i)brewer.chest[i]={ItemId::AWKWARD_POTION,1,0};
    brewer.chest[3]={ItemId::MAGMA_CREAM,2,0};brewer.chest[4]={ItemId::BLAZE_POWDER,1,0};
    for(int i=0;i<399;++i)tickBrewingStand(brewer);
    check(brewer.chest[0].id==ItemId::AWKWARD_POTION,"brewing finished early");
    tickBrewingStand(brewer);
    for(int i=0;i<3;++i)check(brewer.chest[i].id==ItemId::FIRE_RESISTANCE_POTION,"three-bottle brew not atomic");
    check(brewer.chest[3].count==1&&brewer.chest[4].empty()&&brewer.burnRemaining==19,"brewing ingredient/fuel count");
    check(!tickBrewingStand(brewer),"non-recipe consumed fuel");
    auto recipe=smithingResult({ItemId::NETHERITE_UPGRADE_TEMPLATE,1,0},{ItemId::DIAMOND_PICKAXE,1,417},{ItemId::NETHERITE_INGOT,1,0});
    check(recipe.id==ItemId::NETHERITE_PICKAXE&&recipe.damage==417,"smithing lost durability");
    check(findSmeltingRecipe(ItemId::ANCIENT_DEBRIS)->output.id==ItemId::NETHERITE_SCRAP,"debris smelting");
    check(miningSeconds(BlockId::ANCIENT_DEBRIS,{ItemId::NETHERITE_PICKAXE,1,0})>0,"netherite harvesting");
    BlockEntity smith;smith.type=BlockEntityType::SmithingTable;
    smith.chest[0]={ItemId::NETHERITE_UPGRADE_TEMPLATE,2,0};smith.chest[1]={ItemId::DIAMOND_SWORD,1,21};smith.chest[2]={ItemId::NETHERITE_INGOT,2,0};
    InventoryModel inventory;InventoryTransaction transaction;
    InventoryContext context;context.container=&smith;
    InventoryAction action;action.sequence=1;action.operation=InventoryOperation::Click;action.slot={InventoryArea::Container,3};
    check(transaction.apply(inventory,action,context).accepted,"smith output click rejected");
    check(transaction.cursor().id==ItemId::NETHERITE_SWORD&&transaction.cursor().damage==21,"smith output result");
    check(smith.chest[0].count==1&&smith.chest[1].empty()&&smith.chest[2].count==1,"smith output did not consume inputs atomically");
    check(!transaction.apply(inventory,action,context).accepted,"replayed smith action duplicated item");
    action.sequence=2;action.revision=transaction.revision();
    check(!transaction.apply(inventory,action,context).accepted,"empty smith input generated output");
    for(int material=7;material<=8;++material) {
        DoorState door;door.material=static_cast<DoorMaterial>(material);door.upper=true;door.open=true;door.rightHinge=true;
        DoorState decoded;check(decodeDoor(doorBlock(door),decoded)&&decoded.material==door.material&&decoded.upper&&decoded.open&&decoded.rightHinge,"native wood door roundtrip");
        check(fireEncouragement(doorBlock(door))==0&&fuelTicks(plankItem(door.material))==0,"native wood burns");
        ButtonState button;button.material=door.material;button.pressed=true;ButtonState decodedButton;
        check(decodeButton(buttonBlock(button),decodedButton)&&decodedButton.material==button.material&&decodedButton.pressed,"native button roundtrip");
    }
    for(int material=5;material<9;++material)for(auto half:{BlockHalf::Bottom,BlockHalf::Top}) {
        const auto mat=static_cast<ArchitecturalMaterial>(material);
        ArchitecturalBlockState decoded;
        check(decodeArchitecturalBlock(slabBlock(mat,half),decoded)&&decoded.material==mat&&decoded.half==half,"native slab roundtrip");
        for(int direction=0;direction<4;++direction)check(decodeArchitecturalBlock(stairBlock(mat,half,static_cast<BedDirection>(direction)),decoded)&&decoded.material==mat&&decoded.half==half&&static_cast<int>(decoded.direction)==direction,"native stair roundtrip");
    }
    for(int charge=0;charge<=4;++charge)check(respawnAnchorCharge(respawnAnchorBlock(charge))==charge&&itemForBlock(respawnAnchorBlock(charge))==ItemId::RESPAWN_ANCHOR,"anchor charge state identity");
    StatusEffects effects;effects.apply(StatusEffectType::Regeneration,0,100);
    float healing=0;for(int i=0;i<100;++i)effects.tick([&](float n){healing+=n;},[](float){});
    check(healing==2&&effects.active().empty(),"regeneration rate/expiry");
    effects.apply(StatusEffectType::Strength,1,80);effects.apply(StatusEffectType::Strength,0,160);
    check(effects.level(StatusEffectType::Strength)==2&&effects.active()[0].remaining==80,"weaker effect replaced stronger effect");
    Lan::Writer writer;Lan::writeStatusEffects(writer,effects.active());Lan::Reader reader(writer.bytes);
    const auto decoded=Lan::readStatusEffects(reader);reader.finish();
    check(decoded.size()==1&&decoded[0].remaining==80&&decoded[0].amplifier==1,"effect protocol roundtrip");
    auto duplicate=decoded;duplicate.push_back(decoded.front());check(!validStatusEffects(duplicate),"duplicate effects accepted");
    std::cout<<"Native Nether brewing, smithing transactions and effects passed\n";
}
