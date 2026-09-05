#include "world/StructureGenerator.h"

#include "Config.h"
#include "world/HeightPipeline.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

// "STRUCTUR" — structures own an independent seed domain so adding or
// retuning them cannot perturb terrain, caves, ores, or trees.
constexpr uint64_t STRUCTURE_DOMAIN = 0x5354525543545552ULL;

uint64_t domainFor(StructureType type) {
    return WorldGenContext::mix(
        STRUCTURE_DOMAIN ^
        (static_cast<uint64_t>(type) + 1) * 0x9E3779B97F4A7C15ULL);
}

uint64_t variantSeed(uint64_t cellSeed, StructureType type) {
    return WorldGenContext::mix(cellSeed ^ domainFor(type) ^
                                0xD1B54A32D192ED03ULL);
}

void fillBox(const StructureGenerator::StructureWriter& write, int x0, int y0,
             int z0, int x1, int y1, int z1, BlockId id) {
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) write(x, y, z, id);
}

void fillRing(const StructureGenerator::StructureWriter& write, int x0, int y,
              int z0, int x1, int z1, BlockId id) {
    for (int x = x0; x <= x1; ++x) {
        write(x, y, z0, id);
        write(x, y, z1, id);
    }
    for (int z = z0 + 1; z < z1; ++z) {
        write(x0, y, z, id);
        write(x1, y, z, id);
    }
}

BlockId wheatStage(uint64_t hash) {
    return static_cast<BlockId>(static_cast<uint8_t>(BlockId::WHEAT_0) +
                                static_cast<uint8_t>(hash % 8));
}

struct VillageStyle {
    BlockId wall;
    BlockId pillar;
    BlockId roof;
    BlockId floor;
    BlockId path;
    BlockId foundation;
};

constexpr VillageStyle kPlainsVillage{
    BlockId::PLANKS, BlockId::WOOD, BlockId::PLANKS, BlockId::PLANKS,
    BlockId::COARSE_DIRT, BlockId::COBBLESTONE};
constexpr VillageStyle kDesertVillage{
    BlockId::TERRACOTTA, BlockId::TERRACOTTA, BlockId::RED_SAND,
    BlockId::SAND, BlockId::SAND, BlockId::TERRACOTTA};

void drawRoad(const StructureGenerator::StructureWriter& write, int x0, int z0,
              int x1, int z1, int base, BlockId path,
              ArchitecturalMaterial stepMaterial,
              const StructureGenerator::SurfaceSampler& surfaceSampler) {
    int x = x0;
    int z = z0;
    int y = base;
    auto directionFor = [](int dx, int dz) {
        if (dz < 0) return BedDirection::North;
        if (dx > 0) return BedDirection::East;
        if (dz > 0) return BedDirection::South;
        return BedDirection::West;
    };
    auto pave = [&](int px,int pz,int moveX,int moveZ) {
        const int previousX=x, previousZ=z, previousY=y;
        const int natural=surfaceSampler?surfaceSampler(px,pz):base;
        y=std::clamp(natural,y-1,y+1);
        if (y > previousY) {
            write(px,y,pz,stairBlock(stepMaterial,BlockHalf::Bottom,
                                     directionFor(moveX,moveZ)));
        } else {
            write(px,y,pz,path);
            if (y < previousY)
                write(previousX,previousY,previousZ,
                      stairBlock(stepMaterial,BlockHalf::Bottom,
                                 directionFor(-moveX,-moveZ)));
        }
        if (y != previousY) {
            const BlockId retaining=architecturalBaseBlock(stepMaterial);
            write(px-moveZ,std::min(y,previousY),pz+moveX,retaining);
            write(px+moveZ,std::min(y,previousY),pz-moveX,retaining);
        }
        write(px,y+1,pz,BlockId::AIR);
        write(px,y+2,pz,BlockId::AIR);
        x=px;
        z=pz;
    };
    const int stepX = x1 > x0 ? 1 : -1;
    while (x != x1) {
        pave(x+stepX,z,stepX,0);
    }
    const int stepZ = z1 > z0 ? 1 : -1;
    while (z != z1) {
        pave(x,z+stepZ,0,stepZ);
    }
}

void buildWell(const StructureGenerator::StructureWriter& write, int cx, int cz,
               int base, BlockId ringBlock) {
    fillRing(write, cx - 1, base, cz - 1, cx + 1, cz + 1, ringBlock);
    write(cx, base, cz, BlockId::AIR);
    write(cx, base - 1, cz, BlockId::WATER);
}

void buildHouse(const StructureGenerator::StructureWriter& write, int hx, int hz,
                int base, uint64_t variant, int doorDx, int doorDz,
                const VillageStyle& style,
                const StructureGenerator::SurfaceSampler& surfaceSampler) {
    const bool desert = style.wall == BlockId::TERRACOTTA;
    const int halfX = 2 + static_cast<int>((variant >> 5) & 1u);
    const int halfZ = 2 + static_cast<int>((variant >> 9) & 1u);
    for(int z=hz-halfZ;z<=hz+halfZ;++z) for(int x=hx-halfX;x<=hx+halfX;++x) {
        const int natural=surfaceSampler?surfaceSampler(x,z):base;
        for(int y=natural+1;y<=base;++y)write(x,y,z,style.foundation);
        if(x>hx-halfX&&x<hx+halfX&&z>hz-halfZ&&z<hz+halfZ)
            for(int y=base+1;y<=base+4;++y)write(x,y,z,BlockId::AIR);
    }
    fillRing(write, hx-halfX, base, hz-halfZ, hx+halfX, hz+halfZ,
             style.foundation);
    fillBox(write, hx-halfX+1, base, hz-halfZ+1,
            hx+halfX-1, base, hz+halfZ-1, style.floor);
    for (int y = base + 1; y <= base + 4; ++y) {
        for (int z = hz - halfZ; z <= hz + halfZ; ++z) {
            for (int x = hx - halfX; x <= hx + halfX; ++x) {
                if (x > hx-halfX && x < hx+halfX &&
                    z > hz-halfZ && z < hz+halfZ)
                    continue;
                const bool corner = (x == hx-halfX || x == hx+halfX) &&
                                    (z == hz-halfZ || z == hz+halfZ);
                write(x, y, z, corner ? style.pillar : style.wall);
            }
        }
    }
    const int doorX = hx + doorDx * halfX;
    const int doorZ = hz + doorDz * halfZ;
    write(doorX, base + 1, doorZ, BlockId::AIR);
    write(doorX, base + 2, doorZ, BlockId::AIR);
    if (doorDx != 0) {
        write(hx, base + 2, hz-halfZ, BlockId::GLASS);
        write(hx, base + 2, hz+halfZ, BlockId::GLASS);
    } else {
        write(hx-halfX, base + 2, hz, BlockId::GLASS);
        write(hx+halfX, base + 2, hz, BlockId::GLASS);
    }
    if (desert) {
        fillBox(write, hx-halfX, base+5, hz-halfZ,
                hx+halfX, base+5, hz+halfZ,
                slabBlock(ArchitecturalMaterial::Terracotta, BlockHalf::Bottom));
        fillRing(write, hx-halfX, base+6, hz-halfZ,
                 hx+halfX, hz+halfZ, BlockId::TERRACOTTA);
    } else {
        for (int z = hz-halfZ-1; z <= hz+halfZ+1; ++z) {
            write(hx-halfX-1, base+5, z,
                  stairBlock(ArchitecturalMaterial::Planks, BlockHalf::Bottom,
                             BedDirection::East));
            write(hx+halfX+1, base+5, z,
                  stairBlock(ArchitecturalMaterial::Planks, BlockHalf::Bottom,
                             BedDirection::West));
            write(hx-halfX, base+6, z,
                  stairBlock(ArchitecturalMaterial::Planks, BlockHalf::Bottom,
                             BedDirection::East));
            write(hx+halfX, base+6, z,
                  stairBlock(ArchitecturalMaterial::Planks, BlockHalf::Bottom,
                             BedDirection::West));
        }
        // Seal the lower roof course over the wall plate.  The stair eaves
        // alone only occupied the two outer columns, leaving a one-block-high
        // opening between every wall and the upper roof course.
        fillBox(write, hx-halfX, base+5, hz-halfZ-1,
                hx+halfX, base+5, hz+halfZ+1, style.roof);
        fillBox(write, hx-halfX+1, base+6, hz-halfZ-1,
                hx+halfX-1, base+6, hz+halfZ+1, style.roof);
        const int chimneyX = (variant & 1u) ? hx-halfX+1 : hx+halfX-1;
        for (int y = base+5; y <= base+8; ++y)
            write(chimneyX, y, hz+halfZ-1, BlockId::COBBLESTONE);
    }
    write(doorX+doorDx, base, doorZ+doorDz, style.path);
    // Keep the exterior threshold flush with the path. A half slab at
    // base+1 occupies the villager's body space (villagers stand at base+1),
    // so every generated doorway becomes an impassable step. The path itself
    // supplies the ground and the two clear cells below the roofline remain
    // open for both villagers and the ground-path validator.
    write(doorX+doorDx, base+1, doorZ+doorDz, BlockId::AIR);
    write(doorX+doorDx, base+2, doorZ+doorDz, BlockId::AIR);

    // A generated house is one residential unit. Its bed supplies the
    // deterministic initial villager request and the paired workstation lets
    // the runtime POI system assign that villager's profession.
    const BlockId foot = bedBlock(BedPart::Foot, BedDirection::North);
    const BlockId head = bedBlock(BedPart::Head, BedDirection::North);
    write(hx - 1, base + 1, hz, foot);
    write(hx - 1, base + 1, hz - 1, head);
    constexpr std::array<BlockId, 7> workstations{{
        BlockId::COMPOSTER, BlockId::FLETCHING_TABLE, BlockId::LOOM,
        BlockId::CAULDRON, BlockId::BLAST_FURNACE, BlockId::SMITHING_TABLE,
        BlockId::GRINDSTONE,
    }};
    const size_t profession = static_cast<size_t>(
        WorldGenContext::hashPosition(variant, hx, base, hz) %
        workstations.size());
    write(hx + 1, base + 1, hz, workstations[profession]);
    if ((variant & 3u) == 0u)
        write(hx + 1, base + 3, hz, BlockId::TORCH);
}

void buildFarm(const StructureGenerator::StructureWriter& write, int cx, int cz,
               int base, uint64_t variant) {
    for (int dz = -3; dz <= 3; ++dz) {
        for (int dx = -3; dx <= 3; ++dx) {
            if (dx == 0) {
                write(cx, base, cz + dz, BlockId::WATER);
                continue;
            }
            write(cx + dx, base, cz + dz, BlockId::FARMLAND_7);
            const uint64_t h = WorldGenContext::hashPosition(
                variant, cx + dx, base, cz + dz);
            if (h % 3 == 0) write(cx + dx, base + 1, cz + dz, wheatStage(h));
        }
    }
}

void buildCactusPen(const StructureGenerator::StructureWriter& write, int cx,
                    int cz, int base, uint64_t variant) {
    fillBox(write, cx - 3, base, cz - 3, cx + 3, base, cz + 3, BlockId::SAND);
    fillRing(write, cx - 3, base, cz - 3, cx + 3, cz + 3, BlockId::RED_SAND);
    for (int i = 0; i < 4; ++i) {
        const uint64_t h = WorldGenContext::hashPosition(variant, cx, i, cz);
        const int dx = static_cast<int>((h >> 8) % 5) - 2;
        const int dz = static_cast<int>((h >> 24) % 5) - 2;
        const int height = 1 + static_cast<int>((h >> 40) % 3);
        for (int dy = 1; dy <= height; ++dy)
            write(cx + dx, base + dy, cz + dz, BlockId::CACTUS_BLOCK);
    }
}

void buildVillage(const StructurePlacement& placement,
                  const StructureGenerator::StructureWriter& write,
                  const StructureGenerator::SurfaceSampler& surfaceSampler) {
    const bool desert = placement.type == StructureType::DesertVillage;
    const VillageStyle& style = desert ? kDesertVillage : kPlainsVillage;
    const int cx = (placement.minX + placement.maxX) / 2;
    const int cz = (placement.minZ + placement.maxZ) / 2;
    const int base = placement.baseY;
    const uint64_t variant = placement.variant;
    const auto hash = [&](int x, int y, int z) {
        return WorldGenContext::hashPosition(variant, x, y, z);
    };

    // Central plaza with the village well and torch posts.
    fillBox(write, cx - 2, base, cz - 2, cx + 2, base, cz + 2, style.path);
    buildWell(write, cx, cz, base, style.foundation);
    for (const int dx : {-3, 3}) {
        for (const int dz : {-3, 3}) {
            write(cx + dx, base + 1, cz + dz, style.pillar);
            write(cx + dx, base + 2, cz + dz, style.pillar);
            write(cx + dx, base + 3, cz + dz, BlockId::TORCH);
        }
    }

    // Houses ring the plaza; roads connect each door back to the well.
    constexpr std::array<std::pair<int, int>, 8> kHouseOffsets{{
        {-17, -10}, {17, -10}, {-11, 17}, {11, 17},
        {-18, 5}, {18, 5}, {0, -18}, {0, 18},
    }};
    const int houseCount = 5 + static_cast<int>(hash(cx, 1, cz) % 3);
    for (int i = 0; i < 8 && i < houseCount; ++i) {
        const auto& offset = kHouseOffsets[static_cast<size_t>(i)];
        // Adobe houses have roof terraces rather than deep eaves, so their
        // residential ring can sit slightly closer to the plaza and remain
        // within the advertised 41x41 desert-village reservation.
        const int hx = cx + (desert ? offset.first * 8 / 9 : offset.first);
        const int hz = cz + (desert ? offset.second * 8 / 9 : offset.second);
        int doorDx = cx - hx;
        int doorDz = cz - hz;
        if (std::abs(doorDx) >= std::abs(doorDz)) {
            doorDx = doorDx > 0 ? 1 : -1;
            doorDz = 0;
        } else {
            doorDz = doorDz > 0 ? 1 : -1;
            doorDx = 0;
        }
        drawRoad(write, cx, cz, hx, hz, base, style.path,
                 desert ? ArchitecturalMaterial::Terracotta
                        : ArchitecturalMaterial::Cobblestone,
                 surfaceSampler);
        const int houseBase=surfaceSampler
            ? std::clamp(surfaceSampler(hx,hz),base-6,base+6) : base;
        buildHouse(write, hx, hz, houseBase, hash(cx, 2, hz), doorDx, doorDz,
                   style, surfaceSampler);
    }

    // One corner holds a farm (plains) or a cactus pen (desert).
    const uint64_t corner = hash(cx, 3, cz);
    if (desert)
        buildCactusPen(write, cx - 17, cz + 17,
            surfaceSampler?surfaceSampler(cx-17,cz+17):base, corner);
    else
        buildFarm(write, cx - 17, cz + 17,
            surfaceSampler?surfaceSampler(cx-17,cz+17):base, corner);

    // Deterministic market canopy / public gathering lot opposite the farm.
    const int marketX = cx + 16;
    const int marketZ = cz + 16;
    fillBox(write, marketX-4, base, marketZ-3,
            marketX+4, base, marketZ+3, style.path);
    for (const int dx : {-4, 4}) for (const int dz : {-3, 3}) {
        for (int y=1; y<=3; ++y)
            write(marketX+dx, base+y, marketZ+dz, style.pillar);
    }
    const ArchitecturalMaterial canopy = desert
        ? ArchitecturalMaterial::Terracotta : ArchitecturalMaterial::Planks;
    fillBox(write, marketX-4, base+4, marketZ-3,
            marketX+4, base+4, marketZ+3,
            slabBlock(canopy, BlockHalf::Bottom));
    write(marketX-1, base+1, marketZ, BlockId::CHEST);
    write(marketX+1, base+1, marketZ, BlockId::CRAFTING_TABLE);
}

void buildHut(const StructurePlacement& placement,
              const StructureGenerator::StructureWriter& write) {
    const int cx = (placement.minX + placement.maxX) / 2;
    const int cz = (placement.minZ + placement.maxZ) / 2;
    const int base = placement.baseY;
    fillBox(write, cx - 3, base, cz - 3, cx + 3, base, cz + 3,
            BlockId::COARSE_DIRT);
    fillBox(write, cx - 2, base, cz - 2, cx + 2, base, cz + 2, BlockId::PLANKS);
    for (int y = base + 1; y <= base + 3; ++y) {
        for (int z = cz - 3; z <= cz + 3; ++z) {
            for (int x = cx - 3; x <= cx + 3; ++x) {
                if (x > cx - 3 && x < cx + 3 && z > cz - 3 && z < cz + 3)
                    continue;
                const bool corner = (x == cx - 3 || x == cx + 3) &&
                                    (z == cz - 3 || z == cz + 3);
                write(x, y, z, corner ? BlockId::WOOD : BlockId::PLANKS);
            }
        }
    }
    write(cx, base + 1, cz + 3, BlockId::AIR);
    write(cx, base + 2, cz + 3, BlockId::AIR);
    write(cx - 3, base + 2, cz, BlockId::GLASS);
    write(cx + 3, base + 2, cz, BlockId::GLASS);
    // Deep eaves, ridge and offset chimney make the hut readable at range.
    for (int z=cz-4; z<=cz+4; ++z) {
        write(cx-4, base+4, z, stairBlock(ArchitecturalMaterial::Planks,
            BlockHalf::Bottom, BedDirection::East));
        write(cx+4, base+4, z, stairBlock(ArchitecturalMaterial::Planks,
            BlockHalf::Bottom, BedDirection::West));
        write(cx-3, base+5, z, stairBlock(ArchitecturalMaterial::Planks,
            BlockHalf::Bottom, BedDirection::East));
        write(cx+3, base+5, z, stairBlock(ArchitecturalMaterial::Planks,
            BlockHalf::Bottom, BedDirection::West));
    }
    // The first roof course is also the hut's ceiling.  Without these center
    // blocks the raised stair eaves left the complete wall plate open.
    fillBox(write, cx-3, base+4, cz-4, cx+3, base+4, cz+4,
            BlockId::PLANKS);
    fillBox(write, cx-2, base+5, cz-4, cx+2, base+5, cz+4, BlockId::PLANKS);
    for (int y=base+4; y<=base+7; ++y)
        write(cx+2, y, cz-2, BlockId::COBBLESTONE);
    write(cx - 1, base + 1, cz - 1,
          bedBlock(BedPart::Foot, BedDirection::North));
    write(cx - 1, base + 1, cz - 2,
          bedBlock(BedPart::Head, BedDirection::North));
    write(cx + 1, base + 1, cz + 1, BlockId::CHEST);
    // Door-side torch post and a short path.
    write(cx + 1, base + 1, cz + 3, BlockId::WOOD);
    write(cx + 1, base + 2, cz + 3, BlockId::TORCH);
    write(cx, base, cz + 4, BlockId::COARSE_DIRT);
    write(cx, base, cz + 5, BlockId::COARSE_DIRT);
    // Side lean-to with a work bench.
    for (int z=cz-1; z<=cz+2; ++z)
        write(cx-4, base+3, z, slabBlock(ArchitecturalMaterial::Planks,
                                        BlockHalf::Bottom));
    write(cx-4, base+1, cz, BlockId::CRAFTING_TABLE);
    // Keep the decorative sapling outside the shell instead of replacing a
    // lower-course wall block.
    write(cx + ((placement.variant & 1u) ? 4 : -4), base+1, cz+3,
          BlockId::OAK_SAPLING);
}

void buildCamp(const StructurePlacement& placement,
               const StructureGenerator::StructureWriter& write) {
    const int cx = (placement.minX + placement.maxX) / 2;
    const int cz = (placement.minZ + placement.maxZ) / 2;
    const int base = placement.baseY;
    const uint64_t variant = placement.variant;
    fillBox(write, cx - 4, base, cz - 4, cx + 4, base, cz + 4,
            BlockId::COARSE_DIRT);
    // Broken two-high cobble wall segments on two sides.
    for (int x = cx - 4; x <= cx - 1; ++x)
        for (int y = base + 1; y <= base + 2; ++y)
            write(x, y, cz - 4, BlockId::COBBLESTONE);
    for (int z = cz + 1; z <= cz + 4; ++z)
        for (int y = base + 1; y <= base + 2; ++y)
            write(cx + 4, y, z, BlockId::COBBLESTONE);
    // A fallen spruce log.
    for (int x = cx - 1; x <= cx + 1; ++x)
        write(x, base + 1, cz + 2, BlockId::SPRUCE_WOOD);
    // Campfire ring with a torch standing in for the fire.
    fillRing(write, cx - 1, base + 1, cz - 1, cx + 1, cz + 1,
             BlockId::COBBLESTONE);
    write(cx, base + 1, cz, BlockId::TORCH);
    // Supplies and a light post.
    write(cx - 2, base + 1, cz + 3, BlockId::CHEST);
    write(cx + 3, base + 1, cz - 3, BlockId::WOOD);
    write(cx + 3, base + 2, cz - 3, BlockId::TORCH);
    const uint64_t h = WorldGenContext::hashPosition(variant, cx, 0, cz);
    if (h % 2 == 0) write(cx + 2, base + 1, cz + 2, BlockId::COBBLESTONE);
    // Two damaged A-frame tents and bedrolls establish an actual camp layout.
    for (const int side : {-1, 1}) {
        const int tx = cx + side * 4;
        for (int dz=-2; dz<=2; ++dz) {
            write(tx-1, base+1, cz+dz, BlockId::WHITE_WOOL);
            write(tx+1, base+1, cz+dz, BlockId::WHITE_WOOL);
            if ((h + dz + side) % 4 != 0)
                write(tx, base+2, cz+dz, BlockId::WHITE_WOOL);
        }
        write(tx, base+1, cz, BlockId::AIR);
        write(tx, base+1, cz+1,
              bedBlock(BedPart::Foot, BedDirection::North));
        write(tx, base+1, cz,
              bedBlock(BedPart::Head, BedDirection::North));
    }
}

void buildDesertWell(const StructurePlacement& placement,
                     const StructureGenerator::StructureWriter& write) {
    const int cx = (placement.minX + placement.maxX) / 2;
    const int cz = (placement.minZ + placement.maxZ) / 2;
    const int base = placement.baseY;
    // Water sits one block below the dug-out center.
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            write(cx + dx, base, cz + dz, BlockId::AIR);
            write(cx + dx, base - 1, cz + dz, BlockId::WATER);
            write(cx + dx, base - 2, cz + dz, BlockId::WATER);
            write(cx + dx, base - 3, cz + dz, BlockId::TERRACOTTA);
        }
    }
    fillRing(write, cx - 3, base, cz - 3, cx + 3, cz + 3,
             BlockId::SAND);
    fillRing(write, cx - 2, base + 1, cz - 2, cx + 2, cz + 2,
             BlockId::TERRACOTTA);
    for (const int dx : {-2, 2}) {
        for (const int dz : {-2, 2}) {
            write(cx + dx, base + 2, cz + dz, BlockId::TERRACOTTA);
            write(cx + dx, base + 3, cz + dz, BlockId::TERRACOTTA);
        }
    }
    fillBox(write, cx - 2, base + 4, cz - 2, cx + 2, base + 4, cz + 2,
            slabBlock(ArchitecturalMaterial::Terracotta, BlockHalf::Bottom));
    for (const int dx : {-3, 3}) {
        write(cx+dx, base+1, cz,
              stairBlock(ArchitecturalMaterial::Terracotta, BlockHalf::Bottom,
                         dx < 0 ? BedDirection::East : BedDirection::West));
    }
    for (const int dz : {-3, 3}) {
        write(cx, base+1, cz+dz,
              stairBlock(ArchitecturalMaterial::Terracotta, BlockHalf::Bottom,
                         dz < 0 ? BedDirection::South : BedDirection::North));
    }
    write(cx - 2, base + 2, cz, BlockId::SAND);
    write(cx + 2, base + 2, cz, BlockId::SAND);
    const int ruinX=(placement.variant&1u)?-3:3;
    write(cx+ruinX,base+1,cz+2,BlockId::TERRACOTTA);
    if((placement.variant>>1)&1u)
        write(cx+ruinX,base+2,cz+2,
              slabBlock(ArchitecturalMaterial::Terracotta,BlockHalf::Bottom));
}

void buildIgloo(const StructurePlacement& placement,
                const StructureGenerator::StructureWriter& write) {
    const int cx = (placement.minX + placement.maxX) / 2;
    const int cz = (placement.minZ + placement.maxZ) / 2;
    const int base = placement.baseY;
    // Half-dome shell: 3×3 → 3×3 → 2×2-ish → cap, kept hollow.  Each
    // column contributes only its ceiling block; the outer ring is solid
    // wall, and the volume below the ceiling is carved to air so the
    // interior stays walkable (same silhouette as the old solid dome).
    const auto radius2At = [](int dy) {
        return dy < 2 ? 10 : (dy == 2 ? 5 : 1);
    };
    for (int dz = -3; dz <= 3; ++dz) {
        for (int dx = -3; dx <= 3; ++dx) {
            const int dist2 = dx * dx + dz * dz;
            if (dist2 > 10) continue;
            int top = 0;
            for (int dy = 3; dy >= 0; --dy) {
                if (dist2 <= radius2At(dy)) {
                    top = dy;
                    break;
                }
            }
            const bool outerColumn =
                (dx - 1) * (dx - 1) + dz * dz > 10 ||
                (dx + 1) * (dx + 1) + dz * dz > 10 ||
                dx * dx + (dz - 1) * (dz - 1) > 10 ||
                dx * dx + (dz + 1) * (dz + 1) > 10;
            if (outerColumn) {
                // Outer ring: solid snow wall up to the ring height.
                for (int dy = 0; dy <= top; ++dy)
                    write(cx + dx, base + 1 + dy, cz + dz, BlockId::SNOW);
            } else {
                // Hollow interior: ceiling block plus carved air beneath.
                write(cx + dx, base + 1 + top, cz + dz, BlockId::SNOW);
                for (int dy = 0; dy < top; ++dy)
                    write(cx + dx, base + 1 + dy, cz + dz, BlockId::AIR);
            }
        }
    }
    // South-facing entrance.
    write(cx, base + 1, cz - 3, BlockId::AIR);
    write(cx, base + 2, cz - 3, BlockId::AIR);
    // Snow floor with a white-wool rug.
    fillBox(write, cx - 2, base, cz - 2, cx + 2, base, cz + 2, BlockId::SNOW);
    fillBox(write, cx - 1, base, cz - 1, cx + 1, base, cz + 1,
            BlockId::WHITE_WOOL);
    // Bed along the east wall and a torch on the north wall.
    write(cx + 1, base + 1, cz, bedBlock(BedPart::Foot, BedDirection::North));
    write(cx + 1, base + 1, cz - 1,
          bedBlock(BedPart::Head, BedDirection::North));
    write(cx, base + 2, cz + 2, BlockId::TORCH);
    write(cx - 1, base + 1, cz + 1, BlockId::CHEST);
    // Three-block entrance tunnel, side snow banks and a short chimney.
    for (int dz=-6; dz<=-4; ++dz) {
        write(cx-1, base+1, cz+dz, BlockId::SNOW);
        write(cx+1, base+1, cz+dz, BlockId::SNOW);
        write(cx, base+3, cz+dz, BlockId::SNOW);
        write(cx, base+1, cz+dz, BlockId::AIR);
        write(cx, base+2, cz+dz, BlockId::AIR);
    }
    fillBox(write, cx-4, base, cz-2, cx-3, base+1, cz+1, BlockId::SNOW);
    const int chimneyX=(placement.variant&1u)?2:-2;
    write(cx+chimneyX, base+4, cz+1, BlockId::COBBLESTONE);
    write(cx+chimneyX, base+5, cz+1, BlockId::COBBLESTONE);
}

void buildTower(const StructurePlacement& placement,
                const StructureGenerator::StructureWriter& write) {
    const int cx = (placement.minX + placement.maxX) / 2;
    const int cz = (placement.minZ + placement.maxZ) / 2;
    const int base = placement.baseY;
    const uint64_t variant = placement.variant;
    const int height = 12 + static_cast<int>(
        WorldGenContext::hashPosition(variant, cx, 0, cz) % 5);
    fillBox(write, cx - 4, base, cz - 4, cx + 4, base, cz + 4,
            BlockId::COARSE_DIRT);
    for (int y = base + 1; y <= base + height; ++y) {
        for (int dz = -3; dz <= 3; ++dz) {
            for (int dx = -3; dx <= 3; ++dx) {
                if (dx > -3 && dx < 3 && dz > -3 && dz < 3) continue;
                const uint64_t h = WorldGenContext::hashPosition(
                    variant, cx + dx, y, cz + dz);
                if (y >= base + height - 1 && h % 4 == 0) continue;
                if (y > base + 3 && h % 17 == 0) continue;
                write(cx + dx, y, cz + dz,
                      h % 11 == 0 ? BlockId::MOSS : BlockId::COBBLESTONE);
            }
        }
    }
    write(cx, base+1, cz-3, BlockId::AIR);
    write(cx, base+2, cz-3, BlockId::AIR);
    // Broken internal floors and a contiguous stair that winds around the core.
    constexpr std::array<std::pair<int,int>,8> stairPath{{
        {0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}}};
    for (int y=base+1; y<base+height-1; ++y) {
        const size_t phase=static_cast<size_t>(y-base-1)%stairPath.size();
        const auto [sx,sz]=stairPath[phase];
        const auto [nx,nz]=stairPath[(phase+1)%stairPath.size()];
        const BedDirection direction = nz < sz ? BedDirection::North :
            nx > sx ? BedDirection::East :
            nz > sz ? BedDirection::South : BedDirection::West;
        write(cx+sx, y, cz+sz,
              stairBlock(ArchitecturalMaterial::Cobblestone, BlockHalf::Bottom,
                         direction));
        if ((y-base)%4==0)
            fillRing(write,cx-2,y,cz-2,cx+2,cz+2,BlockId::COBBLESTONE);
    }
    write(cx+1, base+1, cz+1, BlockId::CHEST);
    for (int dx=-4; dx<=4; dx+=2)
        write(cx+dx, base+1, cz+4,
              slabBlock(ArchitecturalMaterial::Cobblestone, BlockHalf::Bottom));
}

void buildLumberCamp(const StructurePlacement& placement,
                     const StructureGenerator::StructureWriter& write) {
    const int cx = (placement.minX + placement.maxX) / 2;
    const int cz = (placement.minZ + placement.maxZ) / 2;
    const int base = placement.baseY;
    fillBox(write, cx - 4, base, cz - 4, cx + 4, base, cz + 4,
            BlockId::COARSE_DIRT);
    // Two stacked-log piles.
    for (const int side : {-3, 3}) {
        const int pz = cz + (side < 0 ? -2 : 2);
        for (int x = side - 1; x <= side + 1; ++x)
            write(cx + x, base + 1, pz, BlockId::SPRUCE_WOOD);
        write(cx + side, base + 2, pz, BlockId::SPRUCE_WOOD);
    }
    // Workbench with a crafting table and supplies.
    write(cx, base + 1, cz, BlockId::PLANKS);
    write(cx + 1, base + 1, cz, BlockId::CRAFTING_TABLE);
    write(cx - 1, base + 1, cz, BlockId::CHEST);
    // Light posts.
    write(cx - 3, base + 1, cz - 3, BlockId::WOOD);
    write(cx - 3, base + 2, cz - 3, BlockId::TORCH);
    write(cx + 3, base + 1, cz + 3, BlockId::WOOD);
    write(cx + 3, base + 2, cz + 3, BlockId::TORCH);
    // Fresh saplings on the north edge.
    for (int dx = -1; dx <= 1; ++dx)
        write(cx + dx, base + 1, cz + 3, BlockId::SPRUCE_SAPLING);
    // Open-sided saw shelter with a broad pitched roof.
    for (const int dx : {-5, 5}) for (const int dz : {-4, 0})
        for (int y=1; y<=4; ++y)
            write(cx+dx, base+y, cz+dz, BlockId::SPRUCE_WOOD);
    for (int z=cz-5; z<=cz+1; ++z) {
        write(cx-6, base+5, z, stairBlock(ArchitecturalMaterial::Planks,
            BlockHalf::Bottom, BedDirection::East));
        write(cx+6, base+5, z, stairBlock(ArchitecturalMaterial::Planks,
            BlockHalf::Bottom, BedDirection::West));
        write(cx-5, base+6, z, stairBlock(ArchitecturalMaterial::Planks,
            BlockHalf::Bottom, BedDirection::East));
        write(cx+5, base+6, z, stairBlock(ArchitecturalMaterial::Planks,
            BlockHalf::Bottom, BedDirection::West));
        fillBox(write,cx-4,base+6,z,cx+4,base+6,z,BlockId::PLANKS);
    }
    // Stumps and hauling trail.
    for (const auto& offset : std::array<std::pair<int,int>,4>{{
            {-6,4},{-3,6},{3,6},{6,3}}})
        write(cx+offset.first,base+1,cz+offset.second,BlockId::SPRUCE_WOOD);
    for (int dz=5; dz<=8; ++dz)
        write(cx,base,cz+dz,BlockId::COARSE_DIRT);
    write(cx+((placement.variant&1u)?5:-5),base+1,cz+5,
          BlockId::SPRUCE_SAPLING);
}

void buildXiguangRuin(const StructurePlacement& placement,
                      const StructureGenerator::StructureWriter& write) {
    const int cx=(placement.minX+placement.maxX)/2;
    const int cz=(placement.minZ+placement.maxZ)/2;
    const int base=placement.baseY;
    const uint64_t variant=placement.variant;
    // Raised radial sanctuary with four independently ruined gateways.
    for (int dz=-6; dz<=6; ++dz) for (int dx=-6; dx<=6; ++dx) {
        const int d2=dx*dx+dz*dz;
        if (d2<=36) write(cx+dx,base+1,cz+dz,
            d2>24 ? slabBlock(ArchitecturalMaterial::Sunstone,BlockHalf::Bottom)
                  : BlockId::SUNSTONE);
    }
    for (const BedDirection direction : {BedDirection::North,BedDirection::East,
                                         BedDirection::South,BedDirection::West}) {
        const glm::ivec3 out=bedDirectionOffset(direction);
        const glm::ivec3 side(-out.z,0,out.x);
        for (int step=6; step<=9; ++step)
            write(cx+out.x*step,base+1,cz+out.z*step,
                  slabBlock(ArchitecturalMaterial::Sunstone,BlockHalf::Bottom));
        const int intact=3+static_cast<int>(WorldGenContext::hashPosition(
            variant,out.x,2,out.z)%4u);
        for (const int s : {-2,2}) for (int y=2; y<=intact; ++y)
            write(cx+out.x*7+side.x*s,base+y,cz+out.z*7+side.z*s,
                  BlockId::SUNSTONE);
        for (int s=-2; s<=2; ++s)
            if ((variant+s+static_cast<int>(direction))%5!=0)
                write(cx+out.x*7+side.x*s,base+intact,
                      cz+out.z*7+side.z*s,
                      slabBlock(ArchitecturalMaterial::Sunstone,BlockHalf::Bottom));
    }
    for (int y=2; y<=4; ++y) write(cx,base+y,cz,BlockId::SUNSTONE);
    write(cx,base+5,cz,BlockId::STAR_CRYSTAL);
    write(cx+2,base+2,cz,BlockId::CHEST);
    for (int i=0;i<12;++i) {
        const uint64_t h=WorldGenContext::hashPosition(variant,cx+i,7,cz-i);
        const int dx=static_cast<int>((h>>8)%19)-9;
        const int dz=static_cast<int>((h>>24)%19)-9;
        write(cx+dx,base+1,cz+dz,(h&1u)?BlockId::SUNSTONE:BlockId::MOSS);
    }
}

void buildStarCrystalGeode(const StructurePlacement& placement,
                           const StructureGenerator::StructureWriter& write) {
    const int cx=(placement.minX+placement.maxX)/2;
    const int cz=(placement.minZ+placement.maxZ)/2;
    const int base=placement.baseY;
    const uint64_t variant=placement.variant;
    // Cracked hollow shell; south side and roof deliberately remain open.
    for(int dy=0;dy<=7;++dy) for(int dz=-5;dz<=5;++dz)
        for(int dx=-5;dx<=5;++dx) {
            const int d2=dx*dx+dz*dz+(dy-3)*(dy-3);
            if(d2<17||d2>29) continue;
            const uint64_t h=WorldGenContext::hashPosition(variant,dx,dy,dz);
            if ((dz<-3&&std::abs(dx)<=1&&dy<=3) || h%13u==0) continue;
            write(cx+dx,base+dy,cz+dz,
                  h%5u==0?BlockId::MOSS:BlockId::CLOUDSTONE);
        }
    fillBox(write,cx-3,base,cz-3,cx+3,base,cz+3,BlockId::MOSS);
    for(int dy=1;dy<=5;++dy)
        write(cx,base+dy,cz,BlockId::STAR_CRYSTAL);
    for (const auto& p : std::array<glm::ivec3,4>{{
            {-2,1,1},{2,1,-1},{1,1,2},{-1,1,-2}}}) {
        write(cx+p.x,base+p.y,cz+p.z,BlockId::STAR_CRYSTAL);
        write(cx+p.x,base+p.y+1,cz+p.z,BlockId::STAR_CRYSTAL);
    }
    write(cx+3,base+1,cz+1,BlockId::CHEST);
}

void buildCloudspireTower(const StructurePlacement& placement,
                          const StructureGenerator::StructureWriter& write) {
    const int cx=(placement.minX+placement.maxX)/2;
    const int cz=(placement.minZ+placement.maxZ)/2;
    const int base=placement.baseY;
    const int height=18+static_cast<int>(placement.variant%7u);
    fillBox(write,cx-4,base,cz-4,cx+4,base,cz+4,BlockId::SUNSTONE);
    constexpr std::array<std::pair<int,int>,8> stairPath{{
        {0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}}};
    for(int y=1;y<=height;++y) {
        const int radius=y>height-5?2:3;
        fillRing(write,cx-radius,base+y,cz-radius,cx+radius,cz+radius,
                 y%5==0?BlockId::CLOUDSTONE:BlockId::SUNSTONE);
        if(y<=3){write(cx,base+y,cz-3,BlockId::AIR);}
        if(y==6||y==12) {
            for(int dz=-5;dz<=5;++dz) for(int dx=-5;dx<=5;++dx) {
                const int edge=std::max(std::abs(dx),std::abs(dz));
                if(edge>=2)
                    write(cx+dx,base+y,cz+dz,
                          slabBlock(ArchitecturalMaterial::Cloudstone,
                                    BlockHalf::Bottom));
                if(edge==5)
                    write(cx+dx,base+y+1,cz+dz,
                          slabBlock(ArchitecturalMaterial::Cloudstone,
                                    BlockHalf::Bottom));
            }
        }
        const size_t phase=static_cast<size_t>(y-1)%stairPath.size();
        const auto [sx,sz]=stairPath[phase];
        const auto [nx,nz]=stairPath[(phase+1)%stairPath.size()];
        const BedDirection direction = nz < sz ? BedDirection::North :
            nx > sx ? BedDirection::East :
            nz > sz ? BedDirection::South : BedDirection::West;
        write(cx+sx,base+y,cz+sz,stairBlock(
            ArchitecturalMaterial::Sunstone,BlockHalf::Bottom,
            direction));
    }
    fillBox(write,cx-3,base+height+1,cz-3,cx+3,base+height+1,cz+3,
            slabBlock(ArchitecturalMaterial::Cloudstone,BlockHalf::Bottom));
    for (const glm::ivec3& o : std::array<glm::ivec3,4>{{
            {3,0,0},{-3,0,0},{0,0,3},{0,0,-3}}}) {
        write(cx+o.x,base+height+2,cz+o.z,BlockId::SUNSTONE);
        write(cx+o.x,base+height+3,cz+o.z,BlockId::STAR_CRYSTAL);
    }
    write(cx,base+height+2,cz,BlockId::CLOUDSTONE);
    write(cx,base+height+3,cz,BlockId::STAR_CRYSTAL);
    write(cx+1,base+1,cz+1,BlockId::CHEST);
}

} // namespace

StructureGenerator::StructureGenerator(uint64_t seed,
                                       const HeightPipeline& heightPipeline)
    : m_context(seed)
    , m_structureSeed(m_context.derive(STRUCTURE_DOMAIN))
    , m_heightPipeline(heightPipeline)
{}

int StructureGenerator::floorDiv(int value, int divisor) {
    int quotient = value / divisor;
    const int remainder = value % divisor;
    if (remainder != 0 && ((remainder < 0) != (divisor < 0))) --quotient;
    return quotient;
}

const StructureGenerator::TypeParams& StructureGenerator::params(
    StructureType type) {
    static constexpr std::array<TypeParams, 9> table{{
        {StructureType::None, 0, 0, 0, 0},
        // Village candidates still pass biome, spacing and full-footprint
        // terrain checks. Plains have a 40% cell chance; desert candidates run
        // every cell because the single eligible desert biome is much rarer.
        // Both remain meaningfully gated after the old 12% prefilter.
        {StructureType::Village, 512, 40, 6, 16},
        {StructureType::DesertVillage, 512, 100, 6, 15},
        {StructureType::TravelerHut, 64, 10, 2, 9},
        {StructureType::AbandonedCamp, 80, 9, 2, 5},
        {StructureType::DesertWell, 64, 12, 2, 6},
        {StructureType::Igloo, 80, 12, 2, 7},
        {StructureType::RuinedTower, 128, 10, 3, 18},
        {StructureType::LumberCamp, 96, 10, 2, 8},
    }};
    const int index = static_cast<int>(type);
    return table[index >= 0 && index < static_cast<int>(table.size())
                     ? index : 0];
}

bool StructureGenerator::acceptsBiome(StructureType type, Biome biome) {
    switch (type) {
        case StructureType::Village:
            return biome == Biome::PLAINS || biome == Biome::SUNFLOWER_PLAINS ||
                   biome == Biome::MEADOW;
        case StructureType::DesertVillage:
        case StructureType::DesertWell:
            return biome == Biome::DESERT;
        case StructureType::TravelerHut:
            return biome == Biome::PLAINS || biome == Biome::SUNFLOWER_PLAINS ||
                   biome == Biome::MEADOW || biome == Biome::FOREST ||
                   biome == Biome::BIRCH_FOREST || biome == Biome::SAVANNA ||
                   biome == Biome::DRY_WOODLAND;
        case StructureType::AbandonedCamp:
            return biome == Biome::FOREST || biome == Biome::TAIGA ||
                   biome == Biome::BIRCH_FOREST || biome == Biome::PLAINS ||
                   biome == Biome::MEADOW || biome == Biome::HILLS ||
                   biome == Biome::DRY_WOODLAND;
        case StructureType::Igloo:
            return biome == Biome::SNOW_TUNDRA;
        case StructureType::RuinedTower:
            return biome == Biome::HILLS || biome == Biome::ROCKY_STEPPE ||
                   biome == Biome::PLAINS || biome == Biome::DRY_WOODLAND;
        case StructureType::LumberCamp:
            return biome == Biome::TAIGA;
        default:
            return false;
    }
}

int StructureGenerator::halfSize(StructureType type, uint64_t variant) {
    (void)variant;
    switch (type) {
        case StructureType::Village:
            return 22;
        case StructureType::DesertVillage:
            return 20;
        case StructureType::TravelerHut:
            return 5;
        case StructureType::Igloo:
            return 7;
        case StructureType::AbandonedCamp:
            return 6;
        case StructureType::RuinedTower:
            return 5;
        case StructureType::LumberCamp:
            return 8;
        case StructureType::DesertWell:
            return 4;
        default:
            return 3;
    }
}

int StructureGenerator::maxHalfSize(StructureType type) {
    switch (type) {
        case StructureType::Village: return 22;
        case StructureType::DesertVillage: return 20;
        default: return halfSize(type, 0);
    }
}

StructureGenerator::Candidate StructureGenerator::candidateForCell(
    StructureType type, int cellX, int cellZ) const {
    const TypeParams& p = params(type);
    const uint64_t h = WorldGenContext::hashPosition(
        m_structureSeed ^ domainFor(type), cellX, 0, cellZ);
    Candidate candidate;
    candidate.type = type;
    candidate.chance = h % 100u < static_cast<uint64_t>(p.chancePercent);
    const bool large = type == StructureType::Village ||
                       type == StructureType::DesertVillage;
    const int margin = large ? 16 : 8;
    const int span = p.cell - 2 * margin;
    candidate.x = cellX * p.cell + margin + static_cast<int>((h >> 8) % span);
    candidate.z = cellZ * p.cell + margin +
                  static_cast<int>((h >> 24) % span);
    candidate.variant = variantSeed(h, type);
    candidate.priority = WorldGenContext::mix(h ^ 0x7F4A7C159D88A3B1ULL);
    const int half = halfSize(type, candidate.variant);
    candidate.minX = candidate.x - half;
    candidate.maxX = candidate.x + half;
    candidate.minZ = candidate.z - half;
    candidate.maxZ = candidate.z + half;
    return candidate;
}

bool StructureGenerator::terrainFits(const Candidate& candidate) const {
    const int anchorHeight = m_heightPipeline.sampleColumn(
        candidate.x, candidate.z).height;
    const TypeParams& p = params(candidate.type);
    for (int z = candidate.minZ; z <= candidate.maxZ; ++z) {
        for (int x = candidate.minX; x <= candidate.maxX; ++x) {
            const SurfaceColumn column = m_heightPipeline.sampleColumn(x, z);
            if (column.river || column.height < column.waterLevel) return false;
            if (column.height <= Config::SEA_LEVEL) return false;
            if (column.height > Config::WORLD_MAX_Y - p.maxBuildHeight)
                return false;
            if (std::abs(column.height - anchorHeight) > p.tolerance)
                return false;
        }
    }
    return true;
}

bool StructureGenerator::winsOverlapSpacing(const Candidate& candidate) const {
    const int rank = candidate.type == StructureType::Village ||
                             candidate.type == StructureType::DesertVillage
                         ? 1 : 0;
    for (const StructureType otherType : OVERWORLD_STRUCTURE_TYPES) {
        const TypeParams& p = params(otherType);
        int reach = 0;
        if (otherType == candidate.type) {
            // Keep same-type anchors a comfortable distance apart.
            reach = std::max(p.cell / 2, 2 * halfSize(otherType, 0) + 16);
        } else {
            reach = maxHalfSize(candidate.type) + maxHalfSize(otherType);
        }
        const int range = static_cast<int>(std::ceil(
            static_cast<float>(reach) / static_cast<float>(p.cell))) + 1;
        for (int dz = -range; dz <= range; ++dz) {
            for (int dx = -range; dx <= range; ++dx) {
                if (otherType == candidate.type && dx == 0 && dz == 0) continue;
                const int cellX = floorDiv(candidate.x, p.cell) + dx;
                const int cellZ = floorDiv(candidate.z, p.cell) + dz;
                const Candidate other = candidateForCell(otherType, cellX, cellZ);
                if (!other.chance) continue;
                if (otherType == candidate.type) {
                    const int ddx = other.x - candidate.x;
                    const int ddz = other.z - candidate.z;
                    if (ddx * ddx + ddz * ddz < reach * reach &&
                        other.priority > candidate.priority)
                        return false;
                    continue;
                }
                const bool overlap = other.minX <= candidate.maxX &&
                                     other.maxX >= candidate.minX &&
                                     other.minZ <= candidate.maxZ &&
                                     other.maxZ >= candidate.minZ;
                if (!overlap) continue;
                const int otherRank =
                    otherType == StructureType::Village ||
                            otherType == StructureType::DesertVillage
                        ? 1 : 0;
                if (otherRank > rank ||
                    (otherRank == rank && other.priority > candidate.priority))
                    return false;
            }
        }
    }
    return true;
}

bool StructureGenerator::accept(const Candidate& candidate) const {
    if (!candidate.chance) return false;
    const SurfaceColumn anchor = m_heightPipeline.sampleColumn(
        candidate.x, candidate.z);
    if (!acceptsBiome(candidate.type, anchor.biome)) return false;
    if (!winsOverlapSpacing(candidate)) return false;
    return terrainFits(candidate);
}

void StructureGenerator::generateStructuresRegion(
    int originX, int originZ, int width, int depth,
    std::vector<StructurePlacement>& out) const {
    out.clear();
    for (const StructureType type : OVERWORLD_STRUCTURE_TYPES) {
        const TypeParams& p = params(type);
        const int minCX = floorDiv(originX - p.cell, p.cell);
        const int maxCX = floorDiv(originX + width + p.cell, p.cell);
        const int minCZ = floorDiv(originZ - p.cell, p.cell);
        const int maxCZ = floorDiv(originZ + depth + p.cell, p.cell);
        for (int cz = minCZ; cz <= maxCZ; ++cz) {
            for (int cx = minCX; cx <= maxCX; ++cx) {
                const Candidate candidate = candidateForCell(type, cx, cz);
                if (candidate.x < originX || candidate.x >= originX + width ||
                    candidate.z < originZ || candidate.z >= originZ + depth)
                    continue;
                if (!accept(candidate)) continue;
                const int anchorHeight = m_heightPipeline.sampleColumn(
                    candidate.x, candidate.z).height;
                out.push_back({candidate.x - originX, candidate.z - originZ,
                               anchorHeight, type, candidate.variant,
                               candidate.minX, candidate.maxX,
                               candidate.minZ, candidate.maxZ});
            }
        }
    }
}

std::vector<StructurePlacement> StructureGenerator::generateStructures(
    int chunkWorldX, int chunkWorldZ) const {
    std::vector<StructurePlacement> out;
    generateStructuresRegion(chunkWorldX, chunkWorldZ, Config::CHUNK_SIZE_X,
                             Config::CHUNK_SIZE_Z, out);
    return out;
}

std::optional<LocatedStructure> StructureGenerator::locateNearest(
    StructureType type, int worldX, int worldZ, int maximumDistance) const {
    if (!isOverworldStructure(type) ||
        maximumDistance < 0)
        return {};

    const TypeParams& p = params(type);
    const int originCellX = floorDiv(worldX, p.cell);
    const int originCellZ = floorDiv(worldZ, p.cell);
    const int maximumRing = maximumDistance / p.cell + 2;
    const int64_t maximumDistanceSquared =
        static_cast<int64_t>(maximumDistance) * maximumDistance;
    int64_t bestDistanceSquared = maximumDistanceSquared + 1;
    std::optional<LocatedStructure> best;

    auto inspect = [&](int offsetX, int offsetZ) {
        const Candidate candidate = candidateForCell(
            type, originCellX + offsetX, originCellZ + offsetZ);
        if (!accept(candidate)) return;
        const int64_t dx = static_cast<int64_t>(candidate.x) - worldX;
        const int64_t dz = static_cast<int64_t>(candidate.z) - worldZ;
        const int64_t distanceSquared = dx * dx + dz * dz;
        if (distanceSquared > maximumDistanceSquared) return;
        if (distanceSquared > bestDistanceSquared) return;
        if (distanceSquared == bestDistanceSquared && best &&
            (candidate.x > best->worldX ||
             (candidate.x == best->worldX && candidate.z >= best->worldZ)))
            return;
        bestDistanceSquared = distanceSquared;
        best = LocatedStructure{
            candidate.x,
            m_heightPipeline.sampleColumn(candidate.x, candidate.z).height,
            candidate.z,
            type};
    };

    for (int ring = 0; ring <= maximumRing; ++ring) {
        if (ring == 0) {
            inspect(0, 0);
        } else {
            for (int x = -ring; x <= ring; ++x) {
                inspect(x, -ring);
                inspect(x, ring);
            }
            for (int z = -ring + 1; z < ring; ++z) {
                inspect(-ring, z);
                inspect(ring, z);
            }
        }

        // Every unvisited cell is at least ring*cell+1 blocks away along one
        // axis from a point in the origin cell. Once that exceeds the current
        // best distance, the result is globally nearest, not merely the first
        // accepted ring candidate.
        if (best) {
            const int64_t unvisitedLowerBound =
                static_cast<int64_t>(ring) * p.cell + 1;
            if (unvisitedLowerBound * unvisitedLowerBound >
                bestDistanceSquared)
                break;
        }
    }
    return best;
}

bool StructureGenerator::reservationAt(int worldX, int worldZ) const {
    for (const StructureType type : OVERWORLD_STRUCTURE_TYPES) {
        const TypeParams& p = params(type);
        const int half = maxHalfSize(type);
        const int minCX = floorDiv(worldX - half, p.cell);
        const int maxCX = floorDiv(worldX + half, p.cell);
        const int minCZ = floorDiv(worldZ - half, p.cell);
        const int maxCZ = floorDiv(worldZ + half, p.cell);
        for (int cz = minCZ; cz <= maxCZ; ++cz) {
            for (int cx = minCX; cx <= maxCX; ++cx) {
                const Candidate candidate = candidateForCell(type, cx, cz);
                if (!candidate.chance) continue;
                if (worldX < candidate.minX || worldX > candidate.maxX ||
                    worldZ < candidate.minZ || worldZ > candidate.maxZ)
                    continue;
                if (!acceptsBiome(type, m_heightPipeline.sampleColumn(
                                             candidate.x, candidate.z).biome))
                    continue;
                return true;
            }
        }
    }
    return false;
}

void StructureGenerator::build(const StructurePlacement& placement,
                               const StructureWriter& write,
                               const SurfaceSampler& surfaceSampler) {
    const int centerX = (placement.minX + placement.maxX) / 2;
    const int centerZ = (placement.minZ + placement.maxZ) / 2;
    const int rotations = static_cast<int>((placement.variant >> 61) & 3u);
    const bool mirror = ((placement.variant >> 60) & 1u) != 0;
    const auto transformOffset = [&](int dx, int dz) {
        if (mirror) dx = -dx;
        for (int turn = 0; turn < rotations; ++turn) {
            const int oldX = dx;
            dx = -dz;
            dz = oldX;
        }
        return std::pair<int, int>{dx, dz};
    };
    const auto transformDirection = [&](BedDirection direction) {
        const glm::ivec3 offset = bedDirectionOffset(direction);
        const auto [dx, dz] = transformOffset(offset.x, offset.z);
        if (dz < 0) return BedDirection::North;
        if (dx > 0) return BedDirection::East;
        if (dz > 0) return BedDirection::South;
        return BedDirection::West;
    };
    const StructureWriter transformedWrite = [&](int x, int y, int z,
                                                   BlockId id) {
        const auto [dx, dz] = transformOffset(x - centerX, z - centerZ);
        ArchitecturalBlockState architecture;
        if (decodeArchitecturalBlock(id, architecture) &&
            architecture.shape == RenderShape::Stair) {
            id = stairBlock(architecture.material, architecture.half,
                            transformDirection(architecture.direction));
        } else if (isBed(id)) {
            BedPart part = BedPart::Foot;
            BedDirection direction = BedDirection::North;
            if (decodeBed(id, part, direction))
                id = bedBlock(part, transformDirection(direction));
        }
        const int worldX = centerX + dx;
        const int worldZ = centerZ + dz;
        if (surfaceSampler && y == placement.baseY && isSolid(id)) {
            const int naturalY = surfaceSampler(worldX, worldZ);
            BlockId foundation = id;
            ArchitecturalBlockState foundationState;
            if (decodeArchitecturalBlock(id, foundationState))
                foundation = architecturalBaseBlock(foundationState.material);
            for (int supportY = naturalY + 1; supportY < y; ++supportY)
                write(worldX, supportY, worldZ, foundation);
        }
        write(worldX, y, worldZ, id);
    };
    const SurfaceSampler transformedSurface = surfaceSampler
        ? SurfaceSampler([&](int x, int z) {
              const auto [dx, dz] = transformOffset(x - centerX, z - centerZ);
              return surfaceSampler(centerX + dx, centerZ + dz);
          })
        : SurfaceSampler{};
    switch (placement.type) {
        case StructureType::Village:
        case StructureType::DesertVillage:
            buildVillage(placement, transformedWrite, transformedSurface);
            break;
        case StructureType::TravelerHut:
            buildHut(placement, transformedWrite);
            break;
        case StructureType::AbandonedCamp:
            buildCamp(placement, transformedWrite);
            break;
        case StructureType::DesertWell:
            buildDesertWell(placement, transformedWrite);
            break;
        case StructureType::Igloo:
            buildIgloo(placement, transformedWrite);
            break;
        case StructureType::RuinedTower:
            buildTower(placement, transformedWrite);
            break;
        case StructureType::LumberCamp:
            buildLumberCamp(placement, transformedWrite);
            break;
        case StructureType::XiguangRuin:
            buildXiguangRuin(placement, transformedWrite);
            break;
        case StructureType::StarCrystalGeode:
            buildStarCrystalGeode(placement, transformedWrite);
            break;
        case StructureType::CloudspireTower:
            buildCloudspireTower(placement, transformedWrite);
            break;
        default:
            break;
    }
}
