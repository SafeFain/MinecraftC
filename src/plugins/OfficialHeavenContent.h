#pragma once
#include "plugins/ContentMutation.h"
#include "plugins/ContentQueries.h"
#include "world/BiomeBlockLogic.h"

namespace Plugins {
inline void publishHeavenDimension(){
    DimensionContent d;d.key="minecraftc:heaven";d.name="Heaven";d.weather=MC_WEATHER_CLEAR;
    d.natural=false;d.deathTarget=d.voidTarget="minecraftc:overworld";
    if(!mutableContentState().extensions.dimensions.emplace(1,std::move(d)).second)throw std::runtime_error("Heaven slot already owned");
}
// Production registration and standalone tests share provider-owned policies.
template<class Block,class Item> void heavenSemantics(Block block,Item item){
    for(uint16_t raw=0;raw<static_cast<uint16_t>(BlockId::COUNT);++raw){
        const auto id=static_cast<BlockId>(raw);if(!heavenBlockSlot(id))continue;
        const auto key=blockKey(id);MC_BlockBehaviorV2 d{};d.size=sizeof(d);d.base.size=sizeof(d.base);d.base.block=key.c_str();
        const auto& properties=getBlockProps(id);
        d.fields=MC_BLOCK_TAGS|MC_BLOCK_LIGHT;
        d.light_dampening=properties.transparent||properties.shape!=RenderShape::Cube?0:15;
        if(id==BlockId::SKYROOT_LEAVES){d.light_dampening=1;d.base.fire_encouragement=30;d.base.fire_flammability=60;}
        if(id==BlockId::SKYROOT_WOOD||id==BlockId::SKYROOT_PLANKS){d.base.fire_encouragement=5;d.base.fire_flammability=5;}
        DoorState door;ButtonState button;
        if(decodeDoor(id,door)||decodeButton(id,button)){d.base.fire_encouragement=5;d.base.fire_flammability=20;}
        if(id==BlockId::STARFLOWER||id==BlockId::CLOUD_BLOOM||id==BlockId::GLOWSHROOM){d.base.replaceable=1;d.base.fire_encouragement=60;d.base.fire_flammability=100;}
        if(id==BlockId::STARFLOWER||id==BlockId::CLOUD_BLOOM||id==BlockId::GLOWSHROOM||
           (id>=BlockId::SKY_FERN&&id<=BlockId::HANGING_CLOUD_VINE))d.tags|=MC_TAG_FLOWER;
        if(id==BlockId::SKYROOT_LEAVES)d.tags|=MC_TAG_LEAVES;
        if(id==BlockId::SKYROOT_PLANKS)d.tags|=MC_TAG_PLANKS;
        if(id==BlockId::AETHER_GRASS||id==BlockId::AETHER_SOIL||id==BlockId::AETHER_MOSS)d.tags|=MC_TAG_HEAVEN_SOIL|MC_TAG_MOON_SOIL;
        if(id==BlockId::GLIMMER_SILT)d.tags|=MC_TAG_HEAVEN_SOIL;
        if(id==BlockId::MOONSTONE)d.tags|=MC_TAG_MOON_SOIL;
        if(id>=BlockId::SKY_FERN&&id<=BlockId::CLOUDBERRY_BUSH){d.base.fire_encouragement=60;d.base.fire_flammability=100;d.base.replaceable=1;d.tags|=MC_TAG_PLANT;d.fields|=MC_BLOCK_SUBSTRATE;d.substrate_tags=id==BlockId::MOONFLOWER?MC_TAG_MOON_SOIL:MC_TAG_HEAVEN_SOIL;}
        MC_BreakDrop drop{};std::string dropKey;
        if(id==BlockId::STAR_CRYSTAL_ORE||id==BlockId::CLOUDBERRY_BUSH){
            d.fields|=MC_BLOCK_DROPS;dropKey=itemKey(id==BlockId::STAR_CRYSTAL_ORE?ItemId::STAR_CRYSTAL_SHARD:ItemId::CLOUDBERRY);
            drop={dropKey.c_str(),2,2,1,1};d.drops=&drop;d.drop_count=1;
        }
        if(id==BlockId::STAR_CRYSTAL)d.base.action="official_heaven:skyway";
        if(id==BlockId::HANGING_CLOUD_VINE)d.base.support=2;
        block(d);
    }
    for(uint16_t raw=1;raw<static_cast<uint16_t>(ItemId::COUNT);++raw){
        const auto id=static_cast<ItemId>(raw);if(!heavenItemSlot(id))continue;
        const auto key=itemKey(id);MC_ItemBehaviorV2 d{};d.size=sizeof(d);d.base.size=sizeof(d.base);d.base.item=key.c_str();
        d.fields=MC_ITEM_FUEL|MC_ITEM_TAGS;
        if(id==ItemId::SKYROOT_PLANKS){d.tags=MC_TAG_PLANKS;d.fuel_ticks=300;}
        if(id==ItemId::SKYROOT_DOOR)d.fuel_ticks=200;
        if(id==ItemId::SKYROOT_BUTTON)d.fuel_ticks=100;
        if(id==ItemId::STARSTEP_SCEPTER){d.base.action="official_heaven:starstep";d.base.dimension="minecraftc:heaven";}
        item(d);
    }
}
}
