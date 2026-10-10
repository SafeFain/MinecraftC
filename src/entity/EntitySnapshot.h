#pragma once

#include "entity/EntityLogic.h"
#include "game/VillagerTrade.h"

// Presentation data only. Navigation, worker state and persistence are never reconstructed. Replicated
// IDs address visual instances only.
struct EntitySnapshot {
    uint64_t id = 0;
    EntityType type = EntityType::Item;
    glm::dvec3 position{0};
    glm::vec3 velocity{0}, locomotion{0}, facing{0, 0, -1};
    float health = 0, age = 0, hurt = 0, burning = 0;
    ItemStack item;
    uint32_t seed = 0;
    bool inGround = false, playerOwned = false, sleeping = false, attacking = false;
    VillagerData villager;
};
struct EntityDeathSnapshot {
    uint64_t id = 0;
    EntityType type = EntityType::Cow;
    glm::dvec3 position{0};
    glm::vec3 velocity{0}, facing{0, 0, -1};
    uint32_t seed = 0;
    float elapsed = 0;
};
