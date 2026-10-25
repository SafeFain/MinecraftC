#include "entity/EntityPresentation.h"

#include "entity/EntityAttackTiming.h"

EntityAttackTiming NullEntityPresentation::attackTiming(EntityType type) const {
    return defaultAttackTiming(type);
}
