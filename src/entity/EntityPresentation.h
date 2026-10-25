#pragma once

#include <cstdint>
#include <string_view>

#include "entity/EntityAttack.h"

// Opaque here so the presentation contract stays independent of gameplay and
// model headers; consumers already include entity/EntityLogic.h.
enum class EntityType : uint16_t;

// Narrow bridge from entity simulation to optional presentation state.
// Simulation owns attack wind-up, the one impact event and combat resolution.
// The presenter may supply the matching animation timing and consume action
// intents; drawing and asset lifetimes belong to the implementing presenter.
class IEntityPresentation {
public:
    virtual ~IEntityPresentation() = default;

    // True when this presentation can animate the entity type. Callers use it
    // before creating retention-visible entries such as LAN death snapshots.
    virtual bool hasVisual(EntityType) const { return false; }
    virtual void clearInstances() {}
    virtual bool play(EntityType, uint64_t, std::string_view) { return false; }
    // Animation timing for the attack semantic. Simulation sanitizes the result
    // and falls back to its canonical table, so this may return defaults when
    // models or action graphs are unavailable.
    virtual EntityAttackTiming attackTiming(EntityType) const = 0;
    virtual void setLocomotion(EntityType, uint64_t, float) {}
    virtual void advance(EntityType, uint64_t, float) {}
};

// Timing-only implementation for headless simulation (tests, dedicated servers
// and any session that has not initialized rendering models yet).
class NullEntityPresentation final : public IEntityPresentation {
public:
    EntityAttackTiming attackTiming(EntityType type) const override;
};
