#include "world/CaveGenerator.h"
#include "world/Noise.h"
#include "world/WorldGenContext.h"
#include "Config.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::vector<CaveColumnInfo> columns(int width, int depth, int surface = 80,
                                    bool submerged = false) {
    return std::vector<CaveColumnInfo>(static_cast<size_t>(width) * depth,
                                      {surface, Config::SEA_LEVEL, submerged});
}
}

int main() {
    static_assert(WorldGenContext::GENERATION_VERSION == 14);
    static_assert(WorldGenContext::CHUNK_CACHE_VERSION == (14u << 16 | 1u));
    Noise noise(1234567890ULL);
    CaveGenerator caves(noise, 1234567890ULL);
    auto large = caves.generateVolume(-48, -48, 96, 96, columns(96, 96));
    auto repeat = caves.generateVolume(-48, -48, 96, 96, columns(96, 96));

    size_t carved = 0, eligible = 0, water = 0, lava = 0;
    size_t caveBiomes[5] = {};
    for (int z = -48; z < 48; ++z) {
        for (int x = -48; x < 48; ++x) {
            for (int y = Config::CAVE_MIN_Y; y <= 74; ++y) {
                CaveCell a = large.get(x, y, z);
                require(a == repeat.get(x, y, z), "same seed is not deterministic");
                require(large.biome(x, y, z) == repeat.biome(x, y, z),
                        "same-seed cave biome cache is not deterministic");
                ++eligible;
                if (a != CaveCell::Solid) {
                    ++carved;
                    ++caveBiomes[static_cast<size_t>(large.biome(x, y, z))];
                }
                if (a == CaveCell::Water) ++water;
                if (a == CaveCell::Lava) ++lava;
                require(y > Config::CAVE_LAVA_LEVEL || a != CaveCell::Water,
                        "water generated in lava band");
                require(y <= Config::CAVE_LAVA_LEVEL || a != CaveCell::Lava,
                        "lava generated above lava band");
            }
            for (int y = Config::WORLD_MIN_Y; y < Config::CAVE_MIN_Y; ++y)
                require(large.get(x, y, z) == CaveCell::Solid, "bedrock band was carved");
        }
    }
    double ratio = static_cast<double>(carved) / static_cast<double>(eligible);
    require(ratio >= 0.08 && ratio <= 0.20, "balanced cave density outside 8-20%");
    require(caveBiomes[0] + caveBiomes[1] + caveBiomes[2] + caveBiomes[3] +
                caveBiomes[4] == carved,
            "carved cells did not receive exactly one cached cave biome");
    const double neutralRatio = static_cast<double>(caveBiomes[0]) /
                                static_cast<double>(carved);
    if (neutralRatio < 0.35 || neutralRatio > 0.45)
        std::cerr << "neutral cave ratio=" << neutralRatio << '\n';
    require(neutralRatio >= 0.35 && neutralRatio <= 0.45,
            "neutral transition caves are outside the 35-45% target");
    if (water == 0 || lava == 0)
        std::cerr << "liquid counts: water=" << water << " lava=" << lava << '\n';
    require(water > 0 && lava > 0, "expected both aquifer water and cave lava");

    auto chunk = caves.generateVolume(-16, -16, 16, 16, columns(16, 16));
    for (int z = -16; z < 0; ++z) for (int x = -16; x < 0; ++x)
        for (int y = Config::WORLD_MIN_Y; y < Config::WORLD_MAX_Y; ++y)
            require(chunk.get(x, y, z) == large.get(x, y, z),
                    "overlapping requests disagree at a boundary");
    for (int z = -16; z < 0; ++z) for (int x = -16; x < 0; ++x)
        for (int y = Config::WORLD_MIN_Y; y < Config::WORLD_MAX_Y; ++y)
            require(chunk.biome(x, y, z) == large.biome(x, y, z),
                    "overlapping cave-biome requests disagree at a boundary");

    bool sawVerdant = false, sawKarst = false, sawCrystal = false,
         sawVolcanic = false, sawNeutral = false;
    for (int z = -512; z <= 512; z += 24) {
        for (int x = -512; x <= 512; x += 24) {
            for (int y : {-48, -24, 0, 32, 72, 112}) {
                const CaveBiome biome = caves.biomeAt(x, y, z);
                sawVerdant |= biome == CaveBiome::VerdantGrotto;
                sawKarst |= biome == CaveBiome::DripstoneKarst;
                sawCrystal |= biome == CaveBiome::CrystalHollow;
                sawVolcanic |= biome == CaveBiome::VolcanicDepths;
                sawNeutral |= biome == CaveBiome::Neutral;
                require(y >= -16 || biome != CaveBiome::VerdantGrotto,
                        "verdant grotto escaped its depth mask");
                require(y <= 32 || biome != CaveBiome::CrystalHollow,
                        "crystal hollow escaped its depth mask");
                require(y <= -24 || biome != CaveBiome::VolcanicDepths,
                        "volcanic depths escaped its depth mask");
                require((y >= -32 && y <= 112) ||
                            biome != CaveBiome::DripstoneKarst,
                        "dripstone karst escaped its depth mask");
            }
        }
    }
    require(sawVerdant && sawKarst && sawCrystal && sawVolcanic && sawNeutral,
            "world-coordinate classifier did not expose every cave biome");

    auto wet = caves.generateVolume(0, 0, 16, 16, columns(16, 16, 62, true));
    for (int z = 0; z < 16; ++z) for (int x = 0; x < 16; ++x)
        for (int y = 58; y < Config::WORLD_MAX_Y; ++y)
            require(wet.get(x, y, z) == CaveCell::Solid, "submerged roof was breached");

    Noise otherNoise(987654321ULL);
    CaveGenerator other(otherNoise, 987654321ULL);
    auto changed = other.generateVolume(-16, -16, 16, 16, columns(16, 16));
    bool differs = false;
    for (int z = -16; z < 0 && !differs; ++z) for (int x = -16; x < 0 && !differs; ++x)
        for (int y = Config::CAVE_MIN_Y; y <= 74; ++y)
            if (changed.get(x, y, z) != chunk.get(x, y, z)) { differs = true; break; }
    require(differs, "different seeds produced identical caves");

    std::cout << "cave density=" << ratio << " neutral=" << neutralRatio
              << " carved=" << carved
              << " water=" << water << " lava=" << lava << '\n';
}
