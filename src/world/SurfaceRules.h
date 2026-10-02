#pragma once

#include "world/BiomeMap.h"
#include "world/BiomeBlockLogic.h"
#include "world/TerrainArchetype.h"
#include "world/WorldGenContext.h"
#include "Config.h"

#include <cmath>
#include <cstdint>

struct SurfaceProfile {
    BlockId top = BlockId::GRASS;
    BlockId under = BlockId::DIRT;
    int depth = 3;
};

struct SurfaceRuleContext {
    Biome biome = Biome::PLAINS;
    TerrainArchetype archetype = TerrainArchetype::ROLLING_LOWLANDS;
    int height = Config::SEA_LEVEL;
    int waterLevel = Config::SEA_LEVEL;
    float slope = 0.0f;
    int localRelief = 0;
    float primaryArchetypeWeight = 1.0f;
    float volcanicWeight = 0.0f;
    float craterWeight = 0.0f;
    float riverWeight = 0.0f;
    bool river = false;
    TerrainArchetype secondaryArchetype =
        TerrainArchetype::ROLLING_LOWLANDS;
    float secondaryArchetypeWeight = 0.0f;
};

class SurfaceRules {
public:
    static int floorDiv(int value, int divisor) {
        int quotient = value / divisor;
        const int remainder = value % divisor;
        return remainder < 0 ? quotient - 1 : quotient;
    }

    // Smooth world-aligned fields, including negative cells. Separate domains
    // keep material patches independent of tree/structure placement.
    static float patch(uint64_t seed, int x, int z, uint64_t domain, int scale = 16) {
        const int cx = floorDiv(x, scale), cz = floorDiv(z, scale);
        float fx = static_cast<float>(x - cx * scale) / scale;
        float fz = static_cast<float>(z - cz * scale) / scale;
        fx = fx * fx * (3.0f - 2.0f * fx);
        fz = fz * fz * (3.0f - 2.0f * fz);
        const auto value = [&](int dx, int dz) {
            return static_cast<float>(WorldGenContext::hashPosition(
                WorldGenContext(seed).derive(domain), cx + dx, 0, cz + dz) & 65535u) / 65535.0f;
        };
        const float a = value(0,0), b = value(1,0), c = value(0,1), d = value(1,1);
        return (a + (b-a)*fx) * (1.0f-fz) + (c + (d-c)*fx) * fz;
    }

    template<typename Column>
    static SurfaceRuleContext contextForColumn(const Column& c) {
        SurfaceRuleContext result{c.biome, c.archetype, c.height, c.waterLevel,
            c.slope, c.localRelief, c.primaryArchetypeWeight,
            c.volcanicWeight, c.craterWeight, c.riverWeight, c.isRiver};
        result.secondaryArchetype = c.secondaryArchetype;
        result.secondaryArchetypeWeight = c.archetypeBlend;
        return result;
    }

    static BlockId naturalLandmark(uint64_t seed, int worldX, int worldZ,
                                   Biome biome) {
        const uint64_t landmarkSeed =
            WorldGenContext(seed).derive(0x4C414E444D41524BULL);

        // Every point in a boulder cluster derives the same nearby anchor, so
        // groups cross chunk and region boundaries without request-order state.
        constexpr int boulderCell = 28;
        const int bcx = floorDiv(worldX, boulderCell);
        const int bcz = floorDiv(worldZ, boulderCell);
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int cx = bcx + dx, cz = bcz + dz;
                const uint64_t h = WorldGenContext::hashPosition(
                    landmarkSeed, cx, 0, cz);
                if (h % 11 != 0) continue;
                const int ax = cx * boulderCell + 4 +
                    static_cast<int>((h >> 8) % (boulderCell - 8));
                const int az = cz * boulderCell + 4 +
                    static_cast<int>((h >> 24) % (boulderCell - 8));
                const int ddx = worldX - ax, ddz = worldZ - az;
                const int radius = 1 + static_cast<int>((h >> 40) & 1u);
                if (ddx * ddx + ddz * ddz > radius * radius) continue;
                if (biome == Biome::VOLCANIC_HIGHLANDS) return BlockId::BASALT;
                if (biome == Biome::KARST_FOREST ||
                    biome == Biome::LIMESTONE_HIGHLANDS)
                    return BlockId::LIMESTONE;
                if (biome == Biome::ROCKY_STEPPE ||
                    biome == Biome::ALPINE_TUNDRA ||
                    biome == Biome::HILLS)
                    return BlockId::GRANITE;
            }
        }

        constexpr int logCell = 40;
        const int lcx = floorDiv(worldX, logCell);
        const int lcz = floorDiv(worldZ, logCell);
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int cx = lcx + dx, cz = lcz + dz;
                const uint64_t h = WorldGenContext::hashPosition(
                    landmarkSeed ^ 0x46414C4C454E4C4FULL, cx, 0, cz);
                if (h % 17 != 0) continue;
                const int ax = cx * logCell + 5 +
                    static_cast<int>((h >> 8) % (logCell - 10));
                const int az = cz * logCell + 5 +
                    static_cast<int>((h >> 24) % (logCell - 10));
                const int length = 3 + static_cast<int>((h >> 40) % 3);
                const bool alongX = ((h >> 48) & 1u) != 0;
                const bool onLog = alongX
                    ? worldZ == az && worldX >= ax && worldX < ax + length
                    : worldX == ax && worldZ >= az && worldZ < az + length;
                if (!onLog) continue;
                if (biome == Biome::TAIGA || biome == Biome::ALPINE_TUNDRA)
                    return BlockId::SPRUCE_WOOD;
                if (biome == Biome::BIRCH_FOREST) return BlockId::BIRCH_WOOD;
                if (biome == Biome::JUNGLE || biome == Biome::KARST_FOREST)
                    return BlockId::JUNGLE_WOOD;
                if (biome == Biome::FOREST || biome == Biome::FLOWER_FOREST ||
                    biome == Biome::LUSH_VALLEY)
                    return BlockId::WOOD;
            }
        }
        return BlockId::AIR;
    }

    static SurfaceProfile profile(uint64_t seed, int worldX, int worldZ,
                                  const SurfaceRuleContext& context) {
        uint64_t h = WorldGenContext::hashPosition(
            WorldGenContext(seed).derive(0x5355524641434531ULL),
            worldX, 0, worldZ);
        SurfaceProfile result{
            getBiomeProps(context.biome).surfaceBlock,
            getBiomeProps(context.biome).subsoilBlock,
            3
        };

        switch (context.biome) {
            case Biome::DEEP_OCEAN:
                result.top = (h % 5 == 0) ? BlockId::CLAY : BlockId::GRAVEL;
                result.under = (h % 7 == 0) ? BlockId::CLAY : BlockId::STONE;
                result.depth = 2;
                break;
            case Biome::OCEAN:
                if (h % 6 == 0) result.top = BlockId::GRAVEL;
                if (h % 11 == 0) result.top = BlockId::CLAY;
                result.under = result.top == BlockId::CLAY ? BlockId::CLAY : BlockId::SAND;
                break;
            case Biome::RIVER:
                result.top = (h % 4 == 0) ? BlockId::CLAY :
                             (h % 2 == 0) ? BlockId::GRAVEL : BlockId::SAND;
                result.under = result.top;
                result.depth = 2;
                break;
            case Biome::STONY_SHORE:
                result.top = (h % 3 == 0) ? BlockId::GRAVEL : BlockId::STONE;
                result.under = BlockId::STONE;
                result.depth = 2;
                break;
            case Biome::BADLANDS:
            case Biome::RED_CANYON:
                result.top = BlockId::RED_SAND;
                result.under = BlockId::TERRACOTTA;
                result.depth = context.biome == Biome::RED_CANYON ? 7 : 5;
                break;
            case Biome::FOREST:
            case Biome::FLOWER_FOREST:
            case Biome::BIRCH_FOREST:
            case Biome::TAIGA:
                if (h % 5 == 0) result.top = BlockId::PODZOL;
                break;
            case Biome::SWAMP:
                if (h % 3 != 0) result.top = BlockId::MOSS;
                break;
            case Biome::GLACIAL_PEAKS:
                result = {BlockId::PACKED_ICE, BlockId::PACKED_ICE, 5};
                break;
            case Biome::ALPINE_TUNDRA:
                result = context.slope > 0.56f
                    ? SurfaceProfile{BlockId::GRANITE, BlockId::STONE, 3}
                    : SurfaceProfile{BlockId::SNOW, BlockId::COARSE_DIRT, 3};
                break;
            case Biome::ROCKY_STEPPE:
            case Biome::DRY_WOODLAND:
                result = {BlockId::COARSE_DIRT, BlockId::DIRT, 3};
                break;
            case Biome::LIMESTONE_HIGHLANDS:
                result = {BlockId::LIMESTONE, BlockId::LIMESTONE, 5};
                break;
            case Biome::KARST_FOREST:
                result = context.slope > 0.48f
                    ? SurfaceProfile{BlockId::LIMESTONE, BlockId::LIMESTONE, 5}
                    : SurfaceProfile{BlockId::MOSS, BlockId::LIMESTONE, 4};
                break;
            case Biome::VOLCANIC_HIGHLANDS:
                result = {BlockId::TUFF, BlockId::TUFF, 6};
                break;
            case Biome::BLACK_SAND_COAST:
                result = {BlockId::BLACK_SAND, BlockId::BASALT, 4};
                break;
            case Biome::LUSH_VALLEY:
                result = {h % 4 == 0 ? BlockId::MUD : BlockId::MOSS,
                          BlockId::DIRT, 4};
                break;
            default:
                break;
        }

        const float materialPatch = patch(seed, worldX, worldZ, 0x42494F4D45534F49ULL);
        const bool rich = materialPatch > 0.65f;
        const bool secondary = materialPatch < 0.25f;
        switch (context.biome) {
            case Biome::FOREST: case Biome::FLOWER_FOREST:
            case Biome::BIRCH_FOREST: case Biome::TAIGA:
                if (rich) result = {BlockId::LEAF_LITTER_SOIL, BlockId::ROOTED_DIRT, 3};
                else if (secondary) result.under = BlockId::ROOTED_DIRT;
                break;
            case Biome::JUNGLE: case Biome::KARST_FOREST:
                if (context.slope < 0.48f && rich)
                    result = {BlockId::MOSS, BlockId::ROOTED_DIRT, 4};
                break;
            case Biome::DESERT:
                result.under = BlockId::SANDSTONE;
                if (!rich) result.depth = 5; // sand cap, sandstone below it
                break;
            case Biome::SAVANNA: case Biome::DRY_WOODLAND:
            case Biome::ROCKY_STEPPE:
                if (rich) result = {BlockId::DRY_GRASS_BLOCK, BlockId::COARSE_DIRT, 3};
                break;
            case Biome::SWAMP: case Biome::LUSH_VALLEY:
                if (rich) result = {BlockId::PEAT, BlockId::MUD, 4};
                if (secondary || context.height < context.waterLevel)
                    result = {BlockId::SILT, BlockId::CLAY, 3};
                break;
            case Biome::RIVER:
                if (rich) result = {BlockId::SILT, BlockId::CLAY, 3};
                break;
            case Biome::OCEAN:
                if (rich && context.height < context.waterLevel &&
                    context.height >= context.waterLevel - 18)
                    result = {BlockId::CORAL_ROCK, BlockId::LIMESTONE, 2};
                else if (secondary) result = {BlockId::SILT, BlockId::CLAY, 3};
                break;
            case Biome::DEEP_OCEAN:
                if (rich) result = {BlockId::SHALE, BlockId::SHALE, 3};
                else if (secondary) result = {BlockId::SILT, BlockId::CLAY, 3};
                break;
            case Biome::BEACH:
                if (rich) result.under = BlockId::SANDSTONE;
                break;
            case Biome::STONY_SHORE: case Biome::MOUNTAINS:
                if (rich) result = {BlockId::SHALE, BlockId::STONE, 3};
                break;
            case Biome::LIMESTONE_HIGHLANDS:
                if (rich) result = {BlockId::CALCITE, BlockId::LIMESTONE, 4};
                else if (secondary) result = {BlockId::SHALE, BlockId::LIMESTONE, 3};
                break;
            case Biome::SNOW_TUNDRA: case Biome::ALPINE_TUNDRA:
                result.under = BlockId::PERMAFROST;
                if (rich && context.slope < 0.56f) result.top = BlockId::PERMAFROST;
                break;
            case Biome::GLACIAL_PEAKS:
                if (rich) result = {BlockId::BLUE_ICE, BlockId::BLUE_ICE, 5};
                break;
            case Biome::VOLCANIC_HIGHLANDS:
                if (rich && context.volcanicWeight < 0.35f)
                    result = {BlockId::VOLCANIC_ASH, BlockId::TUFF, 3};
                break;
            case Biome::BLACK_SAND_COAST:
                if (rich) result.under = BlockId::VOLCANIC_ASH;
                break;
            default: break;
        }

        // Volcanic materials follow continuous masks and never replace the
        // surrounding biome outside the cone. Basalt is deliberately sparse:
        // steep patches, crater mottling, and narrow deterministic flow scars.
        const float materialJitter = static_cast<float>((h >> 56) & 0xFFu) /
                                     255.0f - 0.5f;
        if (context.biome == Biome::BLACK_SAND_COAST &&
            context.height <= Config::SEA_LEVEL + 6) {
            result = {BlockId::BLACK_SAND, BlockId::TUFF, 4};
        } else if (context.volcanicWeight >=
                   0.35f + materialJitter * 0.08f) {
            if (context.volcanicWeight < 0.58f) {
                const bool wetFoot = context.riverWeight > 0.16f ||
                    context.biome == Biome::FOREST ||
                    context.biome == Biome::SWAMP ||
                    context.biome == Biome::LUSH_VALLEY;
                if (wetFoot && h % 7 == 0)
                    result = {BlockId::GRASS, BlockId::COARSE_DIRT, 4};
                else if (h % 3 == 0)
                    result = {BlockId::COARSE_DIRT, BlockId::TUFF, 5};
                else
                    result = {BlockId::TUFF, BlockId::TUFF, 5};
            } else {
                const float phase = static_cast<float>((h >> 20) & 0xFFu);
                const float flow = std::sin(
                    (static_cast<float>(worldX) + phase) * 0.045f +
                    std::sin(static_cast<float>(worldZ) * 0.012f) * 1.8f);
                const bool basalt =
                    (context.slope > 0.65f && h % 100 < 48) ||
                    (flow > 0.84f && h % 3 == 0) ||
                    (context.craterWeight > 0.58f && h % 4 == 0);
                result = {basalt ? BlockId::BASALT : BlockId::TUFF,
                          BlockId::TUFF, 6};
            }
        } else if (context.slope > 0.78f &&
            context.archetype != TerrainArchetype::DUNE_SEA &&
            context.archetype != TerrainArchetype::RED_ROCK_CANYON &&
            context.biome != Biome::GLACIAL_PEAKS &&
            context.biome != Biome::LIMESTONE_HIGHLANDS &&
            context.biome != Biome::KARST_FOREST) {
            result.top = (h & 1u) ? BlockId::GRANITE : BlockId::STONE;
            result.under = BlockId::STONE;
            result.depth = 3;
        }
        // Ash is limited to volcanic ground; basalt scars keep their identity.
        if (rich && context.volcanicWeight >= 0.35f &&
            result.top == BlockId::TUFF && context.slope < 0.65f &&
            context.craterWeight < 0.58f) result.top = BlockId::VOLCANIC_ASH;
        if (context.biome == Biome::BLACK_SAND_COAST && rich)
            result.under = BlockId::VOLCANIC_ASH;
        if (context.biome == Biome::MOUNTAINS && rich &&
            result.top != BlockId::TUFF && result.top != BlockId::BASALT)
            result.top = BlockId::SHALE;

        // v16 materials occupy continuous patches, retaining each biome's base.
        const float ecology = patch(seed,worldX,worldZ,0x45434F3136534F49ULL);
        if (ecology > 0.64f && context.volcanicWeight < 0.35f) {
            switch (context.biome) {
                case Biome::MOUNTAINS: case Biome::STONY_SHORE:
                case Biome::ROCKY_STEPPE:
                    result = {ecology > 0.78f ? BlockId::DIORITE : BlockId::GNEISS,
                              BlockId::ANDESITE,3}; break;
                case Biome::HILLS:
                    result.under = BlockId::ANDESITE; break;
                case Biome::LIMESTONE_HIGHLANDS: case Biome::KARST_FOREST:
                    result = {BlockId::MARBLE,BlockId::LIMESTONE,3}; break;
                case Biome::JUNGLE: case Biome::SAVANNA: case Biome::DRY_WOODLAND:
                    result = {BlockId::LATERITE,BlockId::RED_CLAY,3}; break;
                case Biome::RED_CANYON: case Biome::BADLANDS:
                    result.top = BlockId::RED_CLAY; break;
                case Biome::SWAMP: case Biome::RIVER: case Biome::LUSH_VALLEY:
                    if (context.height >= context.waterLevel+2 && context.slope < 0.25f)
                        result = {BlockId::CRACKED_MUD,BlockId::CLAY,2};
                    break;
                case Biome::DESERT:
                    if (context.slope < 0.18f)
                        result = {BlockId::SALT_CRUST,BlockId::SANDSTONE,2};
                    break;
                default: break;
            }
        }
        if (context.biome == Biome::VOLCANIC_HIGHLANDS && ecology > 0.68f &&
            context.volcanicWeight < 0.58f && context.slope < 0.55f)
            result.under = BlockId::ANDESITE;

        // One final surface authority for generated chunks and procedural LOD.
        // Preserve exposed ice and cold bare patches rather than painting every
        // glacial column with opaque snow.
        const int snowLine = getBiomeProps(context.biome).snowLine;
        if (context.height >= snowLine && snowLine < Config::SNOW_LINE_DISABLED &&
            result.top != BlockId::PACKED_ICE && result.top != BlockId::BLUE_ICE &&
            result.top != BlockId::PERMAFROST &&
            !(context.biome == Biome::ALPINE_TUNDRA && context.slope > 0.56f))
            result.top = BlockId::SNOW;
        return result;
    }

    static BlockId blockAtDepth(uint64_t seed, int worldX, int worldZ,
                                int depth,
                                const SurfaceRuleContext& context) {
        const SurfaceProfile surface = profile(
            seed, worldX, worldZ, context);
        if (depth == 0) return surface.top;
        if (context.biome == Biome::DESERT)
            return depth >= 3 ? BlockId::SANDSTONE : BlockId::SAND;
        if (context.biome == Biome::BADLANDS || context.biome == Biome::RED_CANYON) {
            const int band = ((context.height - depth) % 12 + 12) % 12;
            if (band <= 1) return BlockId::WHITE_TERRACOTTA;
            if (band <= 4) return BlockId::OCHRE_TERRACOTTA;
            if (band <= 6) return BlockId::RED_SANDSTONE;
            return BlockId::TERRACOTTA;
        }
        if (context.volcanicWeight >= 0.35f && depth >= 2)
            return BlockId::TUFF;
        if (context.biome == Biome::GLACIAL_PEAKS && depth >= 4)
            return BlockId::STONE;
        return surface.under;
    }

    static BlockId legacyDecoration(uint64_t seed, int worldX, int worldZ,
                              int height, Biome biome, bool river) {
        if (river) return BlockId::AIR;
        uint64_t h = WorldGenContext::hashPosition(
            WorldGenContext(seed).derive(0x4445434F52415445ULL),
            worldX, height, worldZ);

        const BlockId landmark = naturalLandmark(seed, worldX, worldZ, biome);
        if (landmark != BlockId::AIR) return landmark;

        if (biome == Biome::VOLCANIC_HIGHLANDS && h % 89 == 0)
            return BlockId::BASALT;
        if ((biome == Biome::LIMESTONE_HIGHLANDS ||
             biome == Biome::KARST_FOREST) && h % 97 == 0)
            return BlockId::LIMESTONE;
        if ((biome == Biome::ROCKY_STEPPE || biome == Biome::ALPINE_TUNDRA) &&
            h % 101 == 0)
            return BlockId::GRANITE;

        if ((biome == Biome::SWAMP ||
             (height <= Config::SEA_LEVEL + 2 &&
              (biome == Biome::PLAINS || biome == Biome::FOREST))) &&
            h % 13 == 0) {
            return BlockId::REEDS;
        }

        const int density = getBiomeProps(biome).decorationDensity;
        if (static_cast<int>(h % 100) >= density) return BlockId::AIR;
        const uint64_t choice = (h >> 8) % 12;
        if (biome == Biome::SUNFLOWER_PLAINS && choice < 7)
            return BlockId::SUNFLOWER_BOTTOM;
        if (biome == Biome::FLOWER_FOREST || biome == Biome::MEADOW ||
            biome == Biome::SUNFLOWER_PLAINS) {
            switch (choice % 6) {
                case 0: return BlockId::FLOWER;
                case 1: return BlockId::DANDELION;
                case 2: return BlockId::BLUE_ORCHID;
                case 3: return BlockId::ALLIUM;
                case 4: return BlockId::OXEYE_DAISY;
                default: return BlockId::TALL_GRASS;
            }
        }
        return BlockId::TALL_GRASS;
    }

    static BlockId decoration(uint64_t seed, int x, int z,
                              const SurfaceRuleContext& context, BlockId soil) {
        if (context.height < context.waterLevel) return BlockId::AIR;
        const uint64_t h = WorldGenContext::hashPosition(
            WorldGenContext(seed).derive(0x42494F4D45504C41ULL), x, context.height, z);
        const float cluster = patch(seed,x,z,0x504C414E54504154ULL,12);
        const int density = cluster > 0.60f ? 22 : 6;
        BlockId plant = BlockId::AIR;
        if (static_cast<int>(h % 100) < density) {
            switch (context.biome) {
                case Biome::FOREST: case Biome::FLOWER_FOREST:
                case Biome::BIRCH_FOREST: case Biome::TAIGA:
                    plant = h % 4 < 2 ? BlockId::FERN :
                        h % 4 == 2 ? BlockId::BROWN_MUSHROOM : BlockId::RED_MUSHROOM;
                    break;
                case Biome::JUNGLE: case Biome::KARST_FOREST:
                    plant = h % 3 == 0 ? BlockId::TROPICAL_FLOWER :
                        h % 3 == 1 ? BlockId::FERN : BlockId::BROWN_MUSHROOM;
                    break;
                case Biome::PLAINS: case Biome::MEADOW:
                case Biome::SUNFLOWER_PLAINS: case Biome::HILLS:
                    plant = h % 2 ? BlockId::LAVENDER : BlockId::BELLFLOWER;
                    break;
                case Biome::DESERT: case Biome::BADLANDS: case Biome::RED_CANYON:
                    plant = BlockId::DEAD_BUSH; break;
                case Biome::SAVANNA: case Biome::DRY_WOODLAND: case Biome::ROCKY_STEPPE:
                    plant = h % 4 == 0 ? BlockId::DEAD_BUSH : BlockId::DRY_GRASS; break;
                case Biome::SWAMP: case Biome::LUSH_VALLEY: case Biome::RIVER:
                    plant = h % 3 == 0 ? BlockId::CATTAIL :
                        h % 3 == 1 ? BlockId::FERN : BlockId::BROWN_MUSHROOM; break;
                case Biome::SNOW_TUNDRA: case Biome::ALPINE_TUNDRA:
                    plant = BlockId::ALPINE_FLOWER; break;
                case Biome::BEACH: case Biome::STONY_SHORE: case Biome::BLACK_SAND_COAST:
                    plant = BlockId::BEACH_GRASS; break;
                default: break;
            }
        }
        if ((h >> 24) % 100u < static_cast<unsigned>(density)) {
            switch (context.biome) {
                case Biome::PLAINS: case Biome::MEADOW: case Biome::SUNFLOWER_PLAINS:
                case Biome::HILLS: plant = BlockId::CLOVER; break;
                case Biome::FOREST: case Biome::BIRCH_FOREST: case Biome::FLOWER_FOREST:
                    plant = (h >> 32)%2 ? BlockId::NETTLE : BlockId::FALLEN_TWIGS; break;
                case Biome::TAIGA: case Biome::DRY_WOODLAND:
                    plant = (h >> 32)%2 ? BlockId::HEATHER : BlockId::FALLEN_TWIGS; break;
                case Biome::JUNGLE: case Biome::KARST_FOREST:
                    plant = BlockId::JUNGLE_FERN; break;
                case Biome::DESERT: case Biome::BADLANDS: case Biome::SAVANNA:
                case Biome::RED_CANYON:
                    plant = (h >> 32)%2 ? BlockId::DESERT_FLOWER : BlockId::SMALL_CACTUS; break;
                case Biome::SWAMP: case Biome::LUSH_VALLEY: case Biome::RIVER:
                    plant = (h >> 32)%2 ? BlockId::REED_FLOWER : BlockId::WILD_MINT; break;
                case Biome::SNOW_TUNDRA: case Biome::ALPINE_TUNDRA:
                    plant = (h >> 32)%2 ? BlockId::TUNDRA_MOSS : BlockId::HEATHER; break;
                default: break;
            }
        }
        if (plant != BlockId::AIR && supportsBiomePlant(plant,soil)) return plant;
        const BlockId fallback = legacyDecoration(seed,x,z,context.height,context.biome,context.river);
        // Existing landmarks remain valid on exposed rock. Existing flowers
        // and grass must not grow on ice, bare rock or deep sediment.
        if (fallback == BlockId::TALL_GRASS || isFlower(fallback))
            return supportsBiomePlant(BlockId::FERN,soil) ? fallback : BlockId::AIR;
        return fallback;
    }

    struct RubbleColumn { BlockId material = BlockId::AIR; int height = 0; };

    template<typename Sample, typename Reserved>
    static RubbleColumn rubbleColumn(uint64_t seed, int x, int z,
                                     Sample&& sample, Reserved&& reserved) {
        const int cellX = floorDiv(x,16), cellZ = floorDiv(z,16);
        for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
            const int cx = cellX+dx, cz = cellZ+dz;
            const uint64_t hash = WorldGenContext::hashPosition(
                WorldGenContext(seed).derive(0x45434F3136525542ULL),cx,0,cz);
            if (hash%100 >= 20) continue;
            const int ax = cx*16+static_cast<int>((hash>>8)%16);
            const int az = cz*16+static_cast<int>((hash>>24)%16);
            if (std::abs(x-ax)>1 || std::abs(z-az)>1) continue;
            const auto anchor = sample(ax,az);
            BlockId material = BlockId::AIR;
            switch (anchor.biome) {
                case Biome::MOUNTAINS: case Biome::STONY_SHORE:
                    material = (hash>>40)%2 ? BlockId::DIORITE : BlockId::ANDESITE; break;
                case Biome::ROCKY_STEPPE: material = BlockId::GNEISS; break;
                case Biome::LIMESTONE_HIGHLANDS: material = BlockId::MARBLE; break;
                case Biome::VOLCANIC_HIGHLANDS: material = BlockId::ANDESITE; break;
                default: continue;
            }
            bool valid = true;
            for (int rz = -1; rz <= 1; ++rz) for (int rx = -1; rx <= 1; ++rx) {
                const auto c = sample(ax+rx,az+rz);
                valid = valid && c.biome == anchor.biome && c.slope < 0.3f &&
                    c.height >= c.waterLevel && std::abs(c.height-anchor.height)<=1 &&
                    !reserved(ax+rx,az+rz);
            }
            if (!valid) continue;
            // A cross footprint uses at most five columns; the center is tallest.
            const int distance = std::abs(x-ax)+std::abs(z-az);
            if (distance <= 1) return {material,distance == 0 ? 2 : 1};
        }
        return {};
    }

    template<typename GetBlock, typename SetBlock>
    static bool placeRubble(const RubbleColumn& rubble, int y,
                            GetBlock&& get, SetBlock&& set) {
        if (!rubble.height || !isFullCollisionBlock(get(y))) return false;
        for (int dy=1; dy<=rubble.height; ++dy)
            if (!Config::isValidWorldY(y+dy) || get(y+dy)!=BlockId::AIR) return false;
        for (int dy=1; dy<=rubble.height; ++dy) set(y+dy,rubble.material);
        return true;
    }

    template<typename GetBlock, typename SetBlock, typename NearWater>
    static void decorateColumn(uint64_t seed, int x, int z,
                               const SurfaceRuleContext& context,
                               GetBlock&& get, SetBlock&& set, NearWater&& nearWater) {
        const int y = context.height;
        if (!Config::isValidWorldY(y+1) || get(y+1) != BlockId::AIR ||
            !isFullCollisionBlock(get(y))) return;
        const BlockId block = decoration(seed,x,z,context,get(y));
        if (block == BlockId::AIR) return;
        if ((block == BlockId::CATTAIL || block == BlockId::REEDS ||
             block == BlockId::REED_FLOWER || block == BlockId::WILD_MINT) && !nearWater()) return;
        const int count = decorationHeight(seed,x,z,y,block);
        for (int dy = 1; dy <= count; ++dy) {
            if (!Config::isValidWorldY(y+dy) || get(y+dy) != BlockId::AIR) return;
        }
        if (block == BlockId::SUNFLOWER_BOTTOM &&
            (!Config::isValidWorldY(y+2) || get(y+2) != BlockId::AIR)) return;
        for (int dy = 1; dy <= count; ++dy) set(y+dy,block);
        if (block == BlockId::SUNFLOWER_BOTTOM) set(y+2,BlockId::SUNFLOWER_TOP);
    }

    static int decorationHeight(uint64_t seed, int worldX, int worldZ,
                                int height, BlockId decoration) {
        if (decoration != BlockId::BASALT && decoration != BlockId::LIMESTONE &&
            decoration != BlockId::GRANITE)
            return 1;
        const uint64_t h = WorldGenContext::hashPosition(
            WorldGenContext(seed).derive(0x4E41545552414C46ULL),
            worldX, height, worldZ);
        return 1 + static_cast<int>(h % 3);
    }
};
