#pragma once

#include "world/Block.h"

inline bool isBiomePlant(BlockId block) {
    return (block >= BlockId::SKY_FERN && block <= BlockId::CLOUDBERRY_BUSH) ||
        (block >= BlockId::FERN && block <= BlockId::BEACH_GRASS) ||
        (block >= BlockId::CLOVER && block <= BlockId::JUNGLE_FERN) ||
        block == BlockId::CAVE_GLOWSHROOM;
}

inline bool isNaturalRock(BlockId block) {
    return block == BlockId::STONE || block == BlockId::DEEPSLATE ||
        block == BlockId::GRANITE || block == BlockId::LIMESTONE ||
        block == BlockId::BASALT || block == BlockId::TUFF ||
        block == BlockId::CALCITE || block == BlockId::DRIPSTONE_BLOCK ||
        block == BlockId::SHALE || block == BlockId::CORAL_ROCK ||
        (block >= BlockId::ANDESITE && block <= BlockId::MARBLE) ||
        (block >= BlockId::WET_LIMESTONE && block <= BlockId::SULFUR_ROCK);
}

inline bool isCrystalCluster(BlockId block) {
    return block == BlockId::AMETHYST_CLUSTER || block == BlockId::QUARTZ_CLUSTER;
}

inline bool isNaturalDecoration(BlockId block) {
    return isBiomePlant(block) || isCrystalCluster(block);
}

// Generation and manual placement share substrate rules. These plants occupy
// air, never water; riverbank proximity is a generation constraint only.
inline bool supportsBiomePlant(BlockId plant, BlockId soil) {
    if (!isBiomePlant(plant)) return false;
    const bool fertile = soil == BlockId::GRASS || soil == BlockId::DIRT ||
        soil == BlockId::PODZOL || soil == BlockId::MOSS ||
        soil == BlockId::COARSE_DIRT || soil == BlockId::ROOTED_DIRT ||
        soil == BlockId::LEAF_LITTER_SOIL || soil == BlockId::PEAT ||
        soil == BlockId::MUD || soil == BlockId::SILT ||
        soil == BlockId::LATERITE || soil == BlockId::RED_CLAY ||
        soil == BlockId::CAVE_MOSS;
    const bool sandy = soil == BlockId::SAND || soil == BlockId::RED_SAND ||
        soil == BlockId::BLACK_SAND;
    if (plant >= BlockId::SKY_FERN && plant <= BlockId::CLOUDBERRY_BUSH) {
        if (plant == BlockId::MOONFLOWER)
            return soil == BlockId::MOONSTONE || soil == BlockId::AETHER_MOSS ||
                soil == BlockId::AETHER_GRASS || soil == BlockId::AETHER_SOIL;
        return soil == BlockId::AETHER_GRASS || soil == BlockId::AETHER_SOIL ||
            soil == BlockId::AETHER_MOSS || soil == BlockId::GLIMMER_SILT;
    }
    if (plant == BlockId::CAVE_GLOWSHROOM)
        return soil == BlockId::CAVE_MOSS || soil == BlockId::MOSS ||
            soil == BlockId::MUD || soil == BlockId::CLAY;
    if (plant == BlockId::DESERT_FLOWER || plant == BlockId::SMALL_CACTUS)
        return sandy || soil == BlockId::LATERITE || soil == BlockId::COARSE_DIRT;
    if (plant == BlockId::TUNDRA_MOSS || plant == BlockId::HEATHER)
        return fertile || soil == BlockId::PERMAFROST || soil == BlockId::DRY_GRASS_BLOCK;
    if (plant == BlockId::REED_FLOWER || plant == BlockId::WILD_MINT)
        return fertile || soil == BlockId::CLAY;
    if (plant == BlockId::FALLEN_TWIGS)
        return fertile || soil == BlockId::DRY_GRASS_BLOCK;
    if (plant == BlockId::DEAD_BUSH)
        return sandy || soil == BlockId::COARSE_DIRT ||
            soil == BlockId::DRY_GRASS_BLOCK || soil == BlockId::TERRACOTTA ||
            soil == BlockId::OCHRE_TERRACOTTA || soil == BlockId::WHITE_TERRACOTTA;
    if (plant == BlockId::DRY_GRASS)
        return fertile || soil == BlockId::DRY_GRASS_BLOCK;
    if (plant == BlockId::BEACH_GRASS) return sandy || soil == BlockId::GRAVEL;
    if (plant == BlockId::CATTAIL)
        return fertile || soil == BlockId::CLAY || soil == BlockId::SAND;
    if (plant == BlockId::ALPINE_FLOWER)
        return fertile || soil == BlockId::PERMAFROST || soil == BlockId::GRANITE;
    return fertile;
}

inline bool supportsNaturalDecoration(BlockId block, BlockId soil) {
    return isCrystalCluster(block) ? isNaturalRock(soil) : supportsBiomePlant(block, soil);
}

inline bool isBiomeSoil(BlockId block) {
    return (block >= BlockId::ROOTED_DIRT && block <= BlockId::PERMAFROST) ||
        block == BlockId::VOLCANIC_ASH ||
        (block >= BlockId::LATERITE && block <= BlockId::SALT_CRUST) ||
        block == BlockId::CAVE_MOSS;
}

inline bool isBiomeRock(BlockId block) {
    return (block >= BlockId::SHALE && block <= BlockId::WHITE_TERRACOTTA) ||
        block == BlockId::CORAL_ROCK ||
        (block >= BlockId::ANDESITE && block <= BlockId::MARBLE) ||
        (block >= BlockId::WET_LIMESTONE && block <= BlockId::SULFUR_ROCK);
}
