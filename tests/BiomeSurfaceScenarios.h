#pragma once

#include "world/BiomeBlockLogic.h"
#include "world/SurfaceRules.h"
#include "world/CaveEcology.h"

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
    for (int raw=225; raw<=242; ++raw)
        require(found.count(static_cast<BlockId>(raw)) != 0,
                "each v16 surface material and plant must be reachable");
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

inline void testEcologyFormations() {
    SurfaceRuleContext flat;
    flat.biome=Biome::MOUNTAINS; flat.height=72; flat.waterLevel=63; flat.slope=0.1f;
    auto sample = [&](int,int) { return flat; };
    bool foundBoundary=false, differentSeed=false;
    for (int z=-80; z<=80; ++z) for (int x=-80; x<=80; ++x) {
        const auto a=SurfaceRules::rubbleColumn(42,x,z,sample,[](int,int) { return false; });
        const auto repeat=SurfaceRules::rubbleColumn(42,x,z,sample,[](int,int) { return false; });
        const auto other=SurfaceRules::rubbleColumn(73,x,z,sample,[](int,int) { return false; });
        require(a.material==repeat.material && a.height==repeat.height,
                "rubble reconstruction must be deterministic");
        differentSeed |= a.material!=other.material || a.height!=other.height;
        if (!a.height) continue;
        require(a.height<=2 && isNaturalRock(a.material),"rubble height/material bounds");
        require(!SurfaceRules::rubbleColumn(42,x,z,sample,[](int,int) { return true; }).height,
                "rubble must avoid reserved footprints");
        if (x%16==0 || z%16==0) foundBoundary=true;
        BlockId placed=BlockId::AIR;
        require(!SurfaceRules::placeRubble(a,72,
                    [](int y) { return y==72 ? BlockId::STONE : BlockId::WATER; },
                    [&](int,BlockId b) { placed=b; }) && placed==BlockId::AIR,
                "rubble cannot replace water");
        require(!SurfaceRules::placeRubble(a,72,
                    [](int y) { return y==72 ? BlockId::AIR : BlockId::AIR; },
                    [&](int,BlockId b) { placed=b; }),"rubble cannot float");
    }
    require(foundBoundary && differentSeed,"rubble must cross boundaries and vary with seed");
    flat.slope=0.5f;
    require(!SurfaceRules::rubbleColumn(42,0,0,sample,[](int,int) { return false; }).height,
            "steep terrain rejects rubble");

    std::set<BlockId> caveBlocks;
    bool clusterBoundary=false, clusterVariation=false;
    for (int z=-80; z<=80; ++z) for (int x=-80; x<=80; ++x) {
        const auto cluster=CaveEcology::cluster(42,x,-16,z);
        require(cluster==CaveEcology::cluster(42,x,-16,z),"crystal clusters repeat deterministically");
        clusterVariation |= cluster!=CaveEcology::cluster(73,x,-16,z);
        if (isCrystalCluster(cluster)) {
            caveBlocks.insert(cluster);
            clusterBoundary |= x%16==0 || z%16==0;
            require(supportsNaturalDecoration(cluster,BlockId::AMETHYST_BLOCK) &&
                    !supportsNaturalDecoration(cluster,BlockId::DIRT),
                    "crystal clusters require rock support");
        }
        for (int raw=0; raw<5; ++raw) {
            const auto biome=static_cast<CaveBiome>(raw);
            const auto block=CaveEcology::surface(42,x,-16,z,biome,BlockId::STONE);
            require(block==CaveEcology::surface(42,x,-16,z,biome,BlockId::STONE),
                    "cave material patches repeat");
            if (block>=BlockId::CAVE_MOSS) caveBlocks.insert(block);
        }
    }
    for (int raw=243; raw<=251; ++raw)
        require(caveBlocks.count(static_cast<BlockId>(raw)),"every v16 cave material/cluster is reachable");
    require(clusterBoundary && clusterVariation,"crystal clusters cross negative edges and vary by seed");
    require(supportsNaturalDecoration(BlockId::CAVE_GLOWSHROOM,BlockId::CAVE_MOSS) &&
            !supportsNaturalDecoration(BlockId::CAVE_GLOWSHROOM,BlockId::QUARTZ_BLOCK),
            "cave fungus needs moist fertile support");

    // Real decoration on controlled dry/wet caves, with protected occupied air.
    Noise noise(42);
    CaveGenerator caves(noise,42);
    bool foundMushroom=false,foundCluster=false;
    for (int cz=-3; cz<=3; ++cz) for (int cx=-3; cx<=3; ++cx) {
        CaveVolume volume(cx*16,cz*16,16,16);
        auto dry=std::make_unique<Chunk>(cx,cz);
        auto wet=std::make_unique<Chunk>(cx,cz);
        for (int z=0; z<16; ++z) for (int x=0; x<16; ++x) {
            for (int y=-17; y<=-11; ++y) {
                const auto block=(y==-17 || y==-11) ? BlockId::STONE : BlockId::AIR;
                dry->setBlock(x,y,z,block); wet->setBlock(x,y,z,block);
            }
            for (int y=-16; y<=-12; ++y) {
                volume.set(cx*16+x,y,cz*16+z,CaveCell::Air);
                volume.setBiome(cx*16+x,y,cz*16+z,
                    x<8 ? CaveBiome::VerdantGrotto : CaveBiome::CrystalHollow);
            }
        }
        dry->setBlock(0,-16,0,BlockId::CHEST);
        caves.decorateChunk(*dry,volume);
        require(dry->getBlock(0,-16,0)==BlockId::CHEST,"cave decoration preserves occupied space");
        for (int z=0; z<16; ++z) for (int x=0; x<16; ++x) {
            const auto block=dry->getBlock(x,-16,z);
            foundMushroom |= block==BlockId::CAVE_GLOWSHROOM;
            foundCluster |= isCrystalCluster(block);
            if (block==BlockId::CAVE_GLOWSHROOM || isCrystalCluster(block))
                require(supportsNaturalDecoration(block,dry->getBlock(x,-17,z)),
                        "real cave decorations obey shared substrate rules");
            for (int y=-16; y<=-12; ++y) {
                volume.set(cx*16+x,y,cz*16+z,CaveCell::Water);
                wet->setBlock(x,y,z,BlockId::WATER);
            }
        }
        caves.decorateChunk(*wet,volume);
        for (int z=0; z<16; ++z) for (int x=0; x<16; ++x)
            require(wet->getBlock(x,-16,z)==BlockId::WATER,"cave decorations preserve aquifers");
    }
    require(foundMushroom && foundCluster,"real dry caves generate new fungus and crystal groups");
}
