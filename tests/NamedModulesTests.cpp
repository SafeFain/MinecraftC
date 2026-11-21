#include <cstdint>

import minecraftc.core.runtime_clock;
import minecraftc.world.gen_context;

bool verifyHeaderConsumer(WorldGenContext context, const RuntimeClock& clock,
                          std::uint64_t expectedHash);

int main() {
    const WorldGenContext context{42};
    const RuntimeClock clock{[]() -> std::uint64_t { return 7; }};
    const auto hash = WorldGenContext::hashPosition(context.seed(), -4, 2, -8);
    return verifyHeaderConsumer(context, clock, hash) ? 0 : 1;
}
