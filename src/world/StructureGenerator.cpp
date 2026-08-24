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
              int x1, int z1, int base, BlockId path) {
    int x = x0;
    int z = z0;
    const int stepX = x1 > x0 ? 1 : -1;
    while (x != x1) {
        x += stepX;
        write(x, base, z, path);
    }
    const int stepZ = z1 > z0 ? 1 : -1;
    while (z != z1) {
        z += stepZ;
        write(x, base, z, path);
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
                const VillageStyle& style) {
    // Foundation ring replaces the grass/sand surface.
    fillRing(write, hx - 2, base, hz - 2, hx + 2, hz + 2, style.foundation);
    fillBox(write, hx - 1, base, hz - 1, hx + 1, base, hz + 1, style.floor);
    // Corner pillars and three-high walls.
    for (int y = base + 1; y <= base + 3; ++y) {
        for (int z = hz - 2; z <= hz + 2; ++z) {
            for (int x = hx - 2; x <= hx + 2; ++x) {
                if (x > hx - 2 && x < hx + 2 && z > hz - 2 && z < hz + 2)
                    continue;
                const bool corner = (x == hx - 2 || x == hx + 2) &&
                                    (z == hz - 2 || z == hz + 2);
                write(x, y, z, corner ? style.pillar : style.wall);
            }
        }
    }
    // Door opening faces the village center.
    const int doorX = hx + doorDx * 2;
    const int doorZ = hz + doorDz * 2;
    write(doorX, base + 1, doorZ, BlockId::AIR);
    write(doorX, base + 2, doorZ, BlockId::AIR);
    // Glass windows on the walls perpendicular to the door.
    if (doorDx != 0) {
        write(hx, base + 2, hz - 2, BlockId::GLASS);
        write(hx, base + 2, hz + 2, BlockId::GLASS);
    } else {
        write(hx - 2, base + 2, hz, BlockId::GLASS);
        write(hx + 2, base + 2, hz, BlockId::GLASS);
    }
    // Stepped plank roof.
    fillBox(write, hx - 2, base + 4, hz - 2, hx + 2, base + 4, hz + 2,
            style.roof);
    fillBox(write, hx - 1, base + 5, hz - 1, hx + 1, base + 5, hz + 1,
            style.roof);

    const uint64_t furnishing =
        WorldGenContext::hashPosition(variant, hx, 1, hz) % 6;
    switch (furnishing) {
        case 0: {
            const BlockId foot = bedBlock(BedPart::Foot, BedDirection::North);
            const BlockId head = bedBlock(BedPart::Head, BedDirection::North);
            write(hx - 1, base + 1, hz, foot);
            write(hx - 1, base + 1, hz - 1, head);
            break;
        }
        case 1: write(hx, base + 1, hz, BlockId::CRAFTING_TABLE); break;
        case 2: write(hx, base + 1, hz, BlockId::FURNACE); break;
        case 3: write(hx, base + 1, hz, BlockId::CHEST); break;
        case 4: write(hx + 1, base + 2, hz, BlockId::TORCH); break;
        default: break;
    }
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
                  const StructureGenerator::StructureWriter& write) {
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
        {-13, -9}, {13, -9}, {-9, 13}, {9, 13},
        {-14, 4}, {14, 4}, {0, -14}, {0, 14},
    }};
    const int houseCount = 4 + static_cast<int>(hash(cx, 1, cz) % 3);
    for (int i = 0; i < 8 && i < houseCount; ++i) {
        const int hx = cx + kHouseOffsets[static_cast<size_t>(i)].first;
        const int hz = cz + kHouseOffsets[static_cast<size_t>(i)].second;
        int doorDx = cx - hx;
        int doorDz = cz - hz;
        if (std::abs(doorDx) >= std::abs(doorDz)) {
            doorDx = doorDx > 0 ? 1 : -1;
            doorDz = 0;
        } else {
            doorDz = doorDz > 0 ? 1 : -1;
            doorDx = 0;
        }
        drawRoad(write, cx, cz, hx, hz, base, style.path);
        buildHouse(write, hx, hz, base, hash(cx, 2, hz), doorDx, doorDz, style);
    }

    // One corner holds a farm (plains) or a cactus pen (desert).
    const uint64_t corner = hash(cx, 3, cz);
    if (desert)
        buildCactusPen(write, cx - 14, cz + 14, base, corner);
    else
        buildFarm(write, cx - 14, cz + 14, base, corner);
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
    fillBox(write, cx - 3, base + 4, cz - 3, cx + 3, base + 4, cz + 3,
            BlockId::PLANKS);
    fillBox(write, cx - 2, base + 5, cz - 2, cx + 2, base + 5, cz + 2,
            BlockId::PLANKS);
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
        }
    }
    fillRing(write, cx - 2, base + 1, cz - 2, cx + 2, cz + 2,
             BlockId::TERRACOTTA);
    for (const int dx : {-2, 2}) {
        for (const int dz : {-2, 2}) {
            write(cx + dx, base + 2, cz + dz, BlockId::TERRACOTTA);
            write(cx + dx, base + 3, cz + dz, BlockId::TERRACOTTA);
        }
    }
    fillRing(write, cx - 2, base + 4, cz - 2, cx + 2, cz + 2,
             BlockId::TERRACOTTA);
    write(cx - 2, base + 2, cz, BlockId::SAND);
    write(cx + 2, base + 2, cz, BlockId::SAND);
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
            if (dist2 >= 9) {
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
}

void buildTower(const StructurePlacement& placement,
                const StructureGenerator::StructureWriter& write) {
    const int cx = (placement.minX + placement.maxX) / 2;
    const int cz = (placement.minZ + placement.maxZ) / 2;
    const int base = placement.baseY;
    const uint64_t variant = placement.variant;
    const int height = 5 + static_cast<int>(
        WorldGenContext::hashPosition(variant, cx, 0, cz) % 4);
    fillBox(write, cx - 2, base, cz - 2, cx + 2, base, cz + 2,
            BlockId::COARSE_DIRT);
    for (int y = base + 1; y <= base + height; ++y) {
        for (int dz = -2; dz <= 2; ++dz) {
            for (int dx = -2; dx <= 2; ++dx) {
                if (dx > -2 && dx < 2 && dz > -2 && dz < 2) continue;
                const uint64_t h = WorldGenContext::hashPosition(
                    variant, cx + dx, y, cz + dz);
                // Jagged top edge and a random wall gap on the lower course.
                if (y == base + height && h % 4 == 0) continue;
                if (y == base + 2 && h % 13 == 0) continue;
                write(cx + dx, y, cz + dz,
                      h % 11 == 0 ? BlockId::MOSS : BlockId::COBBLESTONE);
            }
        }
    }
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
    static const TypeParams table[] = {
        {StructureType::None, 0, 0, 0, 0},
        {StructureType::Village, 512, 12, 6, 8},
        {StructureType::DesertVillage, 512, 12, 6, 8},
        {StructureType::TravelerHut, 64, 10, 2, 7},
        {StructureType::AbandonedCamp, 80, 9, 2, 4},
        {StructureType::DesertWell, 64, 12, 2, 6},
        {StructureType::Igloo, 80, 12, 2, 6},
        {StructureType::RuinedTower, 128, 10, 3, 10},
        {StructureType::LumberCamp, 96, 10, 2, 5},
    };
    const int index = static_cast<int>(type);
    return table[index >= 0 && index < static_cast<int>(StructureType::Count)
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
    switch (type) {
        case StructureType::Village:
            return 14 + static_cast<int>((variant >> 8) % 5);
        case StructureType::DesertVillage:
            return 14 + static_cast<int>((variant >> 8) % 3);
        case StructureType::TravelerHut:
        case StructureType::Igloo:
            return 3;
        case StructureType::AbandonedCamp:
        case StructureType::RuinedTower:
        case StructureType::LumberCamp:
            return 4;
        case StructureType::DesertWell:
            return 2;
        default:
            return 3;
    }
}

int StructureGenerator::maxHalfSize(StructureType type) {
    switch (type) {
        case StructureType::Village: return 18;
        case StructureType::DesertVillage: return 16;
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
    for (int t = 1; t < static_cast<int>(StructureType::Count); ++t) {
        const StructureType otherType = static_cast<StructureType>(t);
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
    std::vector<StructurePlacement>& out) {
    out.clear();
    for (int t = 1; t < static_cast<int>(StructureType::Count); ++t) {
        const StructureType type = static_cast<StructureType>(t);
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
    int chunkWorldX, int chunkWorldZ) {
    std::vector<StructurePlacement> out;
    generateStructuresRegion(chunkWorldX, chunkWorldZ, Config::CHUNK_SIZE_X,
                             Config::CHUNK_SIZE_Z, out);
    return out;
}

std::optional<LocatedStructure> StructureGenerator::locateNearest(
    StructureType type, int worldX, int worldZ, int maximumDistance) const {
    if (type == StructureType::None || type == StructureType::Count ||
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
    for (int t = 1; t < static_cast<int>(StructureType::Count); ++t) {
        const StructureType type = static_cast<StructureType>(t);
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
                               const StructureWriter& write) {
    switch (placement.type) {
        case StructureType::Village:
        case StructureType::DesertVillage:
            buildVillage(placement, write);
            break;
        case StructureType::TravelerHut:
            buildHut(placement, write);
            break;
        case StructureType::AbandonedCamp:
            buildCamp(placement, write);
            break;
        case StructureType::DesertWell:
            buildDesertWell(placement, write);
            break;
        case StructureType::Igloo:
            buildIgloo(placement, write);
            break;
        case StructureType::RuinedTower:
            buildTower(placement, write);
            break;
        case StructureType::LumberCamp:
            buildLumberCamp(placement, write);
            break;
        default:
            break;
    }
}
