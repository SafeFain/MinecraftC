#include "world/Block.h"
#include "game/Item.h"
#include "renderer/BlockAtlasData.h"

#include <array>
#include <cmath>
#include <cctype>
#include <chrono>
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
    require(atlas.texture.width <= 1024 && atlas.tilesPerSide * 16 == atlas.texture.width &&
            atlas.sequences.size() == atlas.tilesPerSide * atlas.tilesPerSide,
            "v5 atlas must respect physical budget and sequence capacity");
    const auto& stoneSequence = atlas.sequences[getAtlasTextureIndex(BlockTexture::Stone)];
    const auto& fireSequence = atlas.sequences[getAtlasTextureIndex(BlockTexture::Fire)];
    require(stoneSequence.variants == 4 && stoneSequence.frames == 1 &&
            fireSequence.frames == 16 && fireSequence.fpsMilli == 10000,
            "formal stone variants and fire animation were not enabled");
    for (size_t i = 0; i < atlas.sequences.size(); ++i) {
        const auto& sequence = atlas.sequences[i];
        require(sequence.slots[0] == i, "foundation slots changed");
        for (uint32_t j = 0; j < sequence.variants * sequence.frames; ++j)
            require(sequence.slots[j] < atlas.sequences.size(), "sequence escaped physical atlas");
    }
    std::array<bool,4> selected{};
    for (int x = -40; x < 40; ++x) for (int z = -18; z < 18; ++z) {
        const auto variant = materialVariant(stoneSequence,x,-7,z,0);
        selected[variant] = true;
        require(variant == materialVariant(stoneSequence,(x-32)+32,-7,(z+16)-16,0),
                "world coordinate hashing changed across chunk-local reconstruction");
    }
    for (bool visible : selected) require(visible, "hash never selected a declared variant");
    require(atlas.normalTexture.format == TextureFormat::Rgba8Unorm &&
            atlas.propertyTexture.format == TextureFormat::Rgba8Unorm &&
            atlas.normalTexture.mipLevels.size() == 4 && atlas.propertyTexture.mipLevels.size() == 4,
            "semantic material maps require linear formats and tile-local mips");
    const auto propertyAt = [&](BlockTexture texture, uint32_t x, uint32_t y, int channel) {
        const uint32_t slot = getAtlasTextureIndex(texture);
        const size_t offset = ((slot / atlas.tilesPerSide * 16u + y) * atlas.texture.width +
                               slot % atlas.tilesPerSide * 16u + x) * 4u;
        return atlas.propertyTexture.pixels[offset + channel];
    };
    require(propertyAt(BlockTexture::BlueIce, 8, 8, 0) <
            propertyAt(BlockTexture::Shale, 8, 8, 0),
            "ice and rock must retain semantic roughness differences");
    require(propertyAt(BlockTexture::SkyrootLeaves, 8, 8, 0) > 220,
            "skyroot foliage must use foliage roughness");
    require(propertyAt(BlockTexture::StarCrystal, 8, 8, 2) > 0 &&
            propertyAt(BlockTexture::Stone, 8, 8, 2) == 0,
            "semantic emission leaked into non-emissive material");
    // Exercise the real loader's legacy path and malformed declared sequence data.
    const auto fixtureRoot = std::filesystem::temp_directory_path() /
        ("minecraftc-material-sequence-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto fixtureTextures = fixtureRoot / "textures/generated";
    std::filesystem::create_directories(fixtureTextures);
    struct FixtureCleanup {
        std::filesystem::path path;
        ~FixtureCleanup() { std::error_code error; std::filesystem::remove_all(path,error); }
    } cleanup{fixtureRoot};
    const auto sourceTextures = std::filesystem::path(root) / "assets/textures/generated";
    for (const char* name : {"atlas.png", "atlas_normal.png", "atlas_property.png"})
        std::filesystem::copy_file(sourceTextures / name, fixtureTextures / name);
    std::ifstream metadataStream(sourceTextures / "atlas.json");
    const std::string currentMetadata((std::istreambuf_iterator<char>(metadataStream)), {});
    std::string legacyMetadata = currentMetadata;
    legacyMetadata.replace(legacyMetadata.find("sequence_blob"), 13, "unused_sequence");
    { std::ofstream file(fixtureTextures / "atlas.json"); file << legacyMetadata; }
    const auto legacyAtlas = buildBlockAtlasData(fixtureRoot);
    for (size_t slot = 0; slot < legacyAtlas.sequences.size(); ++slot)
        require(legacyAtlas.sequences[slot].variants == 1 && legacyAtlas.sequences[slot].frames == 1 &&
                legacyAtlas.sequences[slot].slots[0] == slot, "legacy atlas did not synthesize identity sequences");
    { std::ofstream file(fixtureTextures / "atlas.json"); file << currentMetadata; }
    { std::ofstream file(fixtureTextures / "atlas_sequences.bin", std::ios::binary); file << "invalid"; }
    bool rejected = false;
    try { (void)buildBlockAtlasData(fixtureRoot); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "malformed declared sequence table was silently accepted");
    loadTextureAssetDefinitions(sourceTextures / "atlas.json",
        std::filesystem::path(root) / "assets/textures/definitions/blocks.json",
        std::filesystem::path(root) / "assets/textures/definitions/items.json");
    // Check every first-level color sample against independent linear-light,
    // alpha-weighted reference math. This also tests all atlas row boundaries.
    const auto& firstMip = atlas.texture.mipLevels.front();
    for (uint32_t y = 0; y < firstMip.height; ++y) for (uint32_t x = 0; x < firstMip.width; ++x) {
        float sum[3]{};
        int alpha = 0;
        for (uint32_t dy = 0; dy < 2; ++dy) for (uint32_t dx = 0; dx < 2; ++dx) {
            const size_t source = ((size_t(y) * 2u + dy) * atlas.texture.width + x * 2u + dx) * 4u;
            const int a = atlas.texture.pixels[source + 3];
            alpha += a;
            for (int c = 0; c < 3; ++c)
                sum[c] += voxelGiSrgbToLinear(atlas.texture.pixels[source + c] / 255.0f) * a;
        }
        if (!alpha) continue;
        for (int c = 0; c < 3; ++c) {
            const float linear = sum[c] / alpha;
            const float encoded = linear <= .0031308f ? linear * 12.92f :
                1.055f * std::pow(linear, 1.0f / 2.4f) - .055f;
            const int expected = static_cast<int>(std::lround(encoded * 255.0f));
            require(std::abs(int(firstMip.pixels[(size_t(y) * firstMip.width + x) * 4u + c]) - expected) <= 1,
                    "color mip used encoded RGB averaging or crossed a tile boundary");
        }
    }
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
    for (uint16_t raw=225; raw<=252; ++raw) {
        const auto block = static_cast<BlockId>(raw);
        const auto slot = getFaceTextureIndex(block,FaceDir::TOP);
        require(slot == raw-37 && slot < atlas.tilesPerSide*atlas.tilesPerSide,
                "v16 blocks map to appended material slots");
    }
    for (uint16_t raw=253; raw<269; ++raw) {
        const auto block=static_cast<BlockId>(raw);
        require(getFaceTextureIndex(block,FaceDir::TOP)==raw-37,
                "Heaven materials retain appended atlas mapping");
    }
    require(propertyAt(BlockTexture::StarCrystalLamp,8,8,2)>0,
            "star lamps inject authored emission into GI");
    require(propertyAt(BlockTexture::HangingCloudVine,3,8,2)==204 &&
            propertyAt(BlockTexture::HangingCloudVine,8,14,2)==0 &&
            propertyAt(BlockTexture::HangingCloudVine,0,0,2)==0,
            "only stardew beads glow; vine stems and transparent air remain non-emissive");
    const auto materials = buildVoxelGiMaterials(atlas);
    const auto& vineMaterial = materials[size_t(BlockId::HANGING_CLOUD_VINE)];
    require(std::abs(vineMaterial.emission.b-0.8f)<0.00001f &&
            vineMaterial.emission.b>vineMaterial.emission.r,
            "stardew GI lighting must retain its cyan tint and level-12 strength");
    require(materials[size_t(BlockId::STONE)].emission == glm::vec3(0),
            "non-emissive atlas material generated a light source");
    require(materials[size_t(BlockId::STAR_CRYSTAL)].emission !=
            materials[size_t(BlockId::TORCH)].emission,
            "different emissive atlas materials share a fixed warm color");
    BlockAtlasData fixture = atlas;
    const auto fillTile = [&](BlockTexture texture) {
        const auto logical = getAtlasTextureIndex(texture);
        const auto& sequence = fixture.sequences[logical];
        for (uint32_t sample = 0; sample < sequence.variants * sequence.frames; ++sample) {
          const auto slot = sequence.slots[sample];
          for (uint32_t y = 0; y < 16; ++y) for (uint32_t x = 0; x < 16; ++x) {
            const size_t offset = (size_t(slot/atlas.tilesPerSide*16+y)*atlas.texture.width+
                slot%atlas.tilesPerSide*16+x)*4;
            for (int c = 0; c < 3; ++c) fixture.texture.pixels[offset+c] = x%2 ? 255 : 128;
            fixture.texture.pixels[offset+3] = x%2 ? 0 : 255;
          }
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
            chunkShader.find("physicalSlot%tileCount") != std::string::npos &&
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
    const auto highAtlas=std::filesystem::temp_directory_path() /
        ("minecraftc-high-atlas-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".json");
    { std::ofstream out(highAtlas); out << R"({"textures":{"star_crystal_lamp":{"index":300}}})"; }
    require(loadTextureAssetDefinitions(highAtlas,
        root+"/assets/textures/definitions/blocks.json",
        root+"/assets/textures/definitions/items.json") &&
        getFaceTextureIndex(BlockId::STAR_CRYSTAL_LAMP,FaceDir::TOP)==300,
        "atlas slots above 255 must not narrow");
    std::filesystem::remove(highAtlas);
    std::cout << "Asset definition tests passed\n";
    return 0;
}
