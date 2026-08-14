#include "world/Chunk.h"
#include "Config.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

// The flat override index layout documented in AGENTS.md:
//   index = x + z*CHUNK_SIZE_X + (worldY+64)*CHUNK_SIZE_X*CHUNK_SIZE_Z
constexpr uint32_t makeIndex(int x, int z, int worldY) {
    return static_cast<uint32_t>(x + z * Config::CHUNK_SIZE_X +
        Config::worldYToStorageY(worldY) * Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z);
}

}  // namespace

int main() {
    // Round-trip decode across a representative grid, including both world-Y
    // extremes and the storageY==0 boundary. Regression for the farming tick
    // bug where the Y component was decoded as a storage offset instead of a
    // world coordinate.
    const int worldYs[] = {Config::WORLD_MIN_Y, -63, 0, 63, 70, 318,
                           Config::WORLD_MAX_Y - 1};
    for (int worldY : worldYs) {
        for (int z = 0; z < Config::CHUNK_SIZE_Z; z += 5) {
            for (int x = 0; x < Config::CHUNK_SIZE_X; x += 5) {
                const uint32_t index = makeIndex(x, z, worldY);
                int dx = -1, dz = -1, dy = -999;
                decodeChunkIndex(index, dx, dz, dy);
                require(dx == x && dz == z && dy == worldY,
                        "override index decodes back to its coordinates");
            }
        }
    }

    // Explicit boundary values: index 0 is (0,0,-64); the highest valid index
    // is (15,15,319). The critical value 134*256+... decodes to world Y=70
    // (not the storage offset 134).
    int x = 0, z = 0, y = 0;
    decodeChunkIndex(0, x, z, y);
    require(x == 0 && z == 0 && y == Config::WORLD_MIN_Y,
            "index 0 is the world-bottom corner");

    const uint32_t maxIndex = makeIndex(15, 15, Config::WORLD_MAX_Y - 1);
    decodeChunkIndex(maxIndex, x, z, y);
    require(x == 15 && z == 15 && y == Config::WORLD_MAX_Y - 1,
            "maximum index decodes to the build-limit corner");

    decodeChunkIndex(makeIndex(1, 2, 70), x, z, y);
    require(x == 1 && z == 2 && y == 70,
            "storage offset 134 decodes to world Y=70, not 134");

    std::cout << "chunk override index decode: OK\n";
    return 0;
}
