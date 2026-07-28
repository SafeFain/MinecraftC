#pragma once

#include <filesystem>
#include <glad/glad.h>

class BlockTextureAtlas {
public:
    BlockTextureAtlas() = default;
    ~BlockTextureAtlas();

    BlockTextureAtlas(const BlockTextureAtlas&) = delete;
    BlockTextureAtlas& operator=(const BlockTextureAtlas&) = delete;

    bool initialize(const std::filesystem::path& assetRoot);
    void bind() const;
    GLuint textureId() const { return m_texture; }

private:
    GLuint m_texture = 0;
};
