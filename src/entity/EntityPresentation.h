#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Opaque here so the presentation contract stays independent of gameplay and
// model headers; consumers already include entity/EntityLogic.h.
enum class EntityType : uint16_t;

// Gameplay-relevant event fired by the animation currently driving an entity.
// Simulation consumes these without depending on model animation types.
struct EntityPresentationEvent {
    std::string action;
    std::string name;
};

// Narrow bridge from entity simulation to optional presentation state.
// Simulation owns no model assets: it only forwards locomotion/action intents
// and receives fired events. Drawing and asset lifetimes belong to the
// implementing presenter.
class IEntityPresentation {
public:
    virtual ~IEntityPresentation() = default;

    // True when this presentation can animate the entity type. Callers use it
    // before creating retention-visible entries such as LAN death snapshots.
    virtual bool hasVisual(EntityType) const { return false; }
    virtual void clearInstances() {}
    virtual bool play(EntityType, uint64_t, std::string_view) { return false; }
    virtual bool playing(EntityType, uint64_t, std::string_view) const { return false; }
    virtual void setLocomotion(EntityType, uint64_t, float) {}
    virtual std::vector<EntityPresentationEvent> advance(EntityType, uint64_t,
                                                         float) {
        return {};
    }
};

// No-op implementation for headless simulation (tests, dedicated servers and
// any session that has not initialized rendering models yet).
class NullEntityPresentation final : public IEntityPresentation {};
