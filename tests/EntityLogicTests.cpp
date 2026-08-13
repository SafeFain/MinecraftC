#include "entity/EntityLogic.h"
#include "entity/ProjectileLogic.h"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
}

int main() {
    require(entityTypeForSpawnEgg(SpawnEggMob::Cow) == EntityType::Cow &&
            entityTypeForSpawnEgg(SpawnEggMob::Chicken) == EntityType::Chicken &&
            entityTypeForSpawnEgg(SpawnEggMob::Zombie) == EntityType::Zombie &&
            entityTypeForSpawnEgg(SpawnEggMob::Blastling) == EntityType::Blastling,
            "spawn egg mob mapping diverged from shared entity types");
    require(selectEntityPlayback(0.0f, false, false) == EntityPlayback::Idle,
            "idle playback selection failed");
    require(selectEntityPlayback(0.2f, false, false) == EntityPlayback::Walk,
            "walk playback selection failed");
    require(selectEntityPlayback(0.2f, true, false) == EntityPlayback::Hurt,
            "hurt playback did not override locomotion");
    require(selectEntityPlayback(0.2f, true, true) == EntityPlayback::Death,
            "death playback did not have highest priority");
    require(walkPlaybackRate(0.0f) == 0.5f &&
            walkPlaybackRate(100.0f) == 2.0f,
            "walk playback rate was not bounded");
    const glm::vec3 locomotion = autonomousHorizontalVelocity(
        {10.0, 4.0, -2.0}, {10.5, 7.0, -2.25}, 0.5f);
    require(std::abs(locomotion.x - 1.0f) < 0.0001f && locomotion.y == 0.0f &&
            std::abs(locomotion.z + 0.5f) < 0.0001f &&
            autonomousHorizontalVelocity({0,0,0},{1,0,1},0.0f) == glm::vec3(0),
            "autonomous locomotion did not use actual horizontal displacement");
    require(attackImpactValid(1.49f, 1.5f, true) &&
            !attackImpactValid(1.5f, 1.5f, true) &&
            !attackImpactValid(1.0f, 1.5f, false),
            "attack impact range or sight revalidation failed");
    require(explosionImpact(2.0f, 5.0f, true) == 0.6f &&
            explosionImpact(2.0f, 5.0f, false) == 0.0f &&
            explosionImpact(5.0f, 5.0f, true) == 0.0f,
            "blocked or out-of-range explosions retained damage");
    require(deathPresentationVisible(0.0f) &&
            deathPresentationVisible(0.999f) &&
            !deathPresentationVisible(ENTITY_DEATH_PRESENTATION_SECONDS),
            "death presentation did not use the exact one-second interval");
    require(advanceDeathPresentation(0.75f, 0.25f) ==
                ENTITY_DEATH_PRESENTATION_SECONDS &&
            advanceDeathPresentation(0.75f, -1.0f) == 0.75f,
            "death presentation timer did not advance monotonically");
    require(hostileSpawnLightValid(0), "darkness permits hostile spawning");
    require(!hostileSpawnLightValid(1) && !hostileSpawnLightValid(14),
            "any block light prevents hostile spawning");
    require(shouldHostileDespawn(129.0f, 0.0f, 1),
            "hostiles beyond the hard radius despawn immediately");
    require(!shouldHostileDespawn(31.0f, 100.0f, 0),
            "nearby hostiles never use random despawn");
    require(!shouldHostileDespawn(40.0f, 29.0f, 0),
            "new hostiles receive the grace period");
    require(shouldHostileDespawn(40.0f, 31.0f, 600),
            "eligible distant hostiles honor deterministic roll");
    require(sweptCollisionSteps(0.0) == 1 && sweptCollisionSteps(0.31) == 3,
            "projectile sweep bounds each collision step");
    require(bowChargeStrength(0.0f) == 0.0f &&
            bowChargeStrength(0.5f) > 0.0f &&
            bowChargeStrength(0.5f) < 1.0f &&
            bowChargeStrength(1.0f) == 1.0f &&
            bowLaunchSpeed(0.0f) == BOW_MIN_SPEED &&
            bowLaunchSpeed(1.0f) == BOW_MAX_SPEED,
            "bow charge did not map monotonically onto launch speed");
    const glm::vec3 inheritedShot = projectileLaunchVelocity(
        {0.0f, 0.0f, 1.0f}, 20.0f, {2.0f, 3.0f, -1.0f});
    require(inheritedShot == glm::vec3(2.0f, 3.0f, 19.0f),
            "projectile did not inherit shooter velocity");
    const glm::dvec3 oneSecond = projectilePosition(
        {1.0, 2.0, 3.0}, {4.0f, 5.0f, 6.0f}, 1.0);
    require(glm::length(oneSecond - glm::dvec3(5.0, 2.1, 9.0)) < 0.00001,
            "projectile position did not apply velocity and gravity analytically");
    const glm::dvec3 splitPosition = projectilePosition(
        projectilePosition({1.0, 2.0, 3.0}, {4.0f, 5.0f, 6.0f}, 0.4),
        projectileVelocityAfter({4.0f, 5.0f, 6.0f}, 0.4f), 0.6);
    require(glm::length(splitPosition - oneSecond) < 0.00001,
            "projectile integration changed with frame subdivision");
    const auto ballistic = lowArcBallisticVelocity(
        {0.0, 1.0, 0.0}, {12.0, 2.0, 0.0}, 20.0f);
    require(ballistic.has_value(), "reachable low ballistic arc had no solution");
    if (ballistic) {
        const double flightSeconds = 12.0 / ballistic->x;
        require(glm::length(projectilePosition(
                    {0.0, 1.0, 0.0}, *ballistic, flightSeconds) -
                glm::dvec3(12.0, 2.0, 0.0)) < 0.001,
                "low ballistic arc did not pass through its target");
    }
    const auto inheritedBallistic = lowArcBallisticVelocity(
        {0.0, 1.0, 0.0}, {12.0, 2.0, 0.0}, 20.0f,
        {2.0f, 0.0f, 0.0f});
    require(inheritedBallistic.has_value(),
            "moving shooter had no reachable ballistic solution");
    if (inheritedBallistic) {
        const double flightSeconds = 12.0 / inheritedBallistic->x;
        require(glm::length(projectilePosition(
                    {0.0, 1.0, 0.0}, *inheritedBallistic, flightSeconds) -
                glm::dvec3(12.0, 2.0, 0.0)) < 0.001,
                "ballistic solution did not include inherited shooter velocity");
    }
    require(!spiderTargetsPlayer(true, false, 4.0f),
            "an unprovoked daytime spider targeted the player");
    require(spiderTargetsPlayer(true, true, 17.9f),
            "a provoked daytime spider did not retaliate");
    require(!spiderTargetsPlayer(true, true, 18.0f),
            "a daytime spider retained anger outside perception range");
    require(spiderTargetsPlayer(false, false, 4.0f),
            "a nighttime spider did not target the player");
    require(mobTargetsPlayer(true, true),
            "a hostile behavior did not target a vulnerable player");
    require(!mobTargetsPlayer(false, true),
            "a hostile behavior targeted an invulnerable game-mode player");
    require(updateBurning(0.0f, true, false, 0.1f) == 5.0f,
            "sunlight did not refresh burning duration");
    require(updateBurning(5.0f, false, false, 1.0f) == 4.0f,
            "shade did not count down residual burning");
    require(updateBurning(5.0f, true, true, 0.1f) == 0.0f,
            "water did not extinguish burning immediately");
    float burnAccumulator = 0.0f;
    int burnTicks = 0;
    for (int i = 0; i < 50; ++i)
        burnTicks += accumulateBurnDamage(burnAccumulator, 0.1f);
    require(burnTicks == 5 && std::abs(burnAccumulator) < 0.0001f,
            "five burning seconds did not produce five damage ticks");
    std::cout << "Entity logic tests passed\n";
}
