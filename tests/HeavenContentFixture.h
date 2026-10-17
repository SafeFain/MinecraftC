#pragma once
#include "plugins/OfficialHeavenContent.h"
#include "plugins/OfficialHeavenActions.h"
// These standalone tests exercise official built-in definitions without a loader.
// Loader/session integration tests initialize their own real package registries.
inline void installHeavenFixture(){
    Plugins::publishHeavenDimension();
    Plugins::heavenSemantics([](const MC_BlockBehaviorV2& d){
        Plugins::BlockBehaviorContent b;b.fields=d.fields;b.light=d.light_dampening;b.tags=d.tags;b.substrate=d.substrate_tags;
        b.replaceable=d.base.replaceable!=0;b.encouragement=d.base.fire_encouragement;b.flammability=d.base.fire_flammability;b.action=d.base.action?d.base.action:"";
        for(uint32_t i=0;i<d.drop_count;++i){const auto& r=d.drops[i];b.drops.push_back({r.item,r.minimum,r.maximum,r.numerator,r.denominator});}
        Plugins::content().extensions.blockBehaviors.emplace(static_cast<uint16_t>(Plugins::resolveBlock(d.base.block)),b);
    },[](const MC_ItemBehaviorV2& d){Plugins::ItemBehaviorContent b;b.fields=d.fields;b.fuel=d.fuel_ticks;b.tags=d.tags;b.action=d.base.action?d.base.action:"";b.dimension=d.base.dimension?d.base.dimension:"";Plugins::content().extensions.itemBehaviors.emplace(static_cast<uint16_t>(Plugins::resolveItem(d.base.item)),b);});
    Plugins::ActionContent sky;sky.key="official_heaven:skyway";sky.range=256;sky.targetV2=[](const auto& v,auto& t){return Plugins::skywayTarget(&v,&t,nullptr)==0;};
    Plugins::content().extensions.actions.emplace(sky.key,sky);
    sky.key="official_heaven:starstep";sky.range=96;sky.cooldown=3;sky.durability=1;sky.rangeFromEye=sky.toolPose=sky.invalidMarker=true;sky.useCooldown=.15f;sky.targetV2=[](const auto& v,auto& t){return Plugins::starstepTarget(&v,&t,nullptr)==0;};
    Plugins::content().extensions.actions.emplace(sky.key,sky);
    ++Plugins::content().revision;
}
