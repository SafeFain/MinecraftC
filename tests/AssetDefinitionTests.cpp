#include "world/Block.h"
#include "game/Item.h"
#include "renderer/BlockAtlasData.h"

#include <array>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <fstream>
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
    const BlockAtlasData atlas = buildBlockAtlasData(root + "/assets");
    require(atlas.texture.mipLevels.size() == 4, "atlas requires five tile-local levels");
    const auto holesIn = [&](const std::vector<uint8_t>& pixels,
                             uint32_t tileSize, BlockTexture texture) {
        const uint32_t slot = getAtlasTextureIndex(texture);
        const uint32_t tx = slot % atlas.tilesPerSide, ty = slot / atlas.tilesPerSide;
        size_t count = 0;
        for (uint32_t y = 0; y < tileSize; ++y)
            for (uint32_t x = 0; x < tileSize; ++x) {
                const size_t offset = ((ty * tileSize + y) * atlas.tilesPerSide * tileSize +
                                        tx * tileSize + x) * 4u;
                if (pixels[offset + 3] < 26) ++count; // Runtime alpha cutoff is 0.1.
            }
        return count;
    };
    for (BlockTexture leaf : {BlockTexture::Leaves, BlockTexture::BirchLeaves,
            BlockTexture::SpruceLeaves, BlockTexture::JungleLeaves,
            BlockTexture::AcaciaLeaves, BlockTexture::SkyrootLeaves}) {
        const size_t baseHoles = holesIn(atlas.texture.pixels, 16, leaf);
        require(baseHoles >= 24 && baseHoles <= 64, "leaf gaps must be visible but bounded");
        uint32_t size = 8;
        for (const auto& level : atlas.texture.mipLevels) {
            const size_t count = holesIn(level.pixels, size, leaf);
            if (size > 1) {
                require(count > 0 && count < size * size, "leaf mip lost foliage or all gaps");
                const double expected = baseHoles * size * size / 256.0;
                require(std::abs(static_cast<double>(count) - expected) <= 1.0,
                        "leaf mip does not preserve source cutout coverage");
            } else require(count == 0, "terminal leaf mip must preserve distant canopies");
            require(holesIn(level.pixels, size, BlockTexture::Stone) == 0,
                    "cutout correction leaked into another material");
            size /= 2;
        }
    }
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
    const auto checkFaces = [] {
        require(getFaceTexture(BlockId::FURNACE, FaceDir::FRONT) == BlockTexture::Furnace &&
                getFaceTexture(BlockId::FURNACE, FaceDir::BACK) == BlockTexture::FurnaceSide &&
                getFaceTexture(BlockId::FURNACE, FaceDir::TOP) == BlockTexture::FurnaceTop,
                "functional front/top/side mapping is incorrect");
        require(getFaceTexture(BlockId::BIRCH_WOOD, FaceDir::TOP) == BlockTexture::BirchLogTop &&
                getFaceTexture(BlockId::ACACIA_WOOD, FaceDir::BOTTOM) == BlockTexture::AcaciaLogTop,
                "tree species must use their own end grain");
        require(getFaceTexture(BlockId::WHITE_BED, FaceDir::TOP) == BlockTexture::WhiteBedTop,
                "bed must use linen top");
    };
    checkFaces();
    require(!loadTextureAssetDefinitions("missing-atlas.json", "missing-blocks.json",
                                         "missing-items.json"),
            "missing definitions did not activate compatibility fallback");
    checkFaces();
    std::ifstream itemsAtlas(root + "/assets/textures/generated/items_atlas.json");
    const std::string itemMetadata((std::istreambuf_iterator<char>(itemsAtlas)),
                                   std::istreambuf_iterator<char>());
    require(!itemMetadata.empty(), "items atlas metadata did not load");
    for (ItemId item : creativeInventoryItems()) {
        std::string logicalName = getItemProps(item).name;
        for (char& c : logicalName) {
            const auto value = static_cast<unsigned char>(c);
            c = std::isalnum(value) ? static_cast<char>(std::tolower(value)) : '_';
        }
        require(itemMetadata.find("\"" + logicalName + "\"") != std::string::npos,
                ("items atlas missing registered item: " + logicalName).c_str());
    }
    std::cout << "Asset definition tests passed\n";
    return 0;
}
