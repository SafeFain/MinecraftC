#pragma once
#include "game/Item.h"
namespace Plugins {
// Serialized slots stay reserved even when their provider is absent.
inline bool heavenBlockSlot(BlockId id){
    const auto n=static_cast<uint16_t>(id);
    return (id>=BlockId::AETHER_GRASS&&id<=BlockId::GLOWSHROOM)||
        (id>=BlockId::SUNSTONE_SLAB_BOTTOM&&id<=BlockId::CLOUDSTONE_STAIRS_TOP_WEST)||
        (id>=BlockId::MOONSTONE&&id<=BlockId::HANGING_CLOUD_VINE)||
        (n>=static_cast<uint16_t>(BlockId::DOOR_FIRST)+5*64&&n<static_cast<uint16_t>(BlockId::DOOR_FIRST)+6*64)||
        (n>=static_cast<uint16_t>(BlockId::BUTTON_FIRST)+5*24&&n<static_cast<uint16_t>(BlockId::BUTTON_FIRST)+6*24);
}
inline bool heavenItemSlot(ItemId id){
    return (id>=ItemId::AETHER_GRASS&&id<=ItemId::GLOWSHROOM)||
        (id>=ItemId::SUNSTONE_SLAB&&id<=ItemId::CLOUDSTONE_STAIRS)||
        (id>=ItemId::MOONSTONE&&id<=ItemId::ROASTED_GLOWSHROOM)||
        id==ItemId::STARSTEP_SCEPTER||id==ItemId::SKYROOT_DOOR||id==ItemId::SKYROOT_BUTTON;
}
}
