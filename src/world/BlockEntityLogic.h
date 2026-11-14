#pragma once
#include "world/BlockEntity.h"
#include "world/RedstoneBlocks.h"
#include "game/SurvivalRules.h"
#include "game/NetherProgression.h"
bool tickFurnace(BlockEntity& entity);

inline std::optional<BlockEntityType> blockEntityTypeForBlock(BlockId block) {
    if(const auto state=Redstone::blockState(block)) {
        if(state->kind!=Redstone::Kind::Dust && state->kind!=Redstone::Kind::Lever && state->kind!=Redstone::Kind::Block)return BlockEntityType::Redstone;
    }
    switch(block) {
        case BlockId::CHEST:return BlockEntityType::Chest;
        case BlockId::FURNACE:return BlockEntityType::Furnace;
        case BlockId::BREWING_STAND:return BlockEntityType::BrewingStand;
        case BlockId::SMITHING_TABLE:return BlockEntityType::SmithingTable;
        case BlockId::BLAZE_SPAWNER:case BlockId::MAGMA_SPAWNER:return BlockEntityType::Spawner;
        default:return {};
    }
}
inline bool isWorkstation(BlockEntityType type) {
    return type >= BlockEntityType::EnchantingTable && type <= BlockEntityType::Grindstone;
}
inline std::optional<BlockEntityType> workstationTypeForBlock(BlockId block) {
    if (block == BlockId::ENCHANTING_TABLE) return BlockEntityType::EnchantingTable;
    if (isAnvil(block)) return BlockEntityType::Anvil;
    if (isGrindstone(block)) return BlockEntityType::Grindstone;
    return {};
}
inline bool isInventoryContainer(BlockEntityType type) {
    return isWorkstation(type)||type==BlockEntityType::Chest||type==BlockEntityType::Furnace||
        type==BlockEntityType::BrewingStand||type==BlockEntityType::SmithingTable;
}
inline int containerStackCount(BlockEntityType type) {
    if (isWorkstation(type)) return type == BlockEntityType::EnchantingTable ? 2 : 3;
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
    if (isWorkstation(entity.type)) return slot == 2 ? &entity.output : &entity.chest[static_cast<size_t>(slot)];
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
    if (isWorkstation(entity.type)) return slot < 2 && (entity.type != BlockEntityType::EnchantingTable || (slot == 1 ? id == ItemId::LAPIS_LAZULI : (id == ItemId::BOOK || getItemProps(id).maxDurability > 0)));
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
