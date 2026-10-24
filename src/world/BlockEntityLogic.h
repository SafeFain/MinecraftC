#pragma once
#include "world/BlockEntity.h"
#include "game/SurvivalRules.h"
#include "game/NetherProgression.h"
bool tickFurnace(BlockEntity& entity);

inline std::optional<BlockEntityType> blockEntityTypeForBlock(BlockId block) {
    switch(block) {
        case BlockId::CHEST:return BlockEntityType::Chest;
        case BlockId::FURNACE:return BlockEntityType::Furnace;
        case BlockId::BREWING_STAND:return BlockEntityType::BrewingStand;
        case BlockId::SMITHING_TABLE:return BlockEntityType::SmithingTable;
        case BlockId::BLAZE_SPAWNER:case BlockId::MAGMA_SPAWNER:return BlockEntityType::Spawner;
        default:return {};
    }
}
inline bool isInventoryContainer(BlockEntityType type) {
    return type==BlockEntityType::Chest||type==BlockEntityType::Furnace||
        type==BlockEntityType::BrewingStand||type==BlockEntityType::SmithingTable;
}
inline int containerStackCount(BlockEntityType type) {
    switch(type) {
        case BlockEntityType::Chest:return 27;
        case BlockEntityType::Furnace:return 3;
        case BlockEntityType::BrewingStand:return 5;
        case BlockEntityType::SmithingTable:return 4;
        default:return 0;
    }
}
inline ItemStack* containerStack(BlockEntity& entity,int slot) {
    if(slot<0||slot>=containerStackCount(entity.type))return nullptr;
    if(entity.type==BlockEntityType::Furnace)return slot==0?&entity.input:slot==1?&entity.fuel:&entity.output;
    if(entity.type==BlockEntityType::SmithingTable&&slot==3)return &entity.output;
    return &entity.chest[static_cast<size_t>(slot)];
}
inline const ItemStack* containerStack(const BlockEntity& entity,int slot) {
    return containerStack(const_cast<BlockEntity&>(entity),slot);
}
inline bool containerAccepts(const BlockEntity& entity,int slot,ItemId id) {
    if(!containerStack(entity,slot))return false;
    if(id==ItemId::EMPTY)return true;
    switch(entity.type) {
        case BlockEntityType::Chest:return true;
        case BlockEntityType::Furnace:return slot==0?findSmeltingRecipe(id)!=nullptr:slot==1&&fuelTicks(id)>0;
        case BlockEntityType::BrewingStand:return slot<3?isBrewingBottle(id):slot==3?isBrewingIngredient(id):id==ItemId::BLAZE_POWDER;
        case BlockEntityType::SmithingTable:
            if(slot==0)return id==ItemId::NETHERITE_UPGRADE_TEMPLATE;
            if(slot==2)return id==ItemId::NETHERITE_INGOT;
            if(slot==1)return !smithingResult({ItemId::NETHERITE_UPGRADE_TEMPLATE,1,0},{id,1,0},{ItemId::NETHERITE_INGOT,1,0}).empty();
            return false;
        default:return false;
    }
}
