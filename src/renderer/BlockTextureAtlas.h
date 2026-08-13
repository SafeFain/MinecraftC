#pragma once

#include <filesystem>
#include "renderer/RenderHandles.h"

class BlockTextureAtlas {
public:
    BlockTextureAtlas() = default;
    ~BlockTextureAtlas();

    BlockTextureAtlas(const BlockTextureAtlas&) = delete;
    BlockTextureAtlas& operator=(const BlockTextureAtlas&) = delete;

    bool initialize(const std::filesystem::path& assetRoot);
    void bind() const;
    void bindMaterialMaps() const;
    RenderTextureHandle texture() const { return m_texture; }
    static int tilesPerSide();

private:
    RenderTextureHandle m_texture;
    uint32_t m_normalTexture = 0;
    uint32_t m_propertyTexture = 0;
};
