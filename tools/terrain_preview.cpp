#include "world/HeightPipeline.h"
#include "world/Noise.h"
#include "Config.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
using Color = std::array<uint8_t, 3>;

Color archetypeColor(TerrainArchetype type) {
    static constexpr Color colors[TERRAIN_ARCHETYPE_COUNT] = {
        Color{10,35,85}, {38,105,130}, {115,120,115}, {105,175,75},
        {55,135,100}, {45,105,65}, {45,115,45}, {155,135,85},
        {220,190,105}, {170,70,40}, {190,195,160}, {130,130,145},
        {190,225,240}, {65,55,55}, {115,105,100}
    };
    return colors[static_cast<uint8_t>(type)];
}

void writePpm(const std::string& path, int width, int height,
              const std::vector<Color>& pixels) {
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << width << ' ' << height << "\n255\n";
    for (const Color& pixel : pixels)
        out.write(reinterpret_cast<const char*>(pixel.data()), 3);
}
}

int main(int argc, char** argv) {
    const uint64_t seed = argc > 1 ? std::stoull(argv[1]) : 1234567890ULL;
    const int originX = argc > 2 ? std::stoi(argv[2]) : -2048;
    const int originZ = argc > 3 ? std::stoi(argv[3]) : -2048;
    const int size = argc > 4 ? std::max(16, std::stoi(argv[4])) : 512;
    const int step = argc > 5 ? std::max(1, std::stoi(argv[5])) : 8;
    const std::string prefix = argc > 6 ? argv[6] : "terrain-preview";

    Noise legacy(seed);
    HeightPipeline terrain(legacy, seed);
    std::vector<Color> heightPixels(static_cast<size_t>(size) * size);
    std::vector<Color> archetypePixels(heightPixels.size());
    std::vector<Color> riverPixels(heightPixels.size());
    std::vector<Color> biomePixels(heightPixels.size());
    std::vector<Color> slopePixels(heightPixels.size());
    std::vector<Color> sectionPixels(
        static_cast<size_t>(size) * Config::WORLD_HEIGHT,
        Color{190, 220, 245});
    std::ofstream crossSection(prefix + "-section.csv");
    crossSection << "world_x,height,water,river,biome,archetype,slope\n";

    for (int py = 0; py < size; ++py) {
        for (int px = 0; px < size; ++px) {
            const int wx = originX + px * step;
            const int wz = originZ + py * step;
            const SurfaceColumn column = terrain.sampleColumn(wx, wz);
            const size_t index = static_cast<size_t>(py) * size + px;
            const float normalized = std::clamp(
                static_cast<float>(column.height - Config::WORLD_MIN_Y) /
                    Config::WORLD_HEIGHT, 0.0f, 1.0f);
            const uint8_t shade = static_cast<uint8_t>(normalized * 255.0f);
            heightPixels[index] = {shade, shade, shade};
            archetypePixels[index] = archetypeColor(column.archetype);
            const uint8_t biome = static_cast<uint8_t>(column.biome);
            biomePixels[index] = {
                static_cast<uint8_t>(45 + (biome * 67) % 190),
                static_cast<uint8_t>(45 + (biome * 97) % 190),
                static_cast<uint8_t>(45 + (biome * 131) % 190)};
            const uint8_t slope = static_cast<uint8_t>(
                std::clamp(column.slope, 0.0f, 1.0f) * 255.0f);
            slopePixels[index] = {slope, slope, slope};
            riverPixels[index] = column.river
                ? Color{30, 120, 240}
                : archetypePixels[index];
            if (py == size / 2) {
                crossSection << wx << ',' << column.height << ','
                    << column.waterLevel << ',' << column.river << ','
                    << static_cast<int>(column.biome) << ','
                    << terrainArchetypeName(column.archetype) << ','
                    << column.slope << '\n';
                for (int worldY = Config::WORLD_MIN_Y;
                     worldY < Config::WORLD_MAX_Y; ++worldY) {
                    const int imageY = Config::WORLD_MAX_Y - 1 - worldY;
                    Color color{190, 220, 245};
                    if (worldY <= column.waterLevel && worldY > column.height)
                        color = {35, 105, 195};
                    if (terrain.isTerrainSolid(wx, worldY, wz, column)) {
                        color = archetypeColor(column.archetype);
                        if (worldY < column.height - 5)
                            color = {85, 82, 78};
                    }
                    sectionPixels[static_cast<size_t>(imageY) * size + px] = color;
                }
            }
        }
    }
    writePpm(prefix + "-height.ppm", size, size, heightPixels);
    writePpm(prefix + "-archetypes.ppm", size, size, archetypePixels);
    writePpm(prefix + "-rivers.ppm", size, size, riverPixels);
    writePpm(prefix + "-biomes.ppm", size, size, biomePixels);
    writePpm(prefix + "-slope.ppm", size, size, slopePixels);
    writePpm(prefix + "-section.ppm", size, Config::WORLD_HEIGHT,
             sectionPixels);
    std::cout << "wrote " << prefix
              << "-{height,slope,biomes,archetypes,rivers,section}.ppm and section.csv\n";
}
