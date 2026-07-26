#include "player/PlayerPhysics.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <tuple>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    using Key = std::tuple<int, int, int>;
    std::map<Key, BlockId> blocks;
    blocks[{0, 10, 0}] = BlockId::STONE; // high ledge, top y=11
    blocks[{0, 9, 1}] = BlockId::STONE;  // next block, top y=10

    auto getter = [&](int x, int y, int z) {
        auto it = blocks.find({x, y, z});
        return it == blocks.end() ? BlockId::AIR : it->second;
    };

    // The center has crossed into z=1, but the player's foot AABB still
    // overlaps z=0. Landing support must remain the high ledge.
    float edgeSupport =
        PlayerPhysics::findSupportHeight(0.5f, 11.001f, 1.10f, getter);
    require(std::abs(edgeSupport - 11.0f) < 0.001f,
            "footprint support ignored the old ledge");

    // Once the footprint fully clears z=0, the lower block becomes the next
    // valid support; gravity remains responsible for reaching it.
    float lowerSupport =
        PlayerPhysics::findSupportHeight(0.5f, 11.001f, 1.31f, getter);
    require(std::abs(lowerSupport - 10.0f) < 0.001f,
            "support did not transition after clearing the ledge");

    require(PlayerPhysics::shouldAutoJump(true, true, true, true, true),
            "clear grounded obstacle did not permit auto jump");
    require(!PlayerPhysics::shouldAutoJump(false, true, true, true, true),
            "disabled auto jump still triggered");
    require(!PlayerPhysics::shouldAutoJump(true, false, true, true, true),
            "airborne auto jump triggered");
    require(!PlayerPhysics::shouldAutoJump(true, true, true, false, true),
            "auto jump ignored current headroom");
    require(!PlayerPhysics::shouldAutoJump(true, true, true, true, false),
            "auto jump ignored target headroom");

    require(PlayerPhysics::movementSubsteps(0.0f) == 1,
            "stationary movement did not retain one collision step");
    require(PlayerPhysics::movementSubsteps(0.19f) == 1,
            "small movement was split unnecessarily");
    require(PlayerPhysics::movementSubsteps(0.21f) == 2,
            "movement larger than the collision bound was not split");
    require(PlayerPhysics::movementSubsteps(-0.61f) == 4,
            "falling movement did not use absolute distance");
    require(PlayerPhysics::waterVerticalVelocity(0.0f, true, false, 0.1f) ==
                Config::WATER_RISE_SPEED,
            "water rise input did not select swim speed");
    require(PlayerPhysics::waterVerticalVelocity(0.0f, false, true, 0.1f) ==
                -Config::WATER_DIVE_SPEED,
            "water dive input did not select dive speed");
    const float sinking = PlayerPhysics::waterVerticalVelocity(0.0f, false, false, 0.1f);
    require(sinking < 0.0f && sinking > -Config::WATER_SINK_SPEED,
            "neutral water movement did not approach gentle sinking");

    std::cout << "player physics logic passed\n";
}
