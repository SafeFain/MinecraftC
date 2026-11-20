#pragma once

#include "world/Block.h"

struct BlockTextureDefinitions {
    std::array<uint16_t, static_cast<size_t>(BlockTexture::Count)> slots{};
    std::array<std::array<BlockTexture, FACE_COUNT>, static_cast<size_t>(BlockId::COUNT)> faces{};
    bool facesReady = false;
};

BlockTextureDefinitions readBlockTextureDefinitions(
    const std::filesystem::path& atlasMetadataPath,
    const std::filesystem::path& blockDefinitionsPath,
    const std::filesystem::path& itemDefinitionsPath);
void publishBlockTextureDefinitions(const BlockTextureDefinitions& definitions);
BlockTexture getFaceTexture(BlockId id, FaceDir face, const BlockTextureDefinitions& definitions);
uint16_t getAtlasTextureIndex(BlockTexture texture, const BlockTextureDefinitions& definitions);
