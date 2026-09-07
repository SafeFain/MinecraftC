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
    const auto standing = PlayerPhysics::dimensions(PlayerPhysics::Pose::Standing);
    const auto crouching = PlayerPhysics::dimensions(PlayerPhysics::Pose::Crouching);
    const auto swimming = PlayerPhysics::dimensions(PlayerPhysics::Pose::Swimming);
    const auto crawling = PlayerPhysics::dimensions(PlayerPhysics::Pose::Crawling);
    require(standing.height == 1.8f && standing.eyeHeight == 1.62f &&
                crouching.height == 1.5f && crouching.eyeHeight == 1.27f &&
                swimming.height == 0.6f && swimming.eyeHeight == 0.4f &&
                crawling.height == swimming.height &&
                crawling.eyeHeight == swimming.eyeHeight,
            "Java 26.2 pose dimensions or eye heights changed");
    require(PlayerPhysics::resolvePose(PlayerPhysics::Pose::Standing,
                true, true) == PlayerPhysics::Pose::Standing &&
            PlayerPhysics::resolvePose(PlayerPhysics::Pose::Standing,
                false, true) == PlayerPhysics::Pose::Crouching &&
            PlayerPhysics::resolvePose(PlayerPhysics::Pose::Standing,
                false, false) == PlayerPhysics::Pose::Crawling &&
            PlayerPhysics::resolvePose(PlayerPhysics::Pose::Swimming,
                true, false) == PlayerPhysics::Pose::Swimming,
            "pose clearance fallback no longer matches Java's selection order");
    const float firstCrouchEye = PlayerPhysics::approachEyeHeight(
        Config::EYE_HEIGHT, Config::PLAYER_CROUCH_EYE_HEIGHT, 0.05f);
    require(std::abs(firstCrouchEye - 1.445f) < 0.0001f &&
                PlayerPhysics::approachEyeHeight(firstCrouchEye,
                    Config::PLAYER_CROUCH_EYE_HEIGHT, 0.05f) < firstCrouchEye,
            "eye-height interpolation no longer converges by half per Java tick");
    const glm::vec2 sneakCardinal =
        PlayerPhysics::javaMovementInput({0.0f, 1.0f}, true);
    const glm::vec2 sneakDiagonal =
        PlayerPhysics::javaMovementInput({1.0f, 1.0f}, true);
    require(std::abs(sneakCardinal.y - 0.3f) < 0.0001f &&
                std::abs(sneakDiagonal.x - 0.3f) < 0.0001f &&
                std::abs(sneakDiagonal.y - 0.3f) < 0.0001f,
            "Java keyboard input shaping lost sneak cardinal or diagonal speed");
    const glm::vec2 edgeBackoff = PlayerPhysics::backOffFromEdge(
        {0.23f, 0.0f}, 0.6f,
        [](float x, float, float) { return x <= 0.11f; });
    require(edgeBackoff.x > 0.079f && edgeBackoff.x < 0.081f,
            "sneak edge backoff did not use Java's 0.05-block increments");
    require(std::abs(Config::PLAYER_SPEED - 4.3f) < 0.0001f &&
                std::abs(Config::SPRINT_SPEED - 6.72f) < 0.0001f,
            "ground movement speeds do not match the classic survival pace");
    require(std::abs(Config::SPRINT_SPEED / 5.6f - 1.2f) < 0.0001f,
            "sprint speed was not increased by exactly twenty percent");

    float minimumApex = 100.0f;
    float maximumApex = 0.0f;
    float minimumAirTime = 100.0f;
    float maximumAirTime = 0.0f;
    for (float frameRate : {30.0f, 60.0f, 120.0f}) {
        const float dt = 1.0f / frameRate;
        float height = 0.0f;
        float velocity = Config::JUMP_SPEED;
        float apex = 0.0f;
        float elapsed = 0.0f;
        while (height >= 0.0f && elapsed < 2.0f) {
            const PlayerPhysics::VerticalMotion motion =
                PlayerPhysics::integrateGravity(velocity, Config::GRAVITY, dt);
            height += motion.displacement;
            velocity = motion.velocity;
            apex = std::max(apex, height);
            elapsed += dt;
        }
        minimumApex = std::min(minimumApex, apex);
        maximumApex = std::max(maximumApex, apex);
        minimumAirTime = std::min(minimumAirTime, elapsed);
        maximumAirTime = std::max(maximumAirTime, elapsed);
    }
    require(minimumApex > 1.24f && maximumApex < 1.251f &&
                maximumApex - minimumApex < 0.005f,
            "jump apex is outside the target or changes with frame rate");
    require(minimumAirTime > 0.62f && maximumAirTime < 0.65f &&
                maximumAirTime - minimumAirTime < 0.018f,
            "jump airtime is outside the target or changes with frame rate");
    const glm::vec2 visualVelocity = PlayerPhysics::horizontalVelocity(
        {10.0, 5.0, -4.0}, {10.43, 5.0, -3.44}, 0.1f);
    require(std::abs(visualVelocity.x - 4.3f) < 0.0001f &&
                std::abs(visualVelocity.y - 5.6f) < 0.0001f &&
                PlayerPhysics::horizontalVelocity({}, {}, 0.0f) == glm::vec2(0.0f),
            "visual velocity did not reflect actual frame displacement");

    PlayerPhysics::HurtImmunity immunity;
    require(PlayerPhysics::damageAfterImmunity(immunity, 3.0f, 0.5f) == 3.0f,
            "first hit bypasses an inactive hurt cooldown");
    require(PlayerPhysics::damageAfterImmunity(immunity, 3.0f, 0.5f) == 0.0f,
            "equal damage is ignored during hurt immunity");
    require(PlayerPhysics::damageAfterImmunity(immunity, 6.0f, 0.5f) == 3.0f,
            "stronger damage applies only its excess during hurt immunity");
    PlayerPhysics::tickHurtImmunity(immunity, 0.5f);
    require(PlayerPhysics::damageAfterImmunity(immunity, 3.0f, 0.5f) == 3.0f,
            "damage applies normally after hurt immunity expires");
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

    blocks.clear();
    blocks[{0, -40, 0}] = BlockId::STONE;
    float negativeSupport =
        PlayerPhysics::findSupportHeight(0.5f, -38.999f, 0.5f, getter);
    require(std::abs(negativeSupport + 39.0f) < 0.001f,
            "support scan stopped at the old zero-height boundary");

    blocks.clear();
    blocks[{0, 10, 0}] = BlockId::WHITE_BED;
    const float bedSupport =
        PlayerPhysics::findSupportHeight(0.5f, 10.5635f, 0.5f, getter);
    require(std::abs(bedSupport - 10.5625f) < 0.001f,
            "player support ignored the bed's nine-sixteenths collision top");

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
    require(Config::WATER_DRAG == 0.8f && Config::WATER_SPRINT_DRAG == 0.9f &&
                Config::WATER_VERTICAL_DRAG == 0.8f &&
                Config::WATER_CONTROL_ACCELERATION_PER_TICK == 0.8f &&
                Config::WATER_WALL_EXIT_SPEED == 6.0f,
            "Java 26.2 water travel constants changed");

    std::cout << "player physics logic passed\n";
}
