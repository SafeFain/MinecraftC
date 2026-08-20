#include "world/Chunk.h"
#include "world/RegionGenerator.h"
#include "world/WorldGenerator.h"
#include "Config.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

int main(int argc, char** argv) {
    const uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 10)
                                   : 0x5EED1234ULL;
    const int runs = argc > 2 ? std::max(3, std::atoi(argv[2])) : 9;
    WorldGenerator generator(seed);
    RegionGenerator regions(generator.getHeightPipeline(),
                            generator.getCaveGenerator(),
                            generator.getTreeGenerator(),
                            generator.getOreGenerator(), seed);
    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(runs));

    for (int run = 0; run < runs; ++run) {
        const int originX = (run - runs / 2) * 3;
        const int originZ = ((run * 5) % runs - runs / 2) * 3;
        std::vector<std::unique_ptr<Chunk>> storage;
        std::vector<Chunk*> chunks;
        storage.reserve(9);
        chunks.reserve(9);
        for (int dz = 0; dz < 3; ++dz) {
            for (int dx = 0; dx < 3; ++dx) {
                storage.push_back(std::make_unique<Chunk>(originX + dx,
                                                          originZ + dz));
                chunks.push_back(storage.back().get());
            }
        }

        const auto start = std::chrono::steady_clock::now();
        std::vector<RegionGenerationData::PendingBlock> pending;
        regions.generateRegion(originX, originZ, 3, Config::REGION_PADDING,
                               chunks, pending);
        const auto stop = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(stop - start)
                              .count());
    }

    std::sort(samples.begin(), samples.end());
    const double median = samples[samples.size() / 2];
    std::cout << "generation_version=" << WorldGenContext::GENERATION_VERSION
              << " runs=" << samples.size()
              << " median_ms=" << std::fixed << std::setprecision(3) << median
              << " min_ms=" << samples.front()
              << " max_ms=" << samples.back() << '\n';
    return 0;
}
