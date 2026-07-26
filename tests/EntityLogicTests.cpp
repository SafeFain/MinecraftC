#include "entity/EntityLogic.h"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
}

int main() {
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
    std::cout << "Entity logic tests passed\n";
}
