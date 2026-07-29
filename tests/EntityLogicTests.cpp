#include "entity/EntityLogic.h"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
}

int main() {
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
