#pragma once
#include "EntityAiScenarios.h"
#include "entity/EntityAttack.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

// Combat timing must be simulation-owned: the same scenario has to resolve
// identically with a loaded animation graph, a null presentation and no
// presentation at all. These cases are regression evidence for the defect where
// an empty presenter silently dropped every hostile melee/shoot/explode event.
namespace EntityCombatTimingTests {
using EntityAiScenarios::PresentationMode;
using EntityAiScenarios::Scene;

inline void fail(const std::string& message) {
    std::cerr << "FAILED combat timing: " << message << '\n';
    std::exit(1);
}

inline const char* modeName(PresentationMode mode) {
    switch (mode) {
        case PresentationMode::Real: return "real";
        case PresentationMode::Null: return "null";
        case PresentationMode::Unset: return "unset";
    }
    return "unknown";
}

// Test-only presentation that supplies a scripted attack timing and no visuals.
class ScriptedPresentation final : public IEntityPresentation {
public:
    explicit ScriptedPresentation(EntityAttackTiming timing) : m_timing(timing) {}
    EntityAttackTiming attackTiming(EntityType) const override { return m_timing; }

private:
    EntityAttackTiming m_timing;
};

struct DamageOutcome {
    int windupFrame = -1;
    int frame = -1;
    float damage = 0.0f;
    float remaining = 0.0f;
};

// Returns the first frame on which the zombie removes player health.
inline DamageOutcome runZombie(const std::filesystem::path& assets,
                               PresentationMode mode, int fps,
                               double seconds = 2.0,
                               const EntityAttackTiming* overrideTiming = nullptr) {
    Scene scene(assets, mode);
    std::optional<ScriptedPresentation> scripted;
    if (overrideTiming) {
        scripted.emplace(*overrideTiming);
        scene.mobs.setPresentation(&*scripted);
    }
    scene.add(EntityType::Zombie, {.5, 1, .5});
    scene.player.setPosition({1.65, 1, .5});
    DamageOutcome outcome;
    const int frames = static_cast<int>(std::round(seconds * fps));
    for (int frame = 0; frame < frames; ++frame) {
        const float before = scene.player.survivalStats().health();
        scene.step(1.0f / fps, true);
        if (outcome.windupFrame < 0 && !scene.mobs.entities().empty() &&
            scene.mobs.entities().front().attackPending)
            outcome.windupFrame = frame;
        const float after = scene.player.survivalStats().health();
        if (after < before) {
            outcome.frame = frame;
            outcome.damage = before - after;
            outcome.remaining = after;
            break;
        }
    }
    return outcome;
}

struct ArrowOutcome {
    int frame = -1;
    uint64_t shooterId = 0;
};

// Returns the first frame on which the skeleton has fired a live arrow.
inline ArrowOutcome runSkeleton(const std::filesystem::path& assets,
                                PresentationMode mode, int fps,
                                double seconds = 2.0) {
    Scene scene(assets, mode);
    const uint64_t skeleton = scene.add(EntityType::Skeleton, {.5, 1, .5});
    scene.player.setPosition({8.0, 1, .5});
    ArrowOutcome outcome;
    const int frames = static_cast<int>(std::round(seconds * fps));
    for (int frame = 0; frame < frames; ++frame) {
        scene.step(1.0f / fps, true);
        for (const auto& entity : scene.mobs.entities()) {
            if (entity.type != EntityType::Arrow) continue;
            outcome.frame = frame;
            outcome.shooterId = entity.shooterId;
            break;
        }
        if (outcome.frame >= 0) break;
        if (!scene.mobs.entityById(skeleton))
            fail(std::string("skeleton disappeared before firing with mode ") +
                 modeName(mode));
    }
    return outcome;
}

struct ExplosionOutcome {
    int frame = -1;
    size_t count = 0;
    bool mobRemoved = false;
};

// Returns the first frame on which the blastling produced an explosion event.
inline ExplosionOutcome runBlastling(const std::filesystem::path& assets,
                                     PresentationMode mode, int fps,
                                     double seconds = 2.0) {
    Scene scene(assets, mode);
    const uint64_t blastling = scene.add(EntityType::Blastling, {.5, 1, .5});
    scene.player.setPosition({1.65, 1, .5});
    ExplosionOutcome outcome;
    const int frames = static_cast<int>(std::round(seconds * fps));
    for (int frame = 0; frame < frames; ++frame) {
        scene.step(1.0f / fps, true);
        const auto events = scene.mobs.takeExplosionEvents();
        if (events.empty()) continue;
        outcome.frame = frame;
        outcome.count = events.size();
        outcome.mobRemoved = scene.mobs.entityById(blastling) == nullptr;
        break;
    }
    return outcome;
}

inline void requireSameFrame(const char* scenario, PresentationMode reference,
                             int referenceFrame, PresentationMode other,
                             int otherFrame) {
    if (referenceFrame < 0)
        fail(std::string(scenario) + " produced no event with mode " +
             modeName(reference));
    if (otherFrame < 0)
        fail(std::string(scenario) + " produced no event with mode " +
             modeName(other));
    if (std::abs(referenceFrame - otherFrame) > 1)
        fail(std::string(scenario) + " frame mismatch: " +
             modeName(reference) + "=" + std::to_string(referenceFrame) +
             " " + modeName(other) + "=" + std::to_string(otherFrame));
}

inline void requireFrameRateTolerance(const char* scenario, int expectedFrame,
                                      int fps, int actualFrame) {
    if (actualFrame < 0)
        fail(std::string(scenario) + " produced no event at " +
             std::to_string(fps) + " fps");
    const int tolerance = std::max(1, fps / 30);
    if (std::abs(expectedFrame * fps / 60 - actualFrame) > tolerance)
        fail(std::string(scenario) + " frame rate changed impact timing at " +
             std::to_string(fps) + " fps");
}

inline int run(const std::filesystem::path& assets) {
    const PresentationMode modes[] = {PresentationMode::Real,
                                      PresentationMode::Null,
                                      PresentationMode::Unset};

    // Zombie melee: real animation graph, null presentation and the internal
    // manager fallback must all land the same single hit.
    DamageOutcome zombieReference{};
    float zombieDamage = 0.0f;
    for (PresentationMode mode : modes) {
        const DamageOutcome outcome = runZombie(assets, mode, 60);
        if (outcome.frame < 0)
            fail(std::string("zombie melee did not damage the player with mode ") +
                 modeName(mode));
        if (zombieReference.frame < 0) {
            zombieReference = outcome;
            zombieDamage = outcome.damage;
        } else {
            requireSameFrame("zombie melee", modes[0], zombieReference.frame,
                             mode, outcome.frame);
            if (std::abs(outcome.damage - zombieDamage) > 1e-4f)
                fail("zombie melee damage depends on presentation");
        }
    }
    for (int fps : {30, 60, 120}) {
        const DamageOutcome outcome = runZombie(assets, PresentationMode::Null, fps);
        requireFrameRateTolerance("zombie melee", zombieReference.frame, fps,
                                  outcome.frame);
    }

    // Skeleton ranged attack: the arrow and its owner must not require a model.
    ArrowOutcome skeletonReference{};
    for (PresentationMode mode : modes) {
        const ArrowOutcome outcome = runSkeleton(assets, mode, 60);
        if (outcome.frame < 0)
            fail(std::string("skeleton did not fire an arrow with mode ") +
                 modeName(mode));
        if (skeletonReference.frame < 0) {
            skeletonReference = outcome;
        } else {
            requireSameFrame("skeleton shoot", modes[0], skeletonReference.frame,
                             mode, outcome.frame);
        }
    }

    // Explosion attack: the blastling must explode and die in every mode.
    ExplosionOutcome blastlingReference{};
    for (PresentationMode mode : modes) {
        const ExplosionOutcome outcome = runBlastling(assets, mode, 60);
        if (outcome.frame < 0)
            fail(std::string("blastling did not explode with mode ") +
                 modeName(mode));
        if (outcome.count != 1)
            fail("blastling produced a duplicate explosion event");
        if (!outcome.mobRemoved)
            fail("blastling survived its own explosion");
        if (blastlingReference.frame < 0) {
            blastlingReference = outcome;
        } else {
            requireSameFrame("blastling explode", modes[0], blastlingReference.frame,
                             mode, outcome.frame);
        }
    }

    // The simulation must read the timing supplied by presentation, but it must
    // still own the clock and the single impact.
    {
        const EntityAttackTiming custom{2.0f, 0.9f, EntityAttackEvent::Melee};
        const DamageOutcome early =
            runZombie(assets, PresentationMode::Null, 60, 0.8, &custom);
        if (early.frame >= 0)
            fail("scripted attack timing hit before its impact");
        const DamageOutcome late =
            runZombie(assets, PresentationMode::Null, 60, 1.2, &custom);
        if (late.windupFrame < 0)
            fail("scripted attack never started");
        const int customDelta = late.frame - late.windupFrame;
        if (customDelta < 53 || customDelta > 55)
            fail("scripted attack timing was not honored: delta " +
                 std::to_string(customDelta));
        const EntityAttackTiming invalid{0.0f, -1.0f, EntityAttackEvent::None};
        const DamageOutcome fallback =
            runZombie(assets, PresentationMode::Null, 60, 0.8, &invalid);
        if (fallback.windupFrame < 0)
            fail("fallback attack never started");
        const int fallbackDelta = fallback.frame - fallback.windupFrame;
        if (fallbackDelta < 17 || fallbackDelta > 19)
            fail("invalid attack timing did not fall back to canonical timing");
    }

    // One action resolves at most one hit, and the existing cooldown still
    // spaces successive attacks.
    {
        Scene scene(assets, PresentationMode::Null);
        scene.add(EntityType::Zombie, {.5, 1, .5});
        scene.player.setPosition({1.65, 1, .5});
        std::vector<int> hits;
        float previous = scene.player.survivalStats().health();
        for (int frame = 0; frame < 120; ++frame) {
            scene.step(1.0f / 60, true);
            const float health = scene.player.survivalStats().health();
            if (health < previous) hits.push_back(frame);
            previous = health;
        }
        if (hits.empty())
            fail("headless zombie never resolved an attack in two seconds");
        if (hits.size() > 1 && hits[1] - hits[0] < 50)
            fail("headless zombie ignored its attack cooldown");
    }

    // Occlusion at impact must still cancel damage without a presenter.
    {
        Scene scene(assets, PresentationMode::Null);
        const uint64_t zombie = scene.add(EntityType::Zombie, {.5, 1, .5});
        scene.player.setPosition({1.65, 1, .5});
        for (int frame = 0; frame < 6; ++frame) scene.step(1.0f / 60, true);
        const auto* attacking = scene.mobs.entityById(zombie);
        if (!attacking || !attacking->attackPending)
            fail("headless zombie did not wind up an attack");
        const float health = scene.player.survivalStats().health();
        scene.block(1, 1, 0, BlockId::STONE);
        scene.block(1, 2, 0, BlockId::STONE);
        for (int frame = 0; frame < 60; ++frame) scene.step(1.0f / 60, true);
        if (scene.player.survivalStats().health() != health)
            fail("headless attack impact passed through a wall");
    }

    // Losing targetability during wind-up must cancel the authoritative clock.
    {
        Scene scene(assets, PresentationMode::Null);
        const uint64_t zombie = scene.add(EntityType::Zombie, {.5, 1, .5});
        scene.player.setPosition({1.65, 1, .5});
        for (int frame = 0; frame < 6; ++frame) scene.step(1.0f / 60, true);
        const float health = scene.player.survivalStats().health();
        for (int frame = 0; frame < 60; ++frame) scene.step(1.0f / 60, false);
        if (scene.player.survivalStats().health() != health)
            fail("cancelled headless attack still damaged the player");
        const auto* mob = scene.mobs.entityById(zombie);
        if (!mob || mob->attackPending)
            fail("cancelled headless attack kept its clock running");
    }

    std::cout << "Entity combat timing tests passed\n";
    return 0;
}
} // namespace EntityCombatTimingTests
