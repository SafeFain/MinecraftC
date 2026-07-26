#include "renderer/BlockTextureAtlas.h"

#include "debug/Log.h"
#include "debug/OpenGL.h"
#include "world/Block.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace {
constexpr int TILE_SIZE = 16;
constexpr int ATLAS_TILES = 8;
using Tile = std::array<uint8_t, TILE_SIZE * TILE_SIZE * 4>;

uint32_t pixelHash(int x, int y, uint32_t salt) {
    uint32_t h = static_cast<uint32_t>(x) * 0x8da6b343u;
    h ^= static_cast<uint32_t>(y) * 0xd8163841u;
    h ^= salt * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x85ebca6bu;
    return h ^ (h >> 16);
}

Tile material(uint8_t r, uint8_t g, uint8_t b, int variation, uint32_t salt) {
    Tile tile{};
    for (int y = 0; y < TILE_SIZE; ++y) {
        for (int x = 0; x < TILE_SIZE; ++x) {
            int delta = static_cast<int>(pixelHash(x, y, salt) % (variation * 2 + 1)) - variation;
            size_t i = static_cast<size_t>(y * TILE_SIZE + x) * 4;
            tile[i]     = static_cast<uint8_t>(std::clamp<int>(r + delta, 0, 255));
            tile[i + 1] = static_cast<uint8_t>(std::clamp<int>(g + delta, 0, 255));
            tile[i + 2] = static_cast<uint8_t>(std::clamp<int>(b + delta, 0, 255));
            tile[i + 3] = 255;
        }
    }
    return tile;
}

Tile loadTile(const std::string& path, const Tile& fallback) {
    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels || width != TILE_SIZE || height != TILE_SIZE) {
        LOG_WARN("Block texture unavailable or not 16x16: " + path);
        stbi_image_free(pixels);
        return fallback;
    }
    Tile result{};
    std::copy(pixels, pixels + result.size(), result.begin());
    stbi_image_free(pixels);
    return result;
}

void overlay(Tile& base, const Tile& top) {
    for (size_t i = 0; i < base.size(); i += 4) {
        int alpha = top[i + 3];
        for (int c = 0; c < 3; ++c)
            base[i + c] = static_cast<uint8_t>(
                (top[i + c] * alpha + base[i + c] * (255 - alpha)) / 255);
        base[i + 3] = std::max(base[i + 3], top[i + 3]);
    }
}

Tile tint(Tile tile, float r, float g, float b) {
    for (size_t i = 0; i < tile.size(); i += 4) {
        tile[i]     = static_cast<uint8_t>(std::clamp(tile[i] * r, 0.0f, 255.0f));
        tile[i + 1] = static_cast<uint8_t>(std::clamp(tile[i + 1] * g, 0.0f, 255.0f));
        tile[i + 2] = static_cast<uint8_t>(std::clamp(tile[i + 2] * b, 0.0f, 255.0f));
    }
    return tile;
}

Tile leaves(uint8_t r, uint8_t g, uint8_t b, uint32_t salt) {
    Tile tile = material(r, g, b, 24, salt);
    for (int y = 0; y < TILE_SIZE; ++y) {
        for (int x = 0; x < TILE_SIZE; ++x) {
            size_t i = static_cast<size_t>(y * TILE_SIZE + x) * 4;
            if (pixelHash(x, y, salt + 9) % 11 == 0) tile[i + 3] = 0;
            if ((x + y + static_cast<int>(salt)) % 7 == 0) {
                tile[i] = static_cast<uint8_t>(tile[i] * 0.72f);
                tile[i + 1] = static_cast<uint8_t>(tile[i + 1] * 0.72f);
                tile[i + 2] = static_cast<uint8_t>(tile[i + 2] * 0.72f);
            }
        }
    }
    return tile;
}

Tile plant(uint8_t r, uint8_t g, uint8_t b, bool flower) {
    Tile tile{};
    auto put = [&](int x, int y, uint8_t pr, uint8_t pg, uint8_t pb) {
        if (x < 0 || x >= 16 || y < 0 || y >= 16) return;
        size_t i = static_cast<size_t>(y * 16 + x) * 4;
        tile[i] = pr; tile[i + 1] = pg; tile[i + 2] = pb; tile[i + 3] = 255;
    };
    for (int y = 0; y < 14; ++y) {
        int x = 8 + ((y / 3) % 2);
        put(x, y, r, g, b);
        if (y > 3 && y % 3 == 0) {
            put(x - 1, y, r, g, b);
            put(x + 1, y + 1, r, g, b);
        }
    }
    if (flower) {
        put(8, 14, 245, 194, 45);
        put(7, 14, 224, 55, 83); put(9, 14, 224, 55, 83);
        put(8, 13, 224, 55, 83); put(8, 15, 224, 55, 83);
    }
    return tile;
}

Tile workstation(uint8_t r, uint8_t g, uint8_t b, uint32_t salt,
                 uint8_t accentR, uint8_t accentG, uint8_t accentB) {
    Tile tile = material(r, g, b, 12, salt);
    for (int y = 1; y < 15; ++y) {
        for (int x = 1; x < 15; ++x) {
            const bool border = x == 1 || x == 14 || y == 1 || y == 14;
            const bool panel = (x == 5 || x == 10) && y >= 4 && y <= 11;
            if (!border && !panel) continue;
            const size_t i = static_cast<size_t>(y * 16 + x) * 4;
            tile[i] = accentR;
            tile[i + 1] = accentG;
            tile[i + 2] = accentB;
        }
    }
    return tile;
}

Tile wheat(int stage) {
    Tile tile{};
    auto put = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        if (x < 0 || x >= 16 || y < 0 || y >= 16) return;
        const size_t i = static_cast<size_t>(y * 16 + x) * 4;
        tile[i] = r; tile[i + 1] = g; tile[i + 2] = b; tile[i + 3] = 255;
    };
    const int height = 6 + stage * 4;
    const bool mature = stage == 2;
    for (int stem : {3, 7, 11}) {
        for (int y = 1; y < height; ++y)
            put(stem + ((y + stem) % 3 == 0), y,
                mature ? 190 : 75, mature ? 151 : 132, mature ? 38 : 38);
        if (mature) {
            for (int y = height - 5; y < height; ++y) {
                put(stem - 1, y, 221, 180, 54);
                put(stem + 1, y - 1, 205, 157, 40);
            }
        } else {
            put(stem - 1, height - 2, 87, 143, 39);
            put(stem + 1, height - 3, 70, 123, 34);
        }
    }
    return tile;
}

Tile logTop() {
    Tile tile = material(145, 104, 55, 10, 81);
    for (int y = 1; y < 15; ++y) {
        for (int x = 1; x < 15; ++x) {
            int edge = std::min({x, y, 15 - x, 15 - y});
            if (edge == 2 || edge == 5) {
                size_t i = static_cast<size_t>(y * 16 + x) * 4;
                tile[i] = 105; tile[i + 1] = 72; tile[i + 2] = 37;
            }
        }
    }
    return tile;
}
}

BlockTextureAtlas::~BlockTextureAtlas() {
    if (m_texture) GL_CHECK(glDeleteTextures(1, &m_texture));
}

bool BlockTextureAtlas::initialize() {
    stbi_set_flip_vertically_on_load(1);
    const std::string root = "assets/textures/source/";
    std::array<Tile, static_cast<size_t>(BlockTexture::Count)> tiles;

    tiles[static_cast<size_t>(BlockTexture::Dirt)] = material(121, 82, 49, 18, 1);
    tiles[static_cast<size_t>(BlockTexture::Stone)] = material(126, 128, 126, 22, 2);
    tiles[static_cast<size_t>(BlockTexture::GrassTop)] =
        loadTile(root + "grass_top.png", material(82, 150, 55, 18, 3));
    Tile grassSide = tiles[static_cast<size_t>(BlockTexture::Dirt)];
    overlay(grassSide, loadTile(root + "grass_side.png", Tile{}));
    tiles[static_cast<size_t>(BlockTexture::GrassSide)] = grassSide;
    tiles[static_cast<size_t>(BlockTexture::OakLog)] =
        loadTile(root + "oak_log.png", material(91, 65, 38, 18, 4));
    tiles[static_cast<size_t>(BlockTexture::LogTop)] = logTop();
    tiles[static_cast<size_t>(BlockTexture::Leaves)] = leaves(55, 130, 43, 5);
    tiles[static_cast<size_t>(BlockTexture::Sand)] =
        loadTile(root + "sand.png", material(218, 203, 146, 10, 6));
    tiles[static_cast<size_t>(BlockTexture::Bedrock)] = material(54, 54, 58, 35, 7);
    tiles[static_cast<size_t>(BlockTexture::Water)] = material(47, 103, 205, 10, 8);
    tiles[static_cast<size_t>(BlockTexture::Snow)] = material(235, 242, 246, 7, 9);
    tiles[static_cast<size_t>(BlockTexture::Planks)] =
        loadTile(root + "oak_planks.png", material(169, 127, 68, 13, 10));
    tiles[static_cast<size_t>(BlockTexture::Deepslate)] = material(54, 57, 63, 15, 11);
    tiles[static_cast<size_t>(BlockTexture::CactusSide)] = material(53, 128, 55, 13, 12);
    tiles[static_cast<size_t>(BlockTexture::CactusTop)] = material(88, 153, 64, 11, 13);

    const Tile stone = tiles[static_cast<size_t>(BlockTexture::Stone)];
    auto ore = [&](BlockTexture id, const char* file) {
        Tile value = stone;
        overlay(value, loadTile(root + file, Tile{}));
        tiles[static_cast<size_t>(id)] = value;
    };
    ore(BlockTexture::CoalOre, "coal_overlay.png");
    ore(BlockTexture::IronOre, "iron_overlay.png");
    ore(BlockTexture::GoldOre, "gold_overlay.png");
    ore(BlockTexture::DiamondOre, "diamond_overlay.png");

    tiles[static_cast<size_t>(BlockTexture::Lava)] = material(239, 91, 19, 22, 14);
    tiles[static_cast<size_t>(BlockTexture::Ice)] = material(151, 204, 230, 12, 15);
    tiles[static_cast<size_t>(BlockTexture::Gravel)] = material(105, 101, 96, 30, 16);
    tiles[static_cast<size_t>(BlockTexture::Clay)] = material(145, 159, 169, 12, 17);
    tiles[static_cast<size_t>(BlockTexture::RedSand)] =
        loadTile(root + "red_sand.png", material(181, 82, 35, 14, 18));
    tiles[static_cast<size_t>(BlockTexture::Terracotta)] = material(151, 74, 47, 12, 19);
    tiles[static_cast<size_t>(BlockTexture::PodzolTop)] = material(91, 59, 30, 22, 20);
    tiles[static_cast<size_t>(BlockTexture::Moss)] = material(71, 127, 45, 20, 21);
    tiles[static_cast<size_t>(BlockTexture::TallGrass)] = plant(54, 151, 46, false);
    Tile fallbackFlower = plant(55, 139, 43, true);
    tiles[static_cast<size_t>(BlockTexture::Flower)] =
        loadTile(root + "flower.png", fallbackFlower);
    tiles[static_cast<size_t>(BlockTexture::Reeds)] = plant(126, 173, 58, false);

    Tile oak = tiles[static_cast<size_t>(BlockTexture::OakLog)];
    tiles[static_cast<size_t>(BlockTexture::BirchLog)] =
        loadTile(root + "birch_log.png", tint(oak, 1.55f, 1.50f, 1.30f));
    tiles[static_cast<size_t>(BlockTexture::BirchLeaves)] = leaves(93, 157, 54, 22);
    tiles[static_cast<size_t>(BlockTexture::SpruceLog)] =
        loadTile(root + "spruce_log.png", tint(oak, 0.62f, 0.58f, 0.52f));
    tiles[static_cast<size_t>(BlockTexture::SpruceLeaves)] = leaves(35, 91, 61, 23);
    tiles[static_cast<size_t>(BlockTexture::JungleLog)] =
        loadTile(root + "jungle_log.png", tint(oak, 0.80f, 0.72f, 0.62f));
    tiles[static_cast<size_t>(BlockTexture::JungleLeaves)] = leaves(39, 142, 42, 24);
    tiles[static_cast<size_t>(BlockTexture::AcaciaLog)] =
        loadTile(root + "acacia_log.png", tint(oak, 1.18f, 0.72f, 0.52f));
    tiles[static_cast<size_t>(BlockTexture::AcaciaLeaves)] = leaves(84, 133, 45, 25);
    tiles[static_cast<size_t>(BlockTexture::Cobblestone)] = material(112, 112, 108, 34, 31);
    tiles[static_cast<size_t>(BlockTexture::CraftingTable)] =
        workstation(137, 91, 45, 32, 66, 39, 20);
    tiles[static_cast<size_t>(BlockTexture::Furnace)] =
        workstation(91, 91, 88, 33, 30, 28, 27);
    tiles[static_cast<size_t>(BlockTexture::Chest)] =
        workstation(151, 96, 35, 34, 224, 178, 66);
    tiles[static_cast<size_t>(BlockTexture::Torch)] = plant(225, 157, 45, true);
    tiles[static_cast<size_t>(BlockTexture::WhiteWool)] = material(222, 222, 216, 12, 35);
    tiles[static_cast<size_t>(BlockTexture::WhiteBed)] = material(228, 225, 218, 10, 36);
    tiles[static_cast<size_t>(BlockTexture::Farmland)] = material(87, 52, 24, 18, 37);
    tiles[static_cast<size_t>(BlockTexture::WetFarmland)] = material(58, 31, 18, 14, 38);
    tiles[static_cast<size_t>(BlockTexture::WheatYoung)] = wheat(0);
    tiles[static_cast<size_t>(BlockTexture::WheatMiddle)] = wheat(1);
    tiles[static_cast<size_t>(BlockTexture::WheatMature)] = wheat(2);
    tiles[static_cast<size_t>(BlockTexture::OakSapling)] = plant(64, 151, 49, false);
    tiles[static_cast<size_t>(BlockTexture::BirchSapling)] = plant(104, 168, 58, false);
    tiles[static_cast<size_t>(BlockTexture::SpruceSapling)] = plant(43, 105, 65, false);
    tiles[static_cast<size_t>(BlockTexture::JungleSapling)] = plant(48, 166, 41, false);
    tiles[static_cast<size_t>(BlockTexture::AcaciaSapling)] = plant(104, 146, 44, false);

    GL_CHECK(glGenTextures(1, &m_texture));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_texture));

    std::array<std::vector<uint8_t>, static_cast<size_t>(BlockTexture::Count)> levelTiles;
    for (size_t i = 0; i < tiles.size(); ++i)
        levelTiles[i].assign(tiles[i].begin(), tiles[i].end());

    int tileSize = TILE_SIZE;
    for (int level = 0; level <= 4; ++level) {
        const int atlasSize = tileSize * ATLAS_TILES;
        std::vector<uint8_t> atlas(
            static_cast<size_t>(atlasSize * atlasSize * 4), 0);
        for (size_t tileIndex = 0; tileIndex < levelTiles.size(); ++tileIndex) {
            const int tileX = static_cast<int>(tileIndex % ATLAS_TILES);
            const int tileY = static_cast<int>(tileIndex / ATLAS_TILES);
            for (int y = 0; y < tileSize; ++y) {
                for (int x = 0; x < tileSize; ++x) {
                    const size_t src = static_cast<size_t>(y * tileSize + x) * 4;
                    const size_t dst = static_cast<size_t>(
                        ((tileY * tileSize + y) * atlasSize +
                         tileX * tileSize + x) * 4);
                    std::copy_n(levelTiles[tileIndex].data() + src, 4,
                                atlas.data() + dst);
                }
            }
        }
        GL_CHECK(glTexImage2D(GL_TEXTURE_2D, level, GL_SRGB8_ALPHA8,
                             atlasSize, atlasSize, 0, GL_RGBA,
                             GL_UNSIGNED_BYTE, atlas.data()));

        if (tileSize == 1) break;
        const int nextSize = tileSize / 2;
        for (auto& tilePixels : levelTiles) {
            std::vector<uint8_t> next(
                static_cast<size_t>(nextSize * nextSize * 4), 0);
            for (int y = 0; y < nextSize; ++y) {
                for (int x = 0; x < nextSize; ++x) {
                    int alphaSum = 0;
                    int rgbSum[3] = {0, 0, 0};
                    for (int oy = 0; oy < 2; ++oy) {
                        for (int ox = 0; ox < 2; ++ox) {
                            const size_t src = static_cast<size_t>(
                                ((y * 2 + oy) * tileSize + x * 2 + ox) * 4);
                            const int alpha = tilePixels[src + 3];
                            alphaSum += alpha;
                            for (int c = 0; c < 3; ++c)
                                rgbSum[c] += tilePixels[src + c] * alpha;
                        }
                    }
                    const size_t dst = static_cast<size_t>(y * nextSize + x) * 4;
                    next[dst + 3] = static_cast<uint8_t>(alphaSum / 4);
                    if (alphaSum > 0) {
                        for (int c = 0; c < 3; ++c)
                            next[dst + c] = static_cast<uint8_t>(rgbSum[c] / alphaSum);
                    }
                }
            }
            tilePixels = std::move(next);
        }
        tileSize = nextSize;
    }

    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            GL_NEAREST_MIPMAP_LINEAR));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 4));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
    return true;
}

void BlockTextureAtlas::bind() const {
    GL_CHECK(glActiveTexture(GL_TEXTURE0));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_texture));
}
