#pragma once

#include "renderer/RenderDevice.h"
#include "renderer/VoxelGiMaterial.h"

#include <filesystem>

struct BlockAtlasData {
    TextureData texture;
    TextureData normalTexture;
    TextureData propertyTexture;
    uint32_t tilesPerSide = 0;
    uint32_t tileSize = 16;
};

// Loads the generated logical-material atlas and constructs every mip by
// alpha-weighted linear-light tile downsampling, so filtering cannot bleed across
// slots. V4 semantic normal/property maps are linear; legacy atlases synthesize them.
BlockAtlasData buildBlockAtlasData(const std::filesystem::path& assetRoot);

VoxelGiMaterialTable buildVoxelGiMaterials(const BlockAtlasData& atlas);
