#pragma once

#include "game/Item.h"
#include <functional>
#include <optional>
#include <vector>
#include <glm/glm.hpp>

enum class FishingPhase { Idle, Flying, Waiting, Approaching, Bite, Stuck };
enum class FishingEventKind { Cast, Splash, Approach, Bite, Reel };
struct FishingEvent {
    FishingEventKind kind;
    glm::dvec3 position{0.0};
    ItemStack catchItem{};
    uint16_t wear = 0;
};
struct FishingView {
    FishingPhase phase = FishingPhase::Idle;
    glm::dvec3 position{0.0};
    float animationSeconds = 0.0f;
    float biteSeconds = 0.0f;
    bool active() const { return phase != FishingPhase::Idle; }
};
// Unknown blocks must stay unknown: streaming gaps cannot become fishable air.
struct FishingEnvironment {
    std::function<std::optional<BlockId>(glm::ivec3)> block;
    std::function<bool(glm::ivec3)> skyAccess;
    std::function<bool(glm::ivec3)> rain;
};

class FishingSystem {
public:
    using Random = std::function<float()>;
    explicit FishingSystem(Random random = {});
    void reset(uint64_t seed);
    void cancel();
    void use(const glm::dvec3& eye, const glm::vec3& forward,
             const glm::vec3& inheritedVelocity, const FishingEnvironment& environment);
    void update(float dt, const glm::dvec3& owner, const FishingEnvironment& environment);
    const FishingView& view() const { return m_view; }
    std::vector<FishingEvent> takeEvents();
    static bool openWater(glm::ivec3 surface, const FishingEnvironment& environment);
    static ItemStack loot(bool open, float category, float selection);
private:
    FishingView m_view;
    Random m_random;
    uint64_t m_randomState = 1;
    glm::vec3 m_velocity{0.0f};
    glm::ivec3 m_surface{0};
    double m_remainder = 0.0;
    float m_remaining = 0.0f;
    float m_cooldown = 0.0f;
    float m_approachDuration = 0.0f;
    float m_approachAngle = 0.0f;
    float m_wakeSeconds = 0.0f;
    std::vector<FishingEvent> m_events;
    float random();
    void wait();
    void tick(float dt, const FishingEnvironment& environment);
};
