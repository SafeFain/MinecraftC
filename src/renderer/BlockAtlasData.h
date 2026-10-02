#pragma once

#include "renderer/RenderDevice.h"
#include "renderer/VoxelGiMaterial.h"

#include <filesystem>

// std430-compatible, little-endian wire fields are decoded explicitly on load.
struct MaterialSequence {
    uint32_t variants = 1;
    uint32_t frames = 1;
    uint32_t fpsMilli = 0;
    uint32_t seed = 0;
    std::array<uint32_t, 64> slots{};
};
static_assert(sizeof(MaterialSequence) == 272);

// Matches the shader's unsigned coordinate hash, including negative cells.
inline uint32_t materialVariant(const MaterialSequence& sequence, int32_t x,
                                int32_t y, int32_t z, uint32_t face) {
    uint32_t h = sequence.seed;
    for (uint32_t coordinate : {uint32_t(x), uint32_t(y), uint32_t(z), face})
        h = (h ^ coordinate) * 16777619u;
    h ^= h >> 16; h *= 2246822519u; h ^= h >> 13;
    return h % sequence.variants;
}

struct BlockAtlasData {
    TextureData texture;
    TextureData normalTexture;
    TextureData propertyTexture;
    uint32_t tilesPerSide = 0;
    uint32_t tileSize = 16;
    std::vector<MaterialSequence> sequences;
};

// Loads the generated logical-material atlas and constructs every mip by
// alpha-weighted linear-light tile downsampling, so filtering cannot bleed across
// slots. V4 semantic normal/property maps are linear; legacy atlases synthesize them.
BlockAtlasData buildBlockAtlasData(const std::filesystem::path& assetRoot);

VoxelGiMaterialTable buildVoxelGiMaterials(const BlockAtlasData& atlas);
