#pragma once
#include "world/Block.h"
namespace FrozenHeavenReference {
inline bool isFrozenNaturalDecoration(BlockId block) {
    return (block>=BlockId::FERN&&block<=BlockId::BEACH_GRASS)||
        (block>=BlockId::CLOVER&&block<=BlockId::JUNGLE_FERN)||
        (block>=BlockId::SKY_FERN&&block<=BlockId::CLOUDBERRY_BUSH)||
        block==BlockId::CAVE_GLOWSHROOM||block==BlockId::AMETHYST_CLUSTER||block==BlockId::QUARTZ_CLUSTER;
}
// 988fbd4 OfficialHeavenContent substrate tags, resolved independently of
// the migrated registry and its semanticPlantSupport callback.
inline bool supportsHeavenPlant(BlockId plant,BlockId soil) {
    if(plant<BlockId::SKY_FERN||plant>BlockId::CLOUDBERRY_BUSH)return false;
    const bool common=soil==BlockId::AETHER_GRASS||soil==BlockId::AETHER_SOIL||soil==BlockId::AETHER_MOSS;
    return common||(plant==BlockId::MOONFLOWER?soil==BlockId::MOONSTONE:soil==BlockId::GLIMMER_SILT);
}
}
