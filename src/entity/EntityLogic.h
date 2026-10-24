#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

#include "game/Item.h"
#include "plugins/ContentRegistry.h"
#include "game/GameRules.h"
#include "world/CaveGenerator.h"

enum class EntityType : uint16_t {
    Item, Cow, Pig, Sheep, Chicken, Zombie, Skeleton, Spider, Blastling,
    Arrow, PrimedTnt, Villager, ZombieVillager, IronGolem,
#define NETHER_ENTITY_ENUM(type,egg,key,label,h,x,y,z,s,d,monster,fire) type,
    MC_NETHER_MOBS(NETHER_ENTITY_ENUM)
#undef NETHER_ENTITY_ENUM
    GhastFireball, BlazeFireball, ThrownPotion, EnderPearl
};
static_assert(static_cast<uint16_t>(EntityType::IronGolem)==13 && static_cast<uint16_t>(EntityType::Ghast)==14 && static_cast<uint16_t>(EntityType::EnderPearl)==28,"Append-only entity identities");
inline bool nativeNetherMob(EntityType type) {return type>=EntityType::Ghast && type<=EntityType::Enderman;}
inline bool nativeProjectile(EntityType type) {return type>=EntityType::GhastFireball && type<=EntityType::EnderPearl;}
inline bool nativeFireImmune(EntityType type) {
    switch(type) {
#define NETHER_FIRE(type,egg,key,label,h,x,y,z,s,d,monster,fire) case EntityType::type:return fire;
        MC_NETHER_MOBS(NETHER_FIRE)
#undef NETHER_FIRE
        default:return false;
    }
}
inline bool undeadEntity(EntityType type) {
    return type==EntityType::Zombie || type==EntityType::ZombieVillager || type==EntityType::Skeleton ||
        type==EntityType::WitherSkeleton || type==EntityType::ZombifiedPiglin || type==EntityType::Zoglin;
}
inline float nativeMobHealth(EntityType type) {
    switch(type) {
#define NETHER_HEALTH(type,egg,key,label,h,x,y,z,s,d,monster,fire) case EntityType::type:return h;
        MC_NETHER_MOBS(NETHER_HEALTH)
#undef NETHER_HEALTH
        default:return 20;
    }
}
inline float nativeMobDamage(EntityType type) {
    switch(type) {
#define NETHER_DAMAGE(type,egg,key,label,h,x,y,z,s,d,monster,fire) case EntityType::type:return d;
        MC_NETHER_MOBS(NETHER_DAMAGE)
#undef NETHER_DAMAGE
        default:return 3;
    }
}
inline float nativeMobSpeed(EntityType type) {
    switch(type) {
#define NETHER_SPEED(type,egg,key,label,h,x,y,z,s,d,monster,fire) case EntityType::type:return s;
        MC_NETHER_MOBS(NETHER_SPEED)
#undef NETHER_SPEED
        default:return 2;
    }
}

inline EntityType entityTemplate(EntityType type) {
    if(const auto* definition=Plugins::entityContent(static_cast<uint16_t>(type)))return static_cast<EntityType>(definition->base);
    return type;
}
inline bool registeredMob(EntityType type) {
    return !nativeProjectile(type)&&type!=EntityType::Item&&type!=EntityType::Arrow&&type!=EntityType::PrimedTnt&&Plugins::validEntity(static_cast<uint16_t>(type));
}

enum class EntityPlayback { Idle, Walk, Hurt, Death, Attack };

inline EntityType entityTypeForSpawnEgg(SpawnEggMob mob) {
    switch (mob) {
#define NETHER_EGG_TYPE(type,egg,key,label,h,x,y,z,s,d,monster,fire) case SpawnEggMob::type:return EntityType::type;
        MC_NETHER_MOBS(NETHER_EGG_TYPE)
#undef NETHER_EGG_TYPE
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
// A full uint32 draw gives the same 1/power probability for fractional
// Blastling power as for integer TNT power. Do not truncate power to an int.
inline bool explosionDropSurvives(uint32_t random, float power, bool decay) {
    return !decay || power <= 1.0f || static_cast<double>(random) / 4294967296.0 < 1.0 / static_cast<double>(power);
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
