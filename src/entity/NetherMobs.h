#pragma once

#include <cstdint>

// Append-only entity identities. Health, collision size, speed, melee damage,
// monster category and fire immunity are native gameplay data, not plugin data.
#define MC_NETHER_MOBS(X) \
 X(Ghast,GHAST_SPAWN_EGG,"ghast","Ghast",10.f,4.f,4.f,4.f,3.f,6.f,true,true) \
 X(Blaze,BLAZE_SPAWN_EGG,"blaze","Blaze",20.f,.6f,1.8f,.6f,2.5f,6.f,true,true) \
 X(WitherSkeleton,WITHER_SKELETON_SPAWN_EGG,"wither_skeleton","Wither Skeleton",20.f,.7f,2.4f,.7f,2.6f,4.f,true,true) \
 X(MagmaCube,MAGMA_CUBE_SPAWN_EGG,"magma_cube","Magma Cube",4.f,.51f,.51f,.51f,2.f,3.f,true,true) \
 X(Piglin,PIGLIN_SPAWN_EGG,"piglin","Piglin",16.f,.6f,1.95f,.6f,2.6f,5.f,true,false) \
 X(PiglinBrute,PIGLIN_BRUTE_SPAWN_EGG,"piglin_brute","Piglin Brute",50.f,.6f,1.95f,.6f,2.8f,7.f,true,false) \
 X(ZombifiedPiglin,ZOMBIFIED_PIGLIN_SPAWN_EGG,"zombified_piglin","Zombified Piglin",20.f,.6f,1.95f,.6f,2.7f,5.f,true,true) \
 X(Hoglin,HOGLIN_SPAWN_EGG,"hoglin","Hoglin",40.f,1.4f,1.4f,1.4f,2.5f,6.f,true,false) \
 X(Zoglin,ZOGLIN_SPAWN_EGG,"zoglin","Zoglin",40.f,1.4f,1.4f,1.4f,2.5f,6.f,true,true) \
 X(Strider,STRIDER_SPAWN_EGG,"strider","Strider",20.f,.9f,1.7f,.9f,1.8f,0.f,false,true) \
 X(Enderman,ENDERMAN_SPAWN_EGG,"enderman","Enderman",40.f,.6f,2.9f,.6f,3.f,7.f,true,false)

#define MC_NETHER_STATE_FIELDS(X) \
 X(uint8_t,magmaSize,2) \
 X(uint8_t,saddled,0) \
 X(uint32_t,angerTicks,0) \
 X(uint16_t,conversionTicks,0) \
 X(uint16_t,barterTicks,0) \
 X(uint32_t,barterSequence,0) \
 X(uint16_t,attackTicks,0) \
 X(uint8_t,burstRemaining,0) \
 X(uint16_t,boostTicks,0) \
 X(uint16_t,cureTicks,0)
struct NetherMobState {
#define NETHER_STATE_FIELD(type,name,value) type name=value;
    MC_NETHER_STATE_FIELDS(NETHER_STATE_FIELD)
#undef NETHER_STATE_FIELD
};
inline bool validNetherMobState(const NetherMobState& s) {
    return s.magmaSize<=2 && s.saddled<=1 && s.angerTicks<=2400 &&
        s.conversionTicks<=300 && s.barterTicks<=120 && s.attackTicks<=400 &&
        s.burstRemaining<=3 && s.boostTicks<=140 && s.cureTicks<=6000;
}
