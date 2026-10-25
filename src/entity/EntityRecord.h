#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "entity/NetherMobs.h"
#include "game/Item.h"
#include "game/StatusEffects.h"
#include "game/VillagerTrade.h"

// Durable entity partition shared by chunk persistence, entity transfer
// journals and the simulation. Runtime-only data (navigation, AI state,
// statistics, render state and runtime IDs) is never part of this record.
// WorldMetadata::PersistedEntity is an alias of this type so the save format
// and every existing spelling stay unchanged.
struct EntityRecord {
    uint16_t type = 0;
    glm::dvec3 position{0.0};
    glm::vec3 velocity{0.0f};
    float health = 0.0f;
    float ageSeconds = 0.0f;
    ItemStack item;
    uint32_t behaviorSeed = 0;
    uint8_t flags = 0;
    float projectileDamage = 0.0f;
    VillagerData villager;
    std::string pluginState="{}";
    uint32_t pluginStateVersion=0;
    NetherMobState nether;
    std::vector<ActiveStatusEffect> effects;
};
