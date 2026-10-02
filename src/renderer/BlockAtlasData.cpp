#include "renderer/BlockAtlasData.h"

#include "core/AssetStore.h"
#include "world/Block.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <stb_image.h>

namespace {
constexpr uint32_t TILE_SIZE = 16;

uint8_t encodeSrgb(float value) {
    const float encoded = value <= 0.0031308f ? value * 12.92f :
        1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint8_t>(std::lround(std::clamp(encoded, 0.0f, 1.0f) * 255.0f));
}

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
                    float rgbSum[3]{};
                    for (uint32_t oy = 0; oy < 2; ++oy) {
                        for (uint32_t ox = 0; ox < 2; ++ox) {
                            const uint32_t sx = tileX * tileSize + x * 2 + ox;
                            const uint32_t sy = tileY * tileSize + y * 2 + oy;
                            const size_t src = (static_cast<size_t>(sy) * sourceWidth + sx) * 4u;
                            const int alpha = source[src + 3];
                            alphaSum += alpha;
                            for (int channel = 0; channel < 3; ++channel)
                                rgbSum[channel] += voxelGiSrgbToLinear(
                                    source[src + channel] / 255.0f) * alpha;
                        }
                    }
                    const uint32_t dx = tileX * nextTile + x;
                    const uint32_t dy = tileY * nextTile + y;
                    const size_t dst = (static_cast<size_t>(dy) * targetWidth + dx) * 4u;
                    target[dst + 3] = static_cast<uint8_t>(alphaSum / 4);
                    if (alphaSum > 0) for (int channel = 0; channel < 3; ++channel)
                        target[dst + channel] = encodeSrgb(rgbSum[channel] / alphaSum);
                }
            }
        }
    }
    return target;
}

void preserveLeafCutoutCoverage(std::vector<uint8_t>& mip,
                               const std::vector<uint8_t>& base,
                               uint32_t tiles, uint32_t size) {
    // One texel cannot represent both foliage and air. Keep the averaged
    // terminal mip for distant canopies and the opaque-leaf fallback shader.
    if (size == 1) return;
    constexpr std::array<BlockTexture, 6> leaves{{
        BlockTexture::Leaves, BlockTexture::BirchLeaves,
        BlockTexture::SpruceLeaves, BlockTexture::JungleLeaves,
        BlockTexture::AcaciaLeaves, BlockTexture::SkyrootLeaves}};
    for (BlockTexture texture : leaves) {
        const uint32_t slot = getAtlasTextureIndex(texture);
        const uint32_t tx = slot % tiles, ty = slot / tiles;
        uint32_t holes = 0;
        for (uint32_t y = 0; y < TILE_SIZE; ++y)
            for (uint32_t x = 0; x < TILE_SIZE; ++x) {
                const size_t offset = ((ty * TILE_SIZE + y) * tiles * TILE_SIZE +
                                       tx * TILE_SIZE + x) * 4u;
                if (base[offset + 3] == 0) ++holes;
            }
        if (!holes) continue;
        std::vector<size_t> offsets;
        for (uint32_t y = 0; y < size; ++y)
            for (uint32_t x = 0; x < size; ++x)
                offsets.push_back(((ty * size + y) * tiles * size +
                                    tx * size + x) * 4u + 3u);
        // The lowest-alpha texels are the actual leaf gaps. Resolve ties by
        // stable position; never resample adjacent atlas materials.
        std::stable_sort(offsets.begin(), offsets.end(), [&](size_t a, size_t b) {
            return mip[a] < mip[b];
        });
        const size_t count = std::min(offsets.size() - 1,
            std::max<size_t>(1, (holes * size * size + 128u) / 256u));
        for (size_t i = 0; i < offsets.size(); ++i)
            mip[offsets[i]] = i < count ? 0 : 255;
    }
}

std::vector<uint8_t> downsampleLinearTiles(const std::vector<uint8_t>& source,
                                           uint32_t tiles, uint32_t tileSize,
                                           bool normalizeNormals) {
    const uint32_t nextTile = tileSize / 2;
    const uint32_t sourceWidth = tiles * tileSize;
    const uint32_t targetWidth = tiles * nextTile;
    std::vector<uint8_t> target(static_cast<size_t>(targetWidth) * targetWidth * 4u);
    for (uint32_t tileY = 0; tileY < tiles; ++tileY) {
        for (uint32_t tileX = 0; tileX < tiles; ++tileX) {
            for (uint32_t y = 0; y < nextTile; ++y) {
                for (uint32_t x = 0; x < nextTile; ++x) {
                    int sum[4]{};
                    for (uint32_t oy = 0; oy < 2; ++oy) {
                        for (uint32_t ox = 0; ox < 2; ++ox) {
                            const uint32_t sx = tileX * tileSize + x * 2 + ox;
                            const uint32_t sy = tileY * tileSize + y * 2 + oy;
                            const size_t src =
                                (static_cast<size_t>(sy) * sourceWidth + sx) * 4u;
                            for (int channel = 0; channel < 4; ++channel)
                                sum[channel] += source[src + channel];
                        }
                    }
                    const uint32_t dx = tileX * nextTile + x;
                    const uint32_t dy = tileY * nextTile + y;
                    const size_t dst =
                        (static_cast<size_t>(dy) * targetWidth + dx) * 4u;
                    for (int channel = 0; channel < 4; ++channel)
                        target[dst + channel] = static_cast<uint8_t>(sum[channel] / 4);
                    if (normalizeNormals) {
                        float nx = target[dst] / 127.5f - 1.0f;
                        float ny = target[dst + 1] / 127.5f - 1.0f;
                        float nz = target[dst + 2] / 127.5f - 1.0f;
                        const float inverseLength = 1.0f /
                            std::max(0.0001f, std::sqrt(nx * nx + ny * ny + nz * nz));
                        nx *= inverseLength; ny *= inverseLength; nz *= inverseLength;
                        target[dst] = static_cast<uint8_t>(
                            std::clamp(nx * 127.5f + 127.5f, 0.0f, 255.0f));
                        target[dst + 1] = static_cast<uint8_t>(
                            std::clamp(ny * 127.5f + 127.5f, 0.0f, 255.0f));
                        target[dst + 2] = static_cast<uint8_t>(
                            std::clamp(nz * 127.5f + 127.5f, 0.0f, 255.0f));
                    }
                }
            }
        }
    }
    return target;
}

uint8_t roughnessForTexture(BlockTexture texture) {
    switch (texture) {
        case BlockTexture::Water: return 24;
        case BlockTexture::Glass: return 48;
        case BlockTexture::Ice: return 42;
        case BlockTexture::PackedIce:
        case BlockTexture::BlueIce: return 42;
        case BlockTexture::Lava: return 138;
        case BlockTexture::IronOre:
        case BlockTexture::GoldOre:
        case BlockTexture::DiamondOre:
        case BlockTexture::CoalOre: return 142;
        case BlockTexture::EmeraldOre:
        case BlockTexture::DeepslateEmeraldOre: return 142;
        case BlockTexture::Leaves:
        case BlockTexture::BirchLeaves:
        case BlockTexture::SpruceLeaves:
        case BlockTexture::JungleLeaves:
        case BlockTexture::AcaciaLeaves:
        case BlockTexture::SkyrootLeaves: return 232;
        default: return 202;
    }
}

uint8_t materialRoughness(size_t slot) {
    for (size_t index = 0; index < static_cast<size_t>(BlockTexture::Count);
         ++index) {
        const auto texture = static_cast<BlockTexture>(index);
        if (getAtlasTextureIndex(texture) == slot)
            return roughnessForTexture(texture);
    }
    return 205;
}

void flipTileRows(std::vector<uint8_t>& pixels, uint32_t width, uint32_t tiles) {
    std::array<uint8_t, TILE_SIZE * 4> row{};
    for (uint32_t ty = 0; ty < tiles; ++ty) for (uint32_t tx = 0; tx < tiles; ++tx) {
        for (uint32_t y = 0; y < TILE_SIZE / 2; ++y) {
            uint8_t* top = pixels.data() +
                (size_t(ty * TILE_SIZE + y) * width + tx * TILE_SIZE) * 4u;
            uint8_t* bottom = pixels.data() +
                (size_t(ty * TILE_SIZE + TILE_SIZE - 1 - y) * width + tx * TILE_SIZE) * 4u;
            std::copy_n(top, row.size(), row.data());
            std::copy_n(bottom, row.size(), top);
            std::copy_n(row.data(), row.size(), bottom);
        }
    }
}

TextureData loadLinearAtlas(const std::filesystem::path& path, uint32_t width) {
    const auto bytes = AssetStore::readPath(path);
    if (bytes.size() > size_t(std::numeric_limits<int>::max()))
        throw std::runtime_error("Material atlas too large: " + path.string());
    int w = 0, h = 0, channels = 0;
    stbi_set_flip_vertically_on_load(0);
    stbi_uc* decoded = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                             &w, &h, &channels, STBI_rgb_alpha);
    if (!decoded || w != static_cast<int>(width) || h != w) {
        stbi_image_free(decoded);
        throw std::runtime_error("Material atlas must match albedo dimensions: " + path.string());
    }
    TextureData texture;
    texture.width = width;
    texture.height = width;
    texture.format = TextureFormat::Rgba8Unorm;
    texture.pixels.assign(decoded, decoded + size_t(width) * width * 4u);
    stbi_image_free(decoded);
    flipTileRows(texture.pixels, width, width / TILE_SIZE);
    return texture;
}

void buildMaterialTextures(BlockAtlasData& result, const std::filesystem::path& generatedRoot,
                           bool generatedMaps) {
    const uint32_t width = result.texture.width;
    if (generatedMaps) {
        // V4 map declarations are a strict asset contract. Missing/corrupt maps
        // must fail rather than silently replacing semantic geometry with RGB.
        result.normalTexture = loadLinearAtlas(generatedRoot / "atlas_normal.png", width);
        result.propertyTexture = loadLinearAtlas(generatedRoot / "atlas_property.png", width);
    } else {
        result.normalTexture.width = width;
        result.normalTexture.height = width;
        result.normalTexture.format = TextureFormat::Rgba8Unorm;
        result.normalTexture.pixels.resize(result.texture.pixels.size());
        result.propertyTexture.width = width;
        result.propertyTexture.height = width;
        result.propertyTexture.format = TextureFormat::Rgba8Unorm;
        result.propertyTexture.pixels.resize(result.texture.pixels.size());
        const auto luminanceAt = [&](uint32_t tileX, uint32_t tileY, int x, int y) {
            x = std::clamp(x, 0, static_cast<int>(TILE_SIZE) - 1);
            y = std::clamp(y, 0, static_cast<int>(TILE_SIZE) - 1);
            const uint32_t px = tileX * TILE_SIZE + static_cast<uint32_t>(x);
            const uint32_t py = tileY * TILE_SIZE + static_cast<uint32_t>(y);
            const size_t offset = (static_cast<size_t>(py) * width + px) * 4u;
            return (result.texture.pixels[offset] * 54 +
                    result.texture.pixels[offset + 1] * 183 +
                    result.texture.pixels[offset + 2] * 19) / 256.0f;
        };
        for (uint32_t tileY = 0; tileY < result.tilesPerSide; ++tileY) {
            for (uint32_t tileX = 0; tileX < result.tilesPerSide; ++tileX) {
                const size_t slot = tileY * result.tilesPerSide + tileX;
                const uint8_t roughness = materialRoughness(slot);
                const bool emissive =
                    slot == getAtlasTextureIndex(BlockTexture::Lava) ||
                    slot == getAtlasTextureIndex(BlockTexture::Fire);
                for (int y = 0; y < static_cast<int>(TILE_SIZE); ++y) {
                    for (int x = 0; x < static_cast<int>(TILE_SIZE); ++x) {
                        const uint32_t px = tileX * TILE_SIZE + static_cast<uint32_t>(x);
                        const uint32_t py = tileY * TILE_SIZE + static_cast<uint32_t>(y);
                        const size_t offset = (static_cast<size_t>(py) * width + px) * 4u;
                        const bool visible = result.texture.pixels[offset + 3] >= 8;
                        const float dx = visible
                            ? (luminanceAt(tileX, tileY, x - 1, y) -
                               luminanceAt(tileX, tileY, x + 1, y)) / 255.0f : 0.0f;
                        const float dy = visible
                            ? (luminanceAt(tileX, tileY, x, y - 1) -
                               luminanceAt(tileX, tileY, x, y + 1)) / 255.0f : 0.0f;
                        const float inverseLength = 1.0f /
                            std::sqrt(dx * dx * 1.8f + dy * dy * 1.8f + 1.0f);
                        result.normalTexture.pixels[offset] = static_cast<uint8_t>(
                            std::clamp(dx * 1.34164f * inverseLength * 127.5f + 127.5f,
                                       0.0f, 255.0f));
                        result.normalTexture.pixels[offset + 1] = static_cast<uint8_t>(
                            std::clamp(dy * 1.34164f * inverseLength * 127.5f + 127.5f,
                                       0.0f, 255.0f));
                        result.normalTexture.pixels[offset + 2] = static_cast<uint8_t>(
                            std::clamp(inverseLength * 127.5f + 127.5f, 0.0f, 255.0f));
                        result.normalTexture.pixels[offset + 3] = 255;
                        result.propertyTexture.pixels[offset] = roughness;
                        result.propertyTexture.pixels[offset + 1] = 0;
                        result.propertyTexture.pixels[offset + 2] = emissive ? 255 : 0;
                        result.propertyTexture.pixels[offset + 3] = static_cast<uint8_t>(
                            std::clamp(luminanceAt(tileX, tileY, x, y), 0.0f, 255.0f));
                    }
                }
            }
        }
    }
    uint32_t tileSize = TILE_SIZE;
    std::vector<uint8_t> normals = result.normalTexture.pixels;
    std::vector<uint8_t> properties = result.propertyTexture.pixels;
    while (tileSize > 1) {
        normals = downsampleLinearTiles(normals, result.tilesPerSide, tileSize, true);
        properties = downsampleLinearTiles(
            properties, result.tilesPerSide, tileSize, false);
        tileSize /= 2;
        const uint32_t mipWidth = result.tilesPerSide * tileSize;
        result.normalTexture.mipLevels.push_back({mipWidth, mipWidth, normals});
        result.propertyTexture.mipLevels.push_back({mipWidth, mipWidth, properties});
    }
    validateTextureData(result.normalTexture);
    validateTextureData(result.propertyTexture);
}
}

BlockAtlasData buildBlockAtlasData(const std::filesystem::path& assetRoot) {
    const auto generatedRoot = assetRoot / "textures" / "generated";
    loadTextureAssetDefinitions(
        generatedRoot / "atlas.json",
        assetRoot / "textures" / "definitions" / "blocks.json",
        assetRoot / "textures" / "definitions" / "items.json");
    const auto path = assetRoot / "textures" / "generated" / "atlas.png";
    const std::vector<uint8_t> encoded = AssetStore::readPath(path);
    if (encoded.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("Block atlas is too large: " + path.string());
    int width = 0, height = 0, channels = 0;
    // The atlas is flipped per-tile below, so it must always be decoded with
    // the global stbi flip flag cleared. Callers elsewhere set the flag to 1
    // for textures that need top-left->bottom-left conversion, and this shared
    // builder can be re-invoked (graphics reset / backend switch) after that.
    stbi_set_flip_vertically_on_load(0);
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
    flipTileRows(result.texture.pixels, result.texture.width, result.tilesPerSide);
    uint32_t tileSize = TILE_SIZE;
    std::vector<uint8_t> level = result.texture.pixels;
    while (tileSize > 1) {
        level = downsampleTiles(level, result.tilesPerSide, tileSize);
        tileSize /= 2;
        // Keep the unmodified averages as the source of the next mip so
        // coverage correction never feeds back into color or opacity.
        auto cutoutLevel = level;
        preserveLeafCutoutCoverage(cutoutLevel, result.texture.pixels,
                                   result.tilesPerSide, tileSize);
        result.texture.mipLevels.push_back({result.tilesPerSide * tileSize,
                                            result.tilesPerSide * tileSize,
                                            std::move(cutoutLevel)});
    }
    std::string metadata;
    try { metadata = AssetStore::readTextPath(generatedRoot / "atlas.json"); }
    catch (const std::runtime_error&) { /* Legacy logical-material fallback. */ }
    buildMaterialTextures(result, generatedRoot, metadata.find("\"material_maps\"") != std::string::npos);
    validateTextureData(result.texture);
    return result;
}

VoxelGiMaterialTable buildVoxelGiMaterials(const BlockAtlasData& atlas) {
    std::array<glm::vec3, static_cast<size_t>(BlockTexture::Count)> colors{};
    for (size_t t = 0; t < colors.size(); ++t) {
        const uint32_t slot = getAtlasTextureIndex(static_cast<BlockTexture>(t));
        glm::vec3 sum(0.0f);
        float weight = 0.0f;
        for (uint32_t y = 0; y < atlas.tileSize; ++y)
            for (uint32_t x = 0; x < atlas.tileSize; ++x) {
                const size_t pixel = (size_t(slot / atlas.tilesPerSide * atlas.tileSize + y) *
                    atlas.texture.width + slot % atlas.tilesPerSide * atlas.tileSize + x) * 4;
                const float alpha = atlas.texture.pixels[pixel + 3] / 255.0f;
                for (int c = 0; c < 3; ++c)
                    sum[c] += voxelGiSrgbToLinear(atlas.texture.pixels[pixel + c] / 255.0f) * alpha;
                weight += alpha;
            }
        colors[t] = weight > 0.0f ? sum / weight : glm::vec3(0.0f);
    }
    VoxelGiMaterialTable result{};
    for (size_t b = 1; b < result.size(); ++b) {
        const auto id = static_cast<BlockId>(b);
        glm::vec3 emissionColor(0.0f);
        for (int f = 0; f < 6; ++f) {
            const auto color = colors[static_cast<size_t>(getFaceTexture(id, static_cast<FaceDir>(f)))];
            result[b].reflectance[f] = glm::min(color, glm::vec3(0.9f));
            emissionColor += color / 6.0f;
        }
        // Chromaticity is independent of reflectance; strength remains the world rule.
        const float peak = std::max({emissionColor.r, emissionColor.g, emissionColor.b});
        if (peak > 0.0f)
            result[b].emission = emissionColor / peak * (getLightEmission(id) / 15.0f);
    }
    return result;
}
