#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

#include "game/Item.h"
#include "game/GameRules.h"
#include "world/CaveGenerator.h"

enum class EntityType : uint8_t {
    Item, Cow, Pig, Sheep, Chicken, Zombie, Skeleton, Spider, Blastling,
    Arrow, PrimedTnt, Villager, ZombieVillager
};

enum class EntityPlayback { Idle, Walk, Hurt, Death, Attack };

inline EntityType entityTypeForSpawnEgg(SpawnEggMob mob) {
    switch (mob) {
        case SpawnEggMob::Cow: return EntityType::Cow;
        case SpawnEggMob::Pig: return EntityType::Pig;
        case SpawnEggMob::Sheep: return EntityType::Sheep;
        case SpawnEggMob::Chicken: return EntityType::Chicken;
        case SpawnEggMob::Zombie: return EntityType::Zombie;
        case SpawnEggMob::Skeleton: return EntityType::Skeleton;
        case SpawnEggMob::Spider: return EntityType::Spider;
        case SpawnEggMob::Blastling: return EntityType::Blastling;
        case SpawnEggMob::Villager: return EntityType::Villager;
        case SpawnEggMob::ZombieVillager: return EntityType::ZombieVillager;
    }
    return EntityType::Cow;
}

inline bool naturalZombieBecomesVillager(uint32_t deterministicRoll) {
    return deterministicRoll % 20u == 0u;
}

inline bool villagerInfectionConverts(
    Difficulty difficulty, uint32_t deterministicRoll) {
    if (difficulty == Difficulty::Hard) return true;
    if (difficulty == Difficulty::Normal)
        return (deterministicRoll & 1u) == 0u;
    return false;
}

constexpr float ENTITY_WALK_SPEED_THRESHOLD = 0.05f;
constexpr float ENTITY_DEATH_PRESENTATION_SECONDS = 1.0f;

inline EntityPlayback selectEntityPlayback(float horizontalSpeed, bool hurt,
                                           bool dead) {
    if (dead) return EntityPlayback::Death;
    if (hurt) return EntityPlayback::Hurt;
    return horizontalSpeed > ENTITY_WALK_SPEED_THRESHOLD
        ? EntityPlayback::Walk : EntityPlayback::Idle;
}

inline float walkPlaybackRate(float horizontalSpeed) {
    return std::clamp(horizontalSpeed, 0.5f, 2.0f);
}
inline glm::vec3 autonomousHorizontalVelocity(const glm::dvec3& before,
                                               const glm::dvec3& after,
                                               float dt) {
    if (dt <= 0.0f) return glm::vec3(0.0f);
    return {static_cast<float>((after.x - before.x) / dt), 0.0f,
            static_cast<float>((after.z - before.z) / dt)};
}
inline bool attackImpactValid(float distance, float reach, bool clearSight) {
    return distance >= 0.0f && distance < reach && clearSight;
}
inline float explosionImpact(float distance, float radius, bool clearSight) {
    if (!clearSight || distance < 0.0f || radius <= 0.0f || distance >= radius)
        return 0.0f;
    return 1.0f - distance / radius;
}

inline bool deathPresentationVisible(float elapsed) {
    return elapsed >= 0.0f && elapsed < ENTITY_DEATH_PRESENTATION_SECONDS;
}

inline float advanceDeathPresentation(float elapsed, float dt) {
    return elapsed + std::max(0.0f, dt);
}

inline bool hostileSpawnLightValid(uint8_t blockLight) { return blockLight == 0; }
inline bool shouldAttemptHostileSpawn(bool underground, bool isDay,
                                      bool thunderstorm, bool peaceful) {
    return !peaceful && (underground || !isDay || thunderstorm);
}
inline EntityType caveHostileFor(CaveBiome biome, uint32_t roll) {
    const uint32_t value = roll % 100u;
    switch (biome) {
        case CaveBiome::VerdantGrotto:
            return value < 45 ? EntityType::Spider : value < 70 ? EntityType::Zombie
                 : value < 90 ? EntityType::Skeleton : EntityType::Blastling;
        case CaveBiome::DripstoneKarst:
            return value < 40 ? EntityType::Skeleton : value < 65 ? EntityType::Spider
                 : value < 90 ? EntityType::Zombie : EntityType::Blastling;
        case CaveBiome::CrystalHollow:
            return value < 35 ? EntityType::Zombie : value < 65 ? EntityType::Skeleton
                 : value < 85 ? EntityType::Spider : EntityType::Blastling;
        case CaveBiome::VolcanicDepths:
            return value < 45 ? EntityType::Blastling : value < 70 ? EntityType::Skeleton
                 : value < 90 ? EntityType::Zombie : EntityType::Spider;
        case CaveBiome::Neutral:
            return value < 25 ? EntityType::Zombie : value < 50 ? EntityType::Skeleton
                 : value < 75 ? EntityType::Spider : EntityType::Blastling;
    }
    return EntityType::Zombie;
}
inline bool shouldHostileDespawn(float distance, float simulatedAge, uint32_t roll) {
    return distance > 128.0f ||
        (distance > 32.0f && simulatedAge > 30.0f && roll % 600 == 0);
}
inline int sweptCollisionSteps(double distance, double maximumStep = 0.15) {
    return std::max(1, static_cast<int>(std::ceil(distance / maximumStep)));
}
inline bool spiderTargetsPlayer(bool isDay, bool provoked, float distance) {
    return distance < 18.0f && (!isDay || provoked);
}
inline bool mobTargetsPlayer(bool playerTargetable, bool behaviorTargetsPlayer) {
    return playerTargetable && behaviorTargetsPlayer;
}
inline float updateBurning(float remaining, bool sunlit, bool inWater, float dt) {
    if (inWater) return 0.0f;
    if (sunlit) return 5.0f;
    return std::max(0.0f, remaining - dt);
}
inline int accumulateBurnDamage(float& accumulator, float burningSeconds) {
    accumulator += std::max(0.0f, burningSeconds);
    const int ticks = static_cast<int>(std::floor(accumulator + 0.000001f));
    accumulator -= static_cast<float>(ticks);
    return ticks;
}
