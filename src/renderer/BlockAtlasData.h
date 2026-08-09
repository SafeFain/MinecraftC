#pragma once

#include "renderer/RenderDevice.h"

#include <filesystem>

struct BlockAtlasData {
    TextureData texture;
    uint32_t tilesPerSide = 0;
    uint32_t tileSize = 16;
};

// Loads the generated logical-material atlas and constructs every mip by
// downsampling each tile independently, so filtering cannot bleed across slots.
BlockAtlasData buildBlockAtlasData(const std::filesystem::path& assetRoot);
