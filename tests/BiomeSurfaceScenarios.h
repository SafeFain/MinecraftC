#pragma once

#include "world/BiomeBlockLogic.h"
#include "world/SurfaceRules.h"

#include <set>

// Synthetic contexts exercise every biome, including rare ones, independently
// of the climate router. Full chunk/region equivalence remains tested below.
inline void testBiomeSurfaceEcology() {
    std::set<BlockId> found;
    bool seedChangesMaterials = false;
    for (int biome = 0; biome < BIOME_COUNT; ++biome) {
        SurfaceRuleContext context;
        context.biome = static_cast<Biome>(biome);
        context.slope = 0.1f;
        context.river = context.biome == Biome::RIVER;
        context.volcanicWeight = context.biome == Biome::VOLCANIC_HIGHLANDS ? 0.45f : 0.0f;
        int samples = 0;
        for (int z = -64; z < 64; z += 2) {
            for (int x = -64; x < 64; x += 2) {
                context.height = context.biome == Biome::OCEAN ? 52 :
                    context.biome == Biome::DEEP_OCEAN ? 24 : 72 + ((x+64)/16)%12;
                context.waterLevel = 63;
                const auto surface = SurfaceRules::profile(42,x,z,context);
                const auto repeat = SurfaceRules::profile(42,x,z,context);
                require(surface.top == repeat.top && surface.under == repeat.under &&
                        surface.depth == repeat.depth, "biome surface sampling is deterministic");
                const auto other = SurfaceRules::profile(73,x,z,context);
                seedChangesMaterials |= surface.top != other.top || surface.under != other.under;
                for (int depth = 0; depth <= surface.depth; ++depth) {
                    const auto block = SurfaceRules::blockAtDepth(42,x,z,depth,context);
                    if (block >= BlockId::ROOTED_DIRT) found.insert(block);
                }
                const auto plant = SurfaceRules::decoration(42,x,z,context,surface.top);
                if (isBiomePlant(plant)) {
                    require(context.height >= context.waterLevel &&
                            supportsBiomePlant(plant,surface.top),
                            "generated plants need dry, suitable support");
                    found.insert(plant);
                }
                require(SurfaceRules::blockAtDepth(42,x,z,0,context) == surface.top,
                        "final snow/ice surface matches depth sampling");
                ++samples;
            }
        }
        require(samples == 4096, "each of the 30 biomes was sampled");
    }
    require(seedChangesMaterials, "surface materials must vary with seed");
    for (int raw = 201; raw <= 224; ++raw)
        require(found.count(static_cast<BlockId>(raw)) != 0,
                "every added natural block has a reachable surface/plant rule");
    for (int z = -35; z <= 35; ++z) {
        for (int x = -35; x <= 35; ++x) {
            const float a = SurfaceRules::patch(42,x,z,12);
            const float b = SurfaceRules::patch(42,x+1,z,12);
            require(std::abs(a-b) < 0.10f,
                    "world-aligned patches stay smooth across negative cell edges");
        }
    }
    require(!supportsBiomePlant(BlockId::FERN,BlockId::STONE) &&
            !supportsBiomePlant(BlockId::FERN,BlockId::SNOW) &&
            !supportsBiomePlant(BlockId::FERN,BlockId::BLUE_ICE) &&
            !supportsBiomePlant(BlockId::LAVENDER,BlockId::SAND) &&
            supportsBiomePlant(BlockId::DEAD_BUSH,BlockId::RED_SAND) &&
            supportsBiomePlant(BlockId::BEACH_GRASS,BlockId::BLACK_SAND),
            "plant substrates distinguish fertile, dry, snowy and coastal ground");

    // Exercise the shared placement path, including reeds/cattail water gates.
    SurfaceRuleContext context;
    context.biome = Biome::SWAMP;
    context.height = 64;
    context.waterLevel = 63;
    bool testedCattail = false;
    for (int x = -100; x <= 100 && !testedCattail; ++x) {
        if (SurfaceRules::decoration(42,x,0,context,BlockId::PEAT) != BlockId::CATTAIL) continue;
        BlockId placed = BlockId::AIR;
        auto get = [&](int y) { return y == 64 ? BlockId::PEAT : BlockId::AIR; };
        auto set = [&](int, BlockId block) { placed = block; };
        SurfaceRules::decorateColumn(42,x,0,context,get,set,[] { return false; });
        require(placed == BlockId::AIR, "cattails cannot generate away from water");
        SurfaceRules::decorateColumn(42,x,0,context,get,set,[] { return true; });
        require(placed == BlockId::CATTAIL, "cattails generate on supported riverbanks");
        placed = BlockId::AIR;
        SurfaceRules::decorateColumn(42,x,0,context,
            [](int y) { return y == 64 ? BlockId::PEAT : BlockId::WATER; },
            set,[] { return true; });
        require(placed == BlockId::AIR, "plant generation cannot overwrite water");
        testedCattail = true;
    }
    require(testedCattail, "waterbank fixture exercises a real cattail candidate");
}
