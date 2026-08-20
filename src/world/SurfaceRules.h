#pragma once

#include "world/BiomeMap.h"
#include "world/TerrainArchetype.h"
#include "world/WorldGenContext.h"
#include "Config.h"

#include <cstdint>

struct SurfaceProfile {
    BlockId top = BlockId::GRASS;
    BlockId under = BlockId::DIRT;
    int depth = 3;
};

class SurfaceRules {
public:
    static int floorDiv(int value, int divisor) {
        int quotient = value / divisor;
        const int remainder = value % divisor;
        return remainder < 0 ? quotient - 1 : quotient;
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
                                  Biome biome,
                                  TerrainArchetype archetype =
                                      TerrainArchetype::ROLLING_LOWLANDS,
                                  float slope = 0.0f) {
        uint64_t h = WorldGenContext::hashPosition(
            WorldGenContext(seed).derive(0x5355524641434531ULL),
            worldX, 0, worldZ);
        SurfaceProfile result{
            getBiomeProps(biome).surfaceBlock,
            getBiomeProps(biome).subsoilBlock,
            3
        };

        switch (biome) {
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
                result.depth = biome == Biome::RED_CANYON ? 7 : 5;
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
                result = slope > 0.56f
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
                result = slope > 0.48f
                    ? SurfaceProfile{BlockId::LIMESTONE, BlockId::LIMESTONE, 5}
                    : SurfaceProfile{BlockId::MOSS, BlockId::LIMESTONE, 4};
                break;
            case Biome::VOLCANIC_HIGHLANDS:
                result = {BlockId::BASALT, BlockId::TUFF, 6};
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
        if (slope > 0.78f && archetype != TerrainArchetype::DUNE_SEA &&
            archetype != TerrainArchetype::RED_ROCK_CANYON &&
            biome != Biome::GLACIAL_PEAKS &&
            biome != Biome::VOLCANIC_HIGHLANDS &&
            biome != Biome::LIMESTONE_HIGHLANDS &&
            biome != Biome::KARST_FOREST) {
            result.top = (h & 1u) ? BlockId::GRANITE : BlockId::STONE;
            result.under = BlockId::STONE;
            result.depth = 3;
        }
        return result;
    }

    static BlockId blockAtDepth(uint64_t seed, int worldX, int worldZ,
                                int height, int depth, Biome biome,
                                TerrainArchetype archetype, float slope) {
        const SurfaceProfile surface = profile(
            seed, worldX, worldZ, biome, archetype, slope);
        if (depth == 0) return surface.top;
        if (biome == Biome::RED_CANYON) {
            const int band = (height - depth + 512) % 9;
            if (band == 0 || band == 1) return BlockId::GRANITE;
            return band < 5 ? BlockId::TERRACOTTA : BlockId::RED_SAND;
        }
        if (biome == Biome::VOLCANIC_HIGHLANDS && depth >= 3)
            return BlockId::BASALT;
        if (biome == Biome::GLACIAL_PEAKS && depth >= 4)
            return BlockId::STONE;
        return surface.under;
    }

    static BlockId decoration(uint64_t seed, int worldX, int worldZ,
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
