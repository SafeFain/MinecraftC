#pragma once

#include "EntityAiScenarios.h"
#include "entity/ProjectileCollision.h"

namespace ProjectileIntegration {
inline void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAILED projectile: " << message << '\n'; std::exit(1); }
}

inline int run(const std::filesystem::path& assets) {
    using EntityAiScenarios::Scene;
    {
        Scene s(assets);
        s.mobs.spawnArrow({.5, 1.5, .5}, {1000, 0, 0}, 2, true);
        const auto far = s.add(EntityType::Cow, {4.57, 1, .5});
        const auto near = s.add(EntityType::Cow, {4.51, 1, .5});
        const float farHealth = s.mobs.entityById(far)->health;
        const float nearHealth = s.mobs.entityById(near)->health;
        s.step(.01f);
        check(s.mobs.entityById(far)->health == farHealth &&
              s.mobs.entityById(near)->health == nearHealth - 2,
              "overlapping targets must be hit in contact order, not insertion order");
    }
    {
        Scene s(assets);
        s.block(3, 1, 0, BlockId::STONE);
        s.mobs.spawnArrow({.5, 1.5, .5}, {1000, 0, 0}, 2, true);
        const auto behind = s.add(EntityType::Cow, {5.5, 1, .5});
        const float health = s.mobs.entityById(behind)->health;
        s.step(.01f);
        const auto& arrow = s.mobs.entities().front();
        check(s.mobs.entityById(behind)->health == health && arrow.inGround &&
              std::abs(arrow.position.x - 3.0) < 1e-9 && arrow.velocity == glm::vec3(0),
              "wall must shield targets and embed arrow on its entry surface");
    }
    {
        Scene s(assets);
        s.block(3, 1, 0, BlockId::STONE);
        s.mobs.spawnArrow({.5, 1.5, .5}, {1000, 0, 0}, 2, true);
        const auto before = s.add(EntityType::Chicken, {2, 1, .5});
        const float health = s.mobs.entityById(before)->health;
        s.step(.01f);
        check(s.mobs.entityById(before)->health == health - 2 &&
              s.mobs.entities().size() == 1,
              "small target before wall must consume the arrow first");
    }
    {
        Scene s(assets);
        s.block(4, 6, 0, BlockId::STONE);
        const glm::dvec3 origin(.5, 2, .5);
        const glm::vec3 velocity(4, 9.8f, 0);
        const auto preview = projectileBlockHit(origin, velocity, 2.0,
            [&s](int x, int y, int z) { return s.world.getBlock(x, y, z); });
        s.mobs.spawnArrow(origin, velocity, 2, true);
        s.step(2.0f);
        const auto& arrow = s.mobs.entities().front();
        check(preview && arrow.inGround &&
              glm::distance(arrow.position, projectilePosition(origin, velocity, *preview)) < 1e-9 &&
              arrow.facing.y > 0,
              "real flight and preview must hit the arc apex with impact orientation");
    }
    for (bool crouching : {false, true}) {
        Scene s(assets);
        s.player.setPosition({4.5, 1, .5});
        s.player.setMouseLocked(true);
        if (crouching) {
            InputState input;
            input.setVirtual(InputAction::Sneak, 1);
            input.update({});
            s.player.handleMovement(input, 0);
            check(s.player.isSneaking(), "pose fixture failed to crouch");
        }
        const float health = s.player.survivalStats().health();
        s.mobs.spawnArrow({.5, 2.7, .5}, {100, 0, 0}, 2, false);
        s.step(.08f);
        check((s.player.survivalStats().health() == health) == crouching,
              "enemy arrow must use current player pose height");
    }
    for (int frames : {1, 2, 10}) {
        Scene s(assets);
        s.player.setMouseLocked(true);
        s.player.inventory().offhand() = {ItemId::SHIELD, 1, 0};
        s.player.handleMouseButton(MouseButton::Right, ButtonAction::Press);
        s.player.update(.3f);
        s.player.setPosition({4.5, 1, .5});
        const float health = s.player.survivalStats().health();
        s.mobs.spawnArrow({4.5, 2, 5}, {0, 0, -100}, 4, false);
        for (int frame = 0; frame < frames; ++frame) s.step(.1f / frames);
        const auto& arrow = s.mobs.entities().front();
        const glm::dvec3 impact = projectilePosition({4.5, 2, 5}, {0, 0, -100}, .042);
        const glm::vec3 reflected = projectileVelocityAfter({0, 0, -100}, .042f) * -.2f;
        const auto expectedPosition = projectilePosition(impact, reflected, .058);
        const auto expectedVelocity = projectileVelocityAfter(reflected, .058f);
        check(s.player.survivalStats().health() == health && arrow.playerOwned &&
              arrow.facing.z > 0 && glm::distance(arrow.velocity, expectedVelocity) < 1e-5f &&
              glm::distance(arrow.position, expectedPosition) < 1e-5,
              "shield must consume remaining flight time independently of frame rate");
        check(s.player.inventory().offhand().damage > 0, "blocked arrow must wear shield");
    }
    std::cout << "Projectile integration tests passed\n";
    return 0;
}
}
