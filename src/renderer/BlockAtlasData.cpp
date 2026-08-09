#include "renderer/BlockAtlasData.h"

#include "core/AssetStore.h"
#include "world/Block.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <stb_image.h>

namespace {
constexpr uint32_t TILE_SIZE = 16;

std::vector<uint8_t> downsampleTiles(const std::vector<uint8_t>& source,
                                     uint32_t tiles, uint32_t tileSize) {
    const uint32_t nextTile = tileSize / 2;
    const uint32_t sourceWidth = tiles * tileSize;
    const uint32_t targetWidth = tiles * nextTile;
    std::vector<uint8_t> target(static_cast<size_t>(targetWidth) * targetWidth * 4u);
    for (uint32_t tileY = 0; tileY < tiles; ++tileY) {
        for (uint32_t tileX = 0; tileX < tiles; ++tileX) {
            for (uint32_t y = 0; y < nextTile; ++y) {
                for (uint32_t x = 0; x < nextTile; ++x) {
                    int alphaSum = 0;
                    int rgbSum[3]{};
                    for (uint32_t oy = 0; oy < 2; ++oy) {
                        for (uint32_t ox = 0; ox < 2; ++ox) {
                            const uint32_t sx = tileX * tileSize + x * 2 + ox;
                            const uint32_t sy = tileY * tileSize + y * 2 + oy;
                            const size_t src = (static_cast<size_t>(sy) * sourceWidth + sx) * 4u;
                            const int alpha = source[src + 3];
                            alphaSum += alpha;
                            for (int channel = 0; channel < 3; ++channel)
                                rgbSum[channel] += source[src + channel] * alpha;
                        }
                    }
                    const uint32_t dx = tileX * nextTile + x;
                    const uint32_t dy = tileY * nextTile + y;
                    const size_t dst = (static_cast<size_t>(dy) * targetWidth + dx) * 4u;
                    target[dst + 3] = static_cast<uint8_t>(alphaSum / 4);
                    if (alphaSum > 0) for (int channel = 0; channel < 3; ++channel)
                        target[dst + channel] = static_cast<uint8_t>(rgbSum[channel] / alphaSum);
                }
            }
        }
    }
    return target;
}
}

BlockAtlasData buildBlockAtlasData(const std::filesystem::path& assetRoot) {
    const auto path = assetRoot / "textures" / "generated" / "atlas.png";
    const std::vector<uint8_t> encoded = AssetStore::readPath(path);
    if (encoded.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("Block atlas is too large: " + path.string());
    int width = 0, height = 0, channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()),
                                             &width, &height, &channels, STBI_rgb_alpha);
    if (!decoded || width <= 0 || width != height || width % TILE_SIZE != 0) {
        stbi_image_free(decoded);
        throw std::runtime_error("Generated block atlas must be a square 16px grid");
    }
    BlockAtlasData result;
    result.tilesPerSide = static_cast<uint32_t>(width) / TILE_SIZE;
    uint32_t requiredSide = 1;
    while (requiredSide * requiredSide < static_cast<uint32_t>(BlockTexture::Count))
        ++requiredSide;
    if (result.tilesPerSide < requiredSide) {
        stbi_image_free(decoded);
        throw std::runtime_error("Generated block atlas has too few logical slots");
    }
    result.texture.width = static_cast<uint32_t>(width);
    result.texture.height = static_cast<uint32_t>(height);
    result.texture.pixels.assign(decoded, decoded + static_cast<size_t>(width) * height * 4u);
    stbi_image_free(decoded);

    // Keep logical slot rows in their generated row-major positions (slot zero
    // is sampled from the texture's bottom row), but flip pixels inside every
    // tile for the bottom-left texture-coordinate convention.
    std::array<uint8_t, TILE_SIZE * 4> row{};
    for (uint32_t tileY = 0; tileY < result.tilesPerSide; ++tileY) {
        for (uint32_t tileX = 0; tileX < result.tilesPerSide; ++tileX) {
            for (uint32_t y = 0; y < TILE_SIZE / 2; ++y) {
                uint8_t* top = result.texture.pixels.data() +
                    (static_cast<size_t>(tileY * TILE_SIZE + y) * width +
                     tileX * TILE_SIZE) * 4u;
                uint8_t* bottom = result.texture.pixels.data() +
                    (static_cast<size_t>(tileY * TILE_SIZE + TILE_SIZE - 1 - y) * width +
                     tileX * TILE_SIZE) * 4u;
                std::copy_n(top, row.size(), row.data());
                std::copy_n(bottom, row.size(), top);
                std::copy_n(row.data(), row.size(), bottom);
            }
        }
    }
    uint32_t tileSize = TILE_SIZE;
    std::vector<uint8_t> level = result.texture.pixels;
    while (tileSize > 1) {
        level = downsampleTiles(level, result.tilesPerSide, tileSize);
        tileSize /= 2;
        result.texture.mipLevels.push_back({result.tilesPerSide * tileSize,
                                            result.tilesPerSide * tileSize, level});
    }
    validateTextureData(result.texture);
    return result;
}
