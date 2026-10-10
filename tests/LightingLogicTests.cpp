#include "world/WorldLighting.h"
#include "world/ChunkStore.h"
#include "world/Chunk.h"
#include <cstdlib>
#include <iostream>
#include <vector>
namespace {
void require(bool value, const char* message) {
    if (!value) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
Chunk* tunnel(ChunkStore& store, int cx) {
    auto* chunk = store.get(cx, 0);
    chunk->loadRawBlocks(std::vector<uint16_t>(Config::CHUNK_VOLUME, static_cast<uint16_t>(BlockId::STONE)));
    for (int x = 0; x < 16; ++x) chunk->setBlock(x, 64, 8, BlockId::AIR);
    chunk->generated = true;
    return chunk;
}
}
int main() {
    ChunkStore store;
    auto* left = tunnel(store, -1);
    WorldLighting lighting(store);
    left->setBlock(15, 64, 8, BlockId::TORCH);
    lighting.rebuild();
    require(left->getBlockLight(15,64,8)==14, "production lighting seeds torch emission");
    auto* right = tunnel(store, 0);
    lighting.markDirty(); lighting.rebuild();
    require(right->getBlockLight(0,64,8)==13 && right->getBlockLight(12,64,8)==1 &&
            right->getBlockLight(13,64,8)==0, "late loaded negative chunk border reconciles with bounded falloff");
    right->setBlock(0,64,8,BlockId::STONE); lighting.updateLightingAt({0,64,8});
    require(right->getBlockLight(1,64,8)==0, "opaque edit removes downstream light");
    right->setBlock(0,64,8,BlockId::WATER); lighting.updateLightingAt({0,64,8});
    require(right->getBlockLight(0,64,8)==12 && right->getBlockLight(1,64,8)==11,
            "water incurs two levels of production attenuation");
    right->setBlock(0,64,8,BlockId::LEAVES); lighting.updateLightingAt({0,64,8});
    require(right->getBlockLight(0,64,8)==13, "leaves retain one-level attenuation");
    right->setBlock(5,64,8,BlockId::TORCH); lighting.updateLightingAt({5,64,8});
    left->setBlock(15,64,8,BlockId::AIR); lighting.updateLightingAt({-1,64,8});
    require(right->getBlockLight(5,64,8)==14 && left->getBlockLight(15,64,8)==8,
            "removal preserves contributions from the surviving source");
    right->setBlock(5,64,8,BlockId::AIR); lighting.updateLightingAt({5,64,8});
    require(left->getBlockLight(15,64,8)==0 && right->getBlockLight(0,64,8)==0,
            "last source removal clears cross-border light");

    ChunkStore skyStore;
    auto* skyLeft = tunnel(skyStore,-1);
    auto* skyRight = tunnel(skyStore,0);
    for (int y=64; y<Config::WORLD_MAX_Y; ++y) skyLeft->setBlock(15,y,8,BlockId::AIR);
    WorldLighting sky(skyStore); sky.rebuild();
    require(skyLeft->getSkyLight(15,64,8)==15 && skyRight->getSkyLight(0,64,8)==14,
            "direct sky stays full vertically and attenuates across negative border");
    skyLeft->setBlock(15,65,8,BlockId::STONE); sky.updateLightingAt({-1,65,8});
    require(skyLeft->getSkyLight(15,64,8)==0 && skyRight->getSkyLight(0,64,8)==0,
            "roof edit removes lateral and vertical skylight");
    skyLeft->setBlock(15,65,8,BlockId::AIR); sky.updateLightingBatch({{-1,65,8},{-1,65,8}});
    require(skyLeft->getSkyLight(15,64,8)==15 && skyRight->getSkyLight(0,64,8)==14,
            "batched roof removal restores skylight");
    std::cout << "Production world lighting tests passed\n";
}
