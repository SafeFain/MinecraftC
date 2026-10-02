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
    for (uint16_t raw = 183; raw <= 200; ++raw) {
        const auto block = static_cast<BlockId>(raw);
        const auto texture = getFaceTexture(block, FaceDir::TOP);
        require(getAtlasTextureIndex(texture) < atlas.tilesPerSide * atlas.tilesPerSide,
                "new decoration material fits atlas capacity");
        for (int face = 0; face < 6; ++face)
            require(getFaceTexture(block, static_cast<FaceDir>(face)) == texture,
                    "new decoration maps all faces consistently");
    }
    require(getAtlasTextureIndex(BlockTexture::TntBottom) == 143 &&
            getAtlasTextureIndex(BlockTexture::StoneBricks) == 144 &&
            getAtlasTextureIndex(BlockTexture::BlackWool) == 161,
            "atlas slots beyond logical material count remain mapped to authored tiles");
    require(getAtlasTextureIndex(BlockTexture::RootedDirt) == 162 &&
            getAtlasTextureIndex(BlockTexture::BeachGrass) == 185 &&
            getFaceTexture(BlockId::DRY_GRASS_BLOCK,FaceDir::BOTTOM) == BlockTexture::Dirt &&
            getFaceTexture(BlockId::DRY_GRASS_BLOCK,FaceDir::FRONT) == BlockTexture::DryGrassSide &&
            getFaceTexture(BlockId::LEAF_LITTER_SOIL,FaceDir::FRONT) == BlockTexture::LeafLitterSide,
            "natural material slots and semantic side faces resolve correctly");
    for (uint16_t raw = 201; raw <= 224; ++raw) {
        const auto block = static_cast<BlockId>(raw);
        const auto texture = getFaceTexture(block,FaceDir::TOP);
        const auto slot = getAtlasTextureIndex(texture);
        require(slot == raw-39 && slot < atlas.tilesPerSide*atlas.tilesPerSide,
                "all natural materials resolve their appended atlas slots");
    }
    const auto materials = buildVoxelGiMaterials(atlas);
    require(materials[size_t(BlockId::STONE)].emission == glm::vec3(0),
            "non-emissive atlas material generated a light source");
    require(materials[size_t(BlockId::STAR_CRYSTAL)].emission !=
            materials[size_t(BlockId::TORCH)].emission,
            "different emissive atlas materials share a fixed warm color");
    BlockAtlasData fixture = atlas;
    const auto fillTile = [&](BlockTexture texture) {
        const auto slot = getAtlasTextureIndex(texture);
        for (uint32_t y = 0; y < 16; ++y) for (uint32_t x = 0; x < 16; ++x) {
            const size_t offset = (size_t(slot/atlas.tilesPerSide*16+y)*atlas.texture.width+
                slot%atlas.tilesPerSide*16+x)*4;
            for (int c = 0; c < 3; ++c) fixture.texture.pixels[offset+c] = x%2 ? 255 : 128;
            fixture.texture.pixels[offset+3] = x%2 ? 0 : 255;
        }
    };
    fillTile(BlockTexture::Stone);
    const auto fixtureMaterials = buildVoxelGiMaterials(fixture);
    const float gray = voxelGiSrgbToLinear(128.0f/255.0f);
    require(std::abs(fixtureMaterials[size_t(BlockId::STONE)].reflectance[0].r-gray)<0.00001f,
            "transparent atlas pixels contaminated linear reflectance");
    for (const auto& material : materials) for (const auto& face : material.reflectance)
        require(glm::all(glm::lessThanEqual(face,glm::vec3(0.9f))) &&
                glm::all(glm::greaterThanEqual(face,glm::vec3(0))),
                "material reflectance exceeded bounded energy");

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
        require(getFaceTexture(BlockId::JUNGLE_LEAVES, FaceDir::TOP) ==
                    BlockTexture::JungleLeaves &&
                getFaceTexture(BlockId::JUNGLE_LEAVES, FaceDir::FRONT) ==
                    BlockTexture::JungleLeaves &&
                getAtlasTextureIndex(BlockTexture::JungleLog) == 34 &&
                getAtlasTextureIndex(BlockTexture::JungleLeaves) == 35 &&
                getAtlasTextureIndex(BlockTexture::JungleLeaves) !=
                    getAtlasTextureIndex(BlockTexture::JungleLog),
                "jungle foliage must cross the atlas row boundary after bark");
        require(getFaceTexture(BlockId::WHITE_BED, FaceDir::TOP) == BlockTexture::WhiteBedTop,
                "bed must use linen top");
    };
    checkFaces();
    const auto readText = [](const std::string& path) {
        std::ifstream input(path);
        return std::string(std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>());
    };
    const std::string chunkShader = readText(
        root + "/assets/shaders/vulkan/chunk.frag");
    const std::string shadowShader = readText(
        root + "/assets/shaders/vulkan/shadow.vert");
    require(chunkShader.find("int slotIndex=max(int(floor(tile)),0);") !=
                std::string::npos &&
            chunkShader.find("slotIndex%tileCount") != std::string::npos &&
            shadowShader.find("int tileIndex=max(int(floor(tileData.z)),0);") !=
                std::string::npos &&
            shadowShader.find("tileIndex%tileCount") != std::string::npos,
            "world atlas lookup must use integer row-boundary addressing");
    require(chunkShader.find(
                "if(innerDistance<inner||outerDistance>=outer)discard;") !=
                std::string::npos &&
            chunkShader.find("bool isLod=frame.chunkOrigin.w>0.0;") !=
                std::string::npos &&
            chunkShader.find("float lodDistance=length(worldPosition.xz);") !=
                std::string::npos &&
            chunkShader.find("floor(phasedPosition/grid)") !=
                std::string::npos &&
            chunkShader.find("neighborInner>=inner&&neighborOuter<outer") !=
                std::string::npos &&
            chunkShader.find("innerCoverage") == std::string::npos &&
            chunkShader.find("outerProgress") == std::string::npos,
            "LOD rings must select one coherent material level per distance");
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
