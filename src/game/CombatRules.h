#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

enum class AttackKind : uint8_t {
    Miss,
    Weak,
    Strong,
    Critical,
    Sweep
};

struct MeleeAttackRequest {
    float reach = 3.0f;
    float damage = 1.0f;
    bool critical = false;
    bool sweeping = false;
    bool sprintKnockback = false;
};

struct MeleeAttackResult {
    bool foundTarget = false;
    bool primaryDamaged = false;
    float primaryDamage = 0.0f;
    glm::dvec3 primaryPosition{0.0};
    std::vector<glm::dvec3> sweptPositions;
};

struct CombatFeedback {
    AttackKind kind = AttackKind::Miss;
    float damage = 0.0f;
    glm::dvec3 position{0.0};
    std::vector<glm::dvec3> sweptPositions;
};

enum class DamageCause : uint8_t {
    Generic,
    Melee,
    Projectile,
    Explosion,
    Fire,
    Fall,
    Drowning,
    Starvation,
    Void,
    Lightning
};

struct DamageSourceInfo {
    float amount = 0.0f;
    DamageCause cause = DamageCause::Generic;
    bool armorApplies = true;
    bool shieldBlockable = false;
    bool causesExhaustion = true;
    bool hasOrigin = false;
    glm::dvec3 origin{0.0};
    glm::vec3 impulse{0.0f};
};

struct DamageOutcome {
    float rawDamage = 0.0f;
    float appliedDamage = 0.0f;
    bool blocked = false;
    bool shieldBroken = false;
};

namespace CombatRules {

constexpr float DEFAULT_ATTACK_DAMAGE = 1.0f;
constexpr float DEFAULT_ATTACK_SPEED = 4.0f;
constexpr float STRONG_ATTACK_THRESHOLD = 0.9f;
constexpr float SWEEP_DAMAGE = 1.0f;

inline float attackCooldownTicks(float attackSpeed) {
    return 20.0f / std::max(attackSpeed, 0.001f);
}

inline float attackStrength(float ticksSinceAttack, float attackSpeed,
                            float partialTick = 0.5f) {
    const float cooldown = attackCooldownTicks(attackSpeed);
    return std::clamp((std::max(0.0f, ticksSinceAttack) + partialTick) /
                          cooldown,
                      0.0f, 1.0f);
}

inline float scaledAttackDamage(float baseDamage, float strength) {
    strength = std::clamp(strength, 0.0f, 1.0f);
    return std::max(0.0f, baseDamage) *
        (0.2f + strength * strength * 0.8f);
}

inline bool strongAttack(float strength) {
    return strength > STRONG_ATTACK_THRESHOLD;
}

inline bool criticalAttack(bool strong, bool falling, bool grounded,
                           bool inWater, bool sprinting) {
    return strong && falling && !grounded && !inWater && !sprinting;
}

inline bool sweepingAttack(bool sword, bool strong, bool grounded,
                           bool sprinting, bool critical) {
    return sword && strong && grounded && !sprinting && !critical;
}

inline float armorDamage(float damage, float armor, float toughness) {
    damage = std::max(0.0f, damage);
    armor = std::clamp(armor, 0.0f, 20.0f);
    toughness = std::max(0.0f, toughness);
    const float effective = std::min(
        20.0f, std::max(armor / 5.0f,
                        armor - damage / (2.0f + toughness / 4.0f)));
    return damage * (1.0f - effective / 25.0f);
}

inline uint16_t armorDurabilityDamage(float rawDamage) {
    return static_cast<uint16_t>(std::max(1.0f, std::floor(rawDamage / 4.0f)));
}

inline uint16_t shieldDurabilityDamage(float rawDamage) {
    if (rawDamage < 3.0f) return 0;
    return static_cast<uint16_t>(1 + std::floor(rawDamage));
}

inline bool sourceInFront(const glm::dvec3& playerPosition,
                          const glm::vec3& playerForward,
                          const glm::dvec3& sourcePosition) {
    glm::dvec2 toSource(sourcePosition.x - playerPosition.x,
                        sourcePosition.z - playerPosition.z);
    const double length = glm::length(toSource);
    if (length <= 0.000001) return false;
    toSource /= length;
    glm::vec2 forward(playerForward.x, playerForward.z);
    const float forwardLength = glm::length(forward);
    if (forwardLength <= 0.000001f) return false;
    forward /= forwardLength;
    return glm::dot(glm::dvec2(forward), toSource) > 0.0;
}

} // namespace CombatRules
