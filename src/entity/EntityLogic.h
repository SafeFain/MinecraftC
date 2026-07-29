#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

enum class EntityType : uint8_t {
    Item, Cow, Pig, Sheep, Chicken, Zombie, Skeleton, Spider, Blastling,
    Arrow, PrimedTnt
};

enum class EntityPlayback { Idle, Walk, Hurt, Death, Attack };

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

inline bool deathPresentationVisible(float elapsed) {
    return elapsed >= 0.0f && elapsed < ENTITY_DEATH_PRESENTATION_SECONDS;
}

inline float advanceDeathPresentation(float elapsed, float dt) {
    return elapsed + std::max(0.0f, dt);
}

inline bool hostileSpawnLightValid(uint8_t blockLight) { return blockLight == 0; }
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
