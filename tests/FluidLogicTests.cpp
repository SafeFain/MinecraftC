#include "world/FluidLogic.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <tuple>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

struct Grid {
    std::map<std::tuple<int, int, int>, BlockId> blocks;

    BlockId get(const glm::ivec3& p) const {
        const auto it = blocks.find({p.x, p.y, p.z});
        return it == blocks.end() ? BlockId::STONE : it->second;
    }
    void set(const glm::ivec3& p, BlockId block) {
        blocks[{p.x, p.y, p.z}] = block;
    }
};
}

int main() {
    require(static_cast<uint8_t>(BlockId::FALLING_WATER) == 104 &&
            static_cast<uint8_t>(BlockId::FALLING_LAVA) == 105 &&
            static_cast<uint8_t>(BlockId::AETHER_GRASS) == 106 &&
            static_cast<uint8_t>(BlockId::STARFLOWER) == 113 &&
            static_cast<uint8_t>(BlockId::COUNT) == 114,
            "falling and Heaven states append without renumbering old ids");
    for (bool lava : {false, true}) {
        const BlockId source = fluidBlockFromAmount(lava, 8);
        const BlockId falling = fluidBlockFromAmount(lava, 8, true);
        const auto sourceState = decodeFluidState(source);
        const auto fallingState = decodeFluidState(falling);
        require(sourceState && sourceState->amount == 8 && sourceState->source &&
                    !sourceState->falling,
                "amount eight decodes to a persistent source");
        require(fallingState && fallingState->amount == 8 && !fallingState->source &&
                    fallingState->falling && isFallingFluid(falling),
                "falling amount eight decodes to a derived falling state");
        for (uint8_t amount = 1; amount < 8; ++amount) {
            const BlockId block = fluidBlockFromAmount(lava, amount);
            const auto state = decodeFluidState(block);
            require(state && state->amount == amount && !state->source &&
                        !state->falling && fluidLevel(block) == 8 - amount,
                    "flowing amount and legacy level round trip");
            require(std::abs(fluidSurfaceHeight(block) - amount / 9.0f) < 0.0001f,
                    "fluid surface height is amount divided by nine");
        }
    }
    require(fluidCanConvertToSource(false) && !fluidCanConvertToSource(true),
            "source conversion defaults match Java Overworld rules");
    require(!isReplaceableByFluid(BlockId::REEDS) &&
            isReplaceableByFluid(BlockId::FIRE) &&
            isReplaceableByFluid(BlockId::TORCH) &&
            isReplaceableByFluid(BlockId::WHEAT_7),
            "fluid passability preserves reeds and replaces plants/fire");

    const FluidAvailable available = [](const glm::ivec3&) { return true; };
    Grid grid;
    const FluidSample sample = [&grid](const glm::ivec3& p) { return grid.get(p); };
    const glm::ivec3 origin{0, 1, 0};
    for (const glm::ivec3& offset : FLUID_HORIZONTAL_OFFSETS) {
        grid.set(origin + offset, BlockId::AIR);
        grid.set(origin + offset + glm::ivec3(0, -1, 0), BlockId::AIR);
    }
    require(preferredFluidDirectionsByAmount(origin, false, 8, false,
                                              sample, available).size() == 4,
            "water spreads to every equally nearest downward direction");

    Grid flat;
    const FluidSample flatSample = [&flat](const glm::ivec3& p) {
        return flat.get(p);
    };
    for (const glm::ivec3& offset : FLUID_HORIZONTAL_OFFSETS)
        flat.set(origin + offset, BlockId::AIR);
    require(preferredFluidDirectionsByAmount(origin, false, 8, false,
                                              flatSample, available).size() == 4,
            "water spreads across a flat supported surface without a drop");

    Grid nearest;
    const FluidSample nearestSample = [&nearest](const glm::ivec3& p) {
        return nearest.get(p);
    };
    nearest.set({1, 1, 0}, BlockId::AIR);
    for (int x = 2; x <= 4; ++x) nearest.set({x, 1, 0}, BlockId::AIR);
    nearest.set({4, 0, 0}, BlockId::AIR);
    const auto waterPath = preferredFluidDirectionsByAmount(
        origin, false, 8, false, nearestSample, available);
    require(waterPath.size() == 1 && waterPath.front() == glm::ivec3(1, 0, 0),
            "water uses the four-cell nearest-drop search");
    const auto lavaPath = preferredFluidDirectionsByAmount(
        origin, true, 8, false, nearestSample, available);
    require(lavaPath.size() == 1 &&
                lavaPath.front() == glm::ivec3(1, 0, 0),
            "Overworld lava uses the shorter two-cell search");

    Grid corners;
    corners.set({0, 0, 0}, BlockId::WATER);
    corners.set({-1, 0, 0}, BlockId::FLOWING_WATER_7);
    corners.set({0, 0, -1}, BlockId::AIR);
    corners.set({-1, 0, -1}, BlockId::STONE);
    const FluidSample cornerSample = [&corners](const glm::ivec3& p) {
        return corners.get(p);
    };
    const float weighted = fluidCornerHeight({0, 0, 0}, false,
                                             cornerSample, available);
    require(std::abs(weighted - 0.75f) < 0.02f,
            "high fluid corner receives tenfold weight while air remains a sample");

    Grid flow;
    flow.set({0, 1, 0}, BlockId::WATER);
    flow.set({1, 1, 0}, BlockId::FLOWING_WATER_7);
    const FluidSample flowSample = [&flow](const glm::ivec3& p) {
        return flow.get(p);
    };
    const glm::vec2 vector = fluidFlowVector({0, 1, 0}, false,
                                             flowSample, available);
    require(vector.x > 0.9f && std::abs(vector.y) < 0.2f,
            "flow vector points toward the lower neighbor");

    require(fluidSpreadDelay(true, fluidBlockFromAmount(true, 6),
                             BlockId::LAVA, 0) == 30 &&
            fluidSpreadDelay(true, fluidBlockFromAmount(true, 6),
                             BlockId::LAVA, 1) == 120 &&
            fluidSpreadDelay(true, BlockId::FALLING_LAVA,
                             BlockId::LAVA, 1) == 30,
            "lava rising-height transitions implement the three-fourths delay branch");

    std::cout << "Fluid logic tests passed\n";
    return 0;
}
