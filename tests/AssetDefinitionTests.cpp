#include "world/Block.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    const std::string root = MINECRAFTC_SOURCE_DIR;
    require(loadTextureAssetDefinitions(
                root + "/assets/textures/generated/atlas.json",
                root + "/assets/textures/definitions/blocks.json",
                root + "/assets/textures/definitions/items.json"),
            "asset JSON definitions did not load");
    require(getFaceTexture(BlockId::GRASS, FaceDir::TOP) ==
                BlockTexture::GrassTop &&
            getFaceTexture(BlockId::GRASS, FaceDir::BOTTOM) ==
                BlockTexture::Dirt &&
            getFaceTexture(BlockId::GRASS, FaceDir::FRONT) ==
                BlockTexture::GrassSide,
            "JSON block face mapping was not applied");
    require(getAtlasTextureIndex(BlockTexture::Dirt) == 0 &&
            getAtlasTextureIndex(BlockTexture::IronOre) == 16,
            "atlas JSON indices were not applied");
    require(getFaceTextureIndex(BlockId::IRON_ORE, FaceDir::TOP) == 16,
            "block definition did not resolve through atlas metadata");
    require(!loadTextureAssetDefinitions("missing-atlas.json", "missing-blocks.json",
                                         "missing-items.json"),
            "missing definitions did not activate compatibility fallback");
    std::cout << "Asset definition tests passed\n";
    return 0;
}
