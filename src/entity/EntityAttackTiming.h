#pragma once

#include "entity/EntityAttack.h"
#include "entity/EntityLogic.h"

// Canonical combat timing used when no presentation/model supplies an animation
// graph. Implementation-only header: it includes gameplay templates, so light
// contracts such as EntityPresentation.h must not include it. Native animation
// assets mirror these values; the combat timing integration test compares the
// real and presentation-free paths.
inline EntityAttackTiming defaultAttackTiming(EntityType type) {
    switch (entityTemplate(type)) {
        case EntityType::Skeleton:
            return {0.75f, 0.45f, EntityAttackEvent::Shoot};
        case EntityType::Blastling:
            return {1.20f, 1.00f, EntityAttackEvent::Explode};
        default:
            return {};
    }
}
