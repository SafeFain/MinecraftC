#include "entity/EntityLogic.h"
#include "entity/ProjectileLogic.h"
#include "entity/ProjectileCollision.h"

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
            entityTypeForSpawnEgg(SpawnEggMob::Blastling) == EntityType::Blastling &&
            entityTypeForSpawnEgg(SpawnEggMob::Villager) == EntityType::Villager &&
            entityTypeForSpawnEgg(SpawnEggMob::ZombieVillager) ==
                EntityType::ZombieVillager &&
            static_cast<uint8_t>(EntityType::Villager) == 11 &&
            static_cast<uint8_t>(EntityType::ZombieVillager) == 12,
            "spawn egg mob mapping diverged from shared entity types");
    size_t zombieVillagerRolls = 0;
    for (uint32_t roll = 0; roll < 100; ++roll)
        if (naturalZombieBecomesVillager(roll)) ++zombieVillagerRolls;
    require(zombieVillagerRolls == 5 &&
            !villagerInfectionConverts(Difficulty::Easy, 0) &&
            villagerInfectionConverts(Difficulty::Normal, 0) &&
            !villagerInfectionConverts(Difficulty::Normal, 1) &&
            villagerInfectionConverts(Difficulty::Hard, 1),
            "zombie-villager natural or difficulty infection rates changed");
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
    require(shouldAttemptHostileSpawn(true, true, false, false) &&
                !shouldAttemptHostileSpawn(false, true, false, false) &&
                shouldAttemptHostileSpawn(false, false, false, false) &&
                shouldAttemptHostileSpawn(false, true, true, false) &&
                !shouldAttemptHostileSpawn(true, false, true, true),
            "underground, daylight, thunder, or peaceful spawn gates failed");
    require(caveHostileFor(CaveBiome::VerdantGrotto, 0) == EntityType::Spider &&
                caveHostileFor(CaveBiome::DripstoneKarst, 0) ==
                    EntityType::Skeleton &&
                caveHostileFor(CaveBiome::CrystalHollow, 0) ==
                    EntityType::Zombie &&
                caveHostileFor(CaveBiome::VolcanicDepths, 0) ==
                    EntityType::Blastling &&
                caveHostileFor(CaveBiome::VerdantGrotto, 99) ==
                    EntityType::Blastling &&
                caveHostileFor(CaveBiome::VolcanicDepths, 99) ==
                    EntityType::Spider,
            "cave-biome hostile weighting changed unexpectedly");
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
    const auto thinHit = projectileAabbHit({0, 2, 0}, {1000, 0, 0},
        {5.001, 1.9, -.01}, {5.002, 2.1, .01}, .01);
    require(thinHit && std::abs(*thinHit - .005001) < 1e-10,
            "high-speed arrow skipped a thin target");
    const glm::vec3 risingVelocity(4, 9.8f, 0);
    const glm::dvec3 apexPoint = projectilePosition({0, 2, 0}, risingVelocity, 1.0);
    const auto curvedHit = projectileAabbHit({0, 2, 0}, risingVelocity,
        apexPoint - glm::dvec3(.01), apexPoint + glm::dvec3(.01), 2.0);
    require(curvedHit && *curvedHit > .99 && *curvedHit < 1.01,
            "parabolic sweep missed a target above the frame chord");
    for (int i = 1; i <= 1000; ++i) {
        const glm::vec3 velocity(0, i * .01f, 0);
        const double apexTime = velocity.y / static_cast<double>(PROJECTILE_GRAVITY);
        const double height = projectilePosition({0, 2, 0}, velocity, apexTime).y;
        const auto tangent = projectileAabbHit({0, 2, 0}, velocity,
            {-.1, height, -.1}, {.1, height + .1, .1}, 2.0);
        require(tangent && std::abs(*tangent - apexTime) < 1e-6 &&
                !projectileAabbHit({0, 2, 0}, velocity,
                    {-.1, height + .00001, -.1}, {.1, height + .1, .1}, 2.0),
                "apex tangency roundoff lost contact or accepted a true near miss");
    }
    const auto fallingHit = projectileAabbHit({0, 2, 0}, risingVelocity,
        {5.99, 5.5, -.1}, {6.01, 6.0, .1}, 2.0);
    require(fallingHit && *fallingHit > 1.49,
            "descending branch lost a vertical boundary contact");
    require(!projectileAabbHit({0, 2, 0}, {0, 0, 0},
                {1, 0, 0}, {2, 4, 1}, 1.0) &&
            !projectileAabbHit({0, 2, 0}, {4, 0, 0},
                {1, 2.1, -.1}, {2, 3, .1}, 1.0) &&
            projectileAabbHit({0, 2, 0}, {0, 0, 0},
                {-1, 1, -1}, {1, 3, 1}, 0.0) == 0.0,
            "parallel, unreachable or initial-overlap AABB contacts failed");
    const glm::dvec3 distantOrigin(-1000000.5, 10, 1000000.5);
    const auto distantHit = projectileAabbHit(distantOrigin, {100, 0, 0},
        distantOrigin + glm::dvec3(1, -.1, -.1),
        distantOrigin + glm::dvec3(1.01, .1, .1), .1);
    require(distantHit && std::abs(*distantHit - .01) < 1e-10,
            "large negative world coordinates lost contact precision");
    const auto wall = [](int x, int y, int z) {
        return x == 5 && y == 2 && z == 0 ? BlockId::STONE : BlockId::AIR;
    };
    const glm::dvec3 shotOrigin(.5, 2.5, .5);
    const glm::vec3 shotVelocity(100, 0, 0);
    const auto wallHit = projectileBlockHit(shotOrigin, shotVelocity, .2, wall);
    require(wallHit && std::abs(*wallHit - .045) < 1e-10,
            "voxel collision did not find exact entry surface");
    for (int fps : {10, 30, 60, 144}) {
        glm::dvec3 position = shotOrigin;
        glm::vec3 velocity = shotVelocity;
        double elapsed = 0.0;
        std::optional<double> contact;
        for (int frame = 0; frame < fps && !contact; ++frame) {
            const double dt = 1.0 / fps;
            const auto hit = projectileBlockHit(position, velocity, dt, wall);
            if (hit) contact = elapsed + *hit;
            position = projectilePosition(position, velocity, dt);
            velocity = projectileVelocityAfter(velocity, static_cast<float>(dt));
            elapsed += dt;
        }
        require(contact && std::abs(*contact - *wallHit) < 1e-8,
                "block contact time changed with frame subdivision");
    }
    const auto dripstone = [](int x, int y, int z) {
        return x == -2 && y == 2 && z == -1
            ? BlockId::POINTED_DRIPSTONE_UP : BlockId::AIR;
    };
    const auto dripHit = projectileBlockHit({-3, 2.8, -.5}, {100, 0, 0}, .03, dripstone);
    require(dripHit && std::abs(*dripHit - .0125) < 1e-10 &&
            !projectileBlockHit({-3, 2.8, -.1}, {100, 0, 0}, .03, dripstone),
            "thin partial blocks or negative voxel coordinates collided incorrectly");
    const auto slab = [](int x, int y, int z) {
        return x == 1 && y == 2 && z == 0
            ? slabBlock(ArchitecturalMaterial::Planks, BlockHalf::Bottom) : BlockId::AIR;
    };
    require(!projectileBlockHit({.5, 2.8, .5}, {100, 0, 0}, .02, slab) &&
            projectileBlockHit({.5, 2.3, .5}, {100, 0, 0}, .02, slab),
            "arrows collided with the empty half of a slab");
    const auto apexBlock = [](int x, int y, int z) {
        return x == 4 && y == 6 && z == 0 ? BlockId::STONE : BlockId::AIR;
    };
    require(projectileBlockHit({.5, 2, .5}, risingVelocity, 2.0, apexBlock).has_value(),
            "voxel candidate traversal missed the arc apex");
    int queries = 0;
    projectileBlockHit({.5, 10.5, .5}, {1000, 0, 0}, .1,
        [&queries](int, int, int) { ++queries; return BlockId::AIR; });
    require(queries < 350, "high-speed candidate traversal sampled excessively");
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
