#pragma once

#include <cstdint>
#include <string_view>

// Neutral combat action contract. Entity simulation owns the action clock and
// the single impact event; presentation may supply the animation timing for the
// same action but must never be the source of authoritative combat events.
enum class EntityAttackEvent : uint8_t { None, Melee, Shoot, Explode };

struct EntityAttackTiming {
    float duration = 0.55f;
    float impact = 0.30f;
    EntityAttackEvent event = EntityAttackEvent::Melee;
};

inline const char* attackEventName(EntityAttackEvent event) {
    switch (event) {
        case EntityAttackEvent::Melee: return "melee";
        case EntityAttackEvent::Shoot: return "shoot";
        case EntityAttackEvent::Explode: return "explode";
        case EntityAttackEvent::None: break;
    }
    return "";
}

inline EntityAttackEvent attackEventFromName(std::string_view name) {
    if (name == "melee") return EntityAttackEvent::Melee;
    if (name == "shoot") return EntityAttackEvent::Shoot;
    if (name == "explode") return EntityAttackEvent::Explode;
    return EntityAttackEvent::None;
}
