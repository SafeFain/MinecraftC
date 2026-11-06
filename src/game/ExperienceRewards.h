#pragma once
#include "entity/EntityLogic.h"
#include "game/SurvivalRules.h"

inline uint32_t miningExperience(BlockId block, const ItemStack& tool, uint32_t random) {
    if (block==BlockId::BLAZE_SPAWNER || block==BlockId::MAGMA_SPAWNER) return 15+random%29;
    if (getBlockDrops(block,tool).empty()) return 0;
    switch(block) {
        case BlockId::COAL_ORE: return random%3;
        case BlockId::DIAMOND_ORE: return 3+random%5;
        case BlockId::NETHER_QUARTZ_ORE: return 2+random%4;
        case BlockId::NETHER_GOLD_ORE: return random%2;
        default: return 0;
    }
}

inline uint32_t mobExperience(EntityType type, uint8_t magmaSize, bool baby, uint32_t random) {
    switch(type) {
        case EntityType::Cow: case EntityType::Pig: case EntityType::Sheep:
        case EntityType::Chicken: case EntityType::Strider: return baby ? 0 : 1+random%3;
        case EntityType::Villager: case EntityType::IronGolem: return 0;
        case EntityType::Blaze: return 10;
        case EntityType::PiglinBrute: return 20;
        case EntityType::MagmaCube: return 1u<<magmaSize;
        case EntityType::Zombie: case EntityType::ZombieVillager:
        case EntityType::ZombifiedPiglin: return baby ? 12 : 5;
        case EntityType::Skeleton: case EntityType::WitherSkeleton:
        case EntityType::Spider: case EntityType::Blastling: case EntityType::Ghast:
        case EntityType::Enderman: case EntityType::Hoglin: case EntityType::Zoglin:
        case EntityType::Piglin: return 5;
        default: return 0;
    }
}
