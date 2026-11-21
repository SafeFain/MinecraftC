#include "core/RuntimeClock.h"
#include "world/WorldGenContext.h"

#include <cstdint>

bool verifyHeaderConsumer(WorldGenContext context, const RuntimeClock& clock,
                          std::uint64_t expectedHash) {
    return context.seed() == 42 && clock.now() == 7 &&
           WorldGenContext::hashPosition(context.seed(), -4, 2, -8) == expectedHash;
}
