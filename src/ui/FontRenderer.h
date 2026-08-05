#pragma once

#include <filesystem>
#include <string>
#include <memory>
#include <glm/glm.hpp>
#include "core/GraphicsApi.h"

class Shader;

class FontRenderer {
public:
    FontRenderer();
    ~FontRenderer();

    FontRenderer(const FontRenderer&) = delete;
    FontRenderer& operator=(const FontRenderer&) = delete;

    void initialize(bool manualGamma, const std::filesystem::path& assetRoot,
                    GraphicsApi api);

    void begin(const glm::mat4& projection);
    void end();

    void renderText(const std::string& text, float x, float y,
                    float scale, const glm::vec3& color);
    glm::vec2 measureText(const std::string& text, float scale) const;

    // Use GLYPH_ prefix to avoid C23 <limits.h> CHAR_WIDTH macro conflict
    static constexpr int GLYPH_W      = 8;
    static constexpr int GLYPH_H      = 14;
    static constexpr int FIRST_GLYPH  = 32;
    static constexpr int GLYPH_COUNT  = 95;   // 126 - 32 + 1
    static constexpr int ATLAS_W      = 2048;
    static constexpr int ATLAS_H      = 2048;

private:
    struct Impl;
    std::unique_ptr<Shader> m_shader;
    std::unique_ptr<Impl> m_impl;
    uint32_t m_atlasTexture = 0;
    uint32_t m_vao = 0;
    uint32_t m_vboPos = 0;
    uint32_t m_vboUV = 0;
    bool m_manualGamma = false;
};
