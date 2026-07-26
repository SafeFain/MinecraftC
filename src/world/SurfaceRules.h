#pragma once

#include "world/BiomeMap.h"
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
    static SurfaceProfile profile(uint64_t seed, int worldX, int worldZ,
                                  Biome biome) {
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
                result.top = BlockId::RED_SAND;
                result.under = BlockId::TERRACOTTA;
                result.depth = 5;
                break;
            case Biome::FOREST:
            case Biome::BIRCH_FOREST:
            case Biome::TAIGA:
                if (h % 5 == 0) result.top = BlockId::PODZOL;
                break;
            case Biome::SWAMP:
                if (h % 3 != 0) result.top = BlockId::MOSS;
                break;
            default:
                break;
        }
        return result;
    }

    static BlockId decoration(uint64_t seed, int worldX, int worldZ,
                              int height, Biome biome, bool river) {
        if (river) return BlockId::AIR;
        uint64_t h = WorldGenContext::hashPosition(
            WorldGenContext(seed).derive(0x4445434F52415445ULL),
            worldX, height, worldZ);

        if ((biome == Biome::SWAMP ||
             (height <= Config::SEA_LEVEL + 2 &&
              (biome == Biome::PLAINS || biome == Biome::FOREST))) &&
            h % 13 == 0) {
            return BlockId::REEDS;
        }

        int density = 0;
        switch (biome) {
            case Biome::MEADOW:       density = 45; break;
            case Biome::PLAINS:       density = 18; break;
            case Biome::FOREST:
            case Biome::BIRCH_FOREST: density = 14; break;
            case Biome::JUNGLE:       density = 22; break;
            case Biome::SAVANNA:      density = 8; break;
            case Biome::TAIGA:        density = 6; break;
            default:                  density = 0; break;
        }
        if (static_cast<int>(h % 100) >= density) return BlockId::AIR;
        return (biome == Biome::MEADOW && (h >> 8) % 3 == 0)
            ? BlockId::FLOWER : BlockId::TALL_GRASS;
    }
};
