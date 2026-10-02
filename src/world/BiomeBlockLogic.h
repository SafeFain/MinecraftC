#pragma once

#include "world/Block.h"

inline bool isBiomePlant(BlockId block) {
    return block >= BlockId::FERN && block <= BlockId::BEACH_GRASS;
}

// Generation and manual placement share substrate rules. These plants occupy
// air, never water; riverbank proximity is a generation constraint only.
inline bool supportsBiomePlant(BlockId plant, BlockId soil) {
    if (!isBiomePlant(plant)) return false;
    const bool fertile = soil == BlockId::GRASS || soil == BlockId::DIRT ||
        soil == BlockId::PODZOL || soil == BlockId::MOSS ||
        soil == BlockId::COARSE_DIRT || soil == BlockId::ROOTED_DIRT ||
        soil == BlockId::LEAF_LITTER_SOIL || soil == BlockId::PEAT ||
        soil == BlockId::MUD || soil == BlockId::SILT;
    const bool sandy = soil == BlockId::SAND || soil == BlockId::RED_SAND ||
        soil == BlockId::BLACK_SAND;
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

inline bool isBiomeSoil(BlockId block) {
    return (block >= BlockId::ROOTED_DIRT && block <= BlockId::PERMAFROST) ||
        block == BlockId::VOLCANIC_ASH;
}

inline bool isBiomeRock(BlockId block) {
    return (block >= BlockId::SHALE && block <= BlockId::WHITE_TERRACOTTA) ||
        block == BlockId::CORAL_ROCK;
}
