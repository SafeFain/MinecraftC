#include "world/Block.h"
#include "core/AssetStore.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_map>

namespace {
constexpr size_t TEXTURE_COUNT = static_cast<size_t>(BlockTexture::Count);
std::array<uint8_t, TEXTURE_COUNT> g_atlasIndices = [] {
    std::array<uint8_t, TEXTURE_COUNT> result{};
    for (size_t i = 0; i < result.size(); ++i) result[i] = static_cast<uint8_t>(i);
    return result;
}();
std::array<std::array<BlockTexture, FACE_COUNT>, static_cast<size_t>(BlockId::COUNT)>
    g_definitionFaces{};
bool g_definitionFacesReady = false;

constexpr std::array<const char*, TEXTURE_COUNT> TEXTURE_ASSET_NAMES = {{
    "dirt", "grass_top", "grass_side", "stone", "oak_log", "oak_log_top",
    "leaves", "sand", "bedrock", "water", "snow", "oak_planks",
    "deepslate", "cactus_side", "cactus_top", "coal_ore", "iron_ore",
    "gold_ore", "diamond_ore", "lava", "ice", "gravel", "clay",
    "red_sand", "terracotta", "podzol_top", "moss", "tall_grass",
    "flower", "reeds", "birch_log", "birch_leaves", "spruce_log",
    "spruce_leaves", "jungle_log", "jungle_leaves", "acacia_log",
    "acacia_leaves", "cobblestone", "crafting_table", "furnace", "chest",
    "torch", "white_wool", "white_bed", "farmland", "wet_farmland",
    "wheat_young", "wheat_middle", "wheat_mature", "oak_sapling",
    "birch_sapling", "spruce_sapling", "jungle_sapling", "acacia_sapling",
    "snow_layer", "fire", "glass", "tnt", "obsidian", "dandelion",
    "blue_orchid", "allium", "oxeye_daisy", "sunflower_bottom",
    "sunflower_top", "cloud", "limestone", "basalt", "tuff",
    "coarse_dirt", "mud", "packed_ice", "black_sand", "granite",
    "aether_grass_top", "aether_grass_side", "aether_soil", "cloudstone",
    "sunstone", "skyroot_log", "skyroot_log_top", "skyroot_leaves",
    "star_crystal", "starflower", "cloud_bloom", "glowshroom",
    "emerald_ore", "deepslate_emerald_ore", "composter",
    "fletching_table", "loom", "cauldron", "blast_furnace",
    "smithing_table", "grindstone"
}};

const std::unordered_map<std::string, BlockTexture>& textureNames() {
    static const std::unordered_map<std::string, BlockTexture> names = [] {
        std::unordered_map<std::string, BlockTexture> result;
        for (size_t i = 0; i < TEXTURE_ASSET_NAMES.size(); ++i)
            result.emplace(TEXTURE_ASSET_NAMES[i], static_cast<BlockTexture>(i));
        return result;
    }();
    return names;
}

const std::unordered_map<std::string, BlockId>& blockNames() {
    static const std::unordered_map<std::string, BlockId> names = [] {
        std::unordered_map<std::string, BlockId> result;
        for (size_t i = 1; i < static_cast<size_t>(BlockId::COUNT); ++i) {
            std::string key;
            for (unsigned char character : BLOCK_TABLE[i].name) {
                if (std::isalnum(character))
                    key.push_back(static_cast<char>(std::tolower(character)));
                else if (!key.empty() && key.back() != '_') key.push_back('_');
            }
            result.emplace(key, static_cast<BlockId>(i));
        }
        result["wood"] = BlockId::WOOD;
        result["planks"] = BlockId::PLANKS;
        result["cactus_block"] = BlockId::CACTUS_BLOCK;
        result["podzol"] = BlockId::PODZOL;
        result["wet_farmland"] = BlockId::FARMLAND_7;
        result["wheat_young"] = BlockId::WHEAT_0;
        result["wheat_middle"] = BlockId::WHEAT_4;
        result["wheat_mature"] = BlockId::WHEAT_7;
        result["poppy"] = BlockId::FLOWER;
        for (uint8_t level = 1; level <= 7; ++level) {
            result["flowing_water_" + std::to_string(level)] = fluidBlock(false, level);
            result["flowing_lava_" + std::to_string(level)] = fluidBlock(true, level);
        }
        return result;
    }();
    return names;
}

std::string readTextFile(const std::filesystem::path& path) {
    try{return AssetStore::readTextPath(path);}catch(const std::exception&){return {};}
}

bool quotedField(const std::string& object, const char* field, std::string& value) {
    const std::regex expression(std::string("\\\"") + field +
                                "\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
    std::smatch match;
    if (!std::regex_search(object, match, expression)) return false;
    value = match[1].str();
    return true;
}
}

// ── Block properties table ────────────────────────────────────────────

const std::array<BlockProperties, static_cast<size_t>(BlockId::COUNT)> BLOCK_TABLE = {{
    { BlockId::AIR,          "Air",          glm::vec3(0.0f, 0.0f, 0.0f), false, false },
    { BlockId::GRASS,        "Grass",        glm::vec3(0.34f, 0.68f, 0.24f), true, false },
    { BlockId::DIRT,         "Dirt",         glm::vec3(0.56f, 0.37f, 0.18f), true, false },
    { BlockId::STONE,        "Stone",        glm::vec3(0.50f, 0.50f, 0.50f), true, false },
    { BlockId::WOOD,         "Wood",         glm::vec3(0.55f, 0.40f, 0.20f), true, false },
    { BlockId::LEAVES,       "Leaves",       glm::vec3(0.15f, 0.55f, 0.15f), true, false,
      RenderShape::Cube, RenderLayer::Cutout, 1.0f },
    { BlockId::SAND,         "Sand",         glm::vec3(0.90f, 0.84f, 0.60f), true, false },
    { BlockId::BEDROCK,      "Bedrock",      glm::vec3(0.20f, 0.20f, 0.20f), true, false },
    { BlockId::WATER,        "Water",        glm::vec3(0.20f, 0.40f, 0.90f), false, true,
      RenderShape::Fluid, RenderLayer::Translucent, 0.62f },
    { BlockId::SNOW,         "Snow",         glm::vec3(0.95f, 0.95f, 0.95f), true, false },
    { BlockId::PLANKS,       "Planks",       glm::vec3(0.70f, 0.55f, 0.30f), true, false },
    { BlockId::DEEPSLATE,    "Deepslate",    glm::vec3(0.25f, 0.25f, 0.27f), true, false },
    { BlockId::CACTUS_BLOCK, "Cactus",       glm::vec3(0.33f, 0.55f, 0.27f), true, false },
    { BlockId::COAL_ORE,     "Coal Ore",     glm::vec3(0.15f, 0.15f, 0.15f), true, false },
    { BlockId::IRON_ORE,     "Iron Ore",     glm::vec3(0.65f, 0.55f, 0.45f), true, false },
    { BlockId::GOLD_ORE,     "Gold Ore",     glm::vec3(0.85f, 0.75f, 0.25f), true, false },
    { BlockId::DIAMOND_ORE,  "Diamond Ore",  glm::vec3(0.40f, 0.80f, 0.85f), true, false },
    { BlockId::LAVA,         "Lava",         glm::vec3(0.95f, 0.50f, 0.10f), false, true,
      RenderShape::Fluid, RenderLayer::Translucent, 0.86f },
    { BlockId::ICE,          "Ice",          glm::vec3(0.70f, 0.85f, 0.95f), true, false,
      RenderShape::Cube, RenderLayer::Translucent, 0.72f },
    { BlockId::GRAVEL,       "Gravel",        glm::vec3(0.43f, 0.42f, 0.40f), true, false },
    { BlockId::CLAY,         "Clay",          glm::vec3(0.55f, 0.60f, 0.63f), true, false },
    { BlockId::RED_SAND,     "Red Sand",      glm::vec3(0.73f, 0.33f, 0.13f), true, false },
    { BlockId::TERRACOTTA,   "Terracotta",    glm::vec3(0.60f, 0.30f, 0.20f), true, false },
    { BlockId::PODZOL,       "Podzol",        glm::vec3(0.35f, 0.22f, 0.10f), true, false },
    { BlockId::MOSS,         "Moss",          glm::vec3(0.25f, 0.50f, 0.16f), true, false },
    { BlockId::TALL_GRASS,   "Tall Grass",    glm::vec3(0.30f, 0.68f, 0.20f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::FLOWER,       "Flower",        glm::vec3(0.92f, 0.35f, 0.48f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::REEDS,        "Reeds",         glm::vec3(0.50f, 0.72f, 0.24f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::BIRCH_WOOD,   "Birch Wood",    glm::vec3(0.82f, 0.78f, 0.62f), true, false },
    { BlockId::BIRCH_LEAVES, "Birch Leaves",  glm::vec3(0.38f, 0.66f, 0.22f), true, false,
      RenderShape::Cube, RenderLayer::Cutout, 1.0f },
    { BlockId::SPRUCE_WOOD,  "Spruce Wood",   glm::vec3(0.32f, 0.22f, 0.12f), true, false },
    { BlockId::SPRUCE_LEAVES,"Spruce Leaves", glm::vec3(0.12f, 0.40f, 0.22f), true, false,
      RenderShape::Cube, RenderLayer::Cutout, 1.0f },
    { BlockId::JUNGLE_WOOD,  "Jungle Wood",   glm::vec3(0.46f, 0.30f, 0.14f), true, false },
    { BlockId::JUNGLE_LEAVES,"Jungle Leaves", glm::vec3(0.10f, 0.58f, 0.15f), true, false,
      RenderShape::Cube, RenderLayer::Cutout, 1.0f },
    { BlockId::ACACIA_WOOD,  "Acacia Wood",   glm::vec3(0.62f, 0.30f, 0.14f), true, false },
    { BlockId::ACACIA_LEAVES,"Acacia Leaves", glm::vec3(0.34f, 0.55f, 0.17f), true, false,
      RenderShape::Cube, RenderLayer::Cutout, 1.0f },
    { BlockId::COBBLESTONE,   "Cobblestone",   glm::vec3(0.43f), true, false },
    { BlockId::CRAFTING_TABLE,"Crafting Table",glm::vec3(0.55f, 0.36f, 0.18f), true, false },
    { BlockId::FURNACE,       "Furnace",       glm::vec3(0.38f), true, false },
    { BlockId::CHEST,         "Chest",         glm::vec3(0.58f, 0.36f, 0.12f), true, false },
    { BlockId::TORCH,         "Torch",         glm::vec3(0.95f, 0.72f, 0.25f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::WHITE_WOOL,    "White Wool",    glm::vec3(0.92f), true, false },
    { BlockId::WHITE_BED,     "White Bed", glm::vec3(0.88f), true, true,
      RenderShape::Bed, RenderLayer::Opaque, 1.0f },
    { BlockId::FARMLAND,      "Farmland",      glm::vec3(0.35f, 0.20f, 0.08f), true, false },
    { BlockId::WHEAT_0,       "Wheat",         glm::vec3(0.38f, 0.52f, 0.14f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::WHEAT_1,       "Wheat",         glm::vec3(0.42f, 0.56f, 0.14f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::WHEAT_2,       "Wheat",         glm::vec3(0.48f, 0.60f, 0.14f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::WHEAT_3,       "Wheat",         glm::vec3(0.55f, 0.64f, 0.14f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::WHEAT_4,       "Wheat",         glm::vec3(0.62f, 0.67f, 0.16f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::WHEAT_5,       "Wheat",         glm::vec3(0.72f, 0.69f, 0.18f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::WHEAT_6,       "Wheat",         glm::vec3(0.80f, 0.70f, 0.20f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::WHEAT_7,       "Wheat",         glm::vec3(0.88f, 0.72f, 0.22f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::FARMLAND_1,    "Wet Farmland",  glm::vec3(0.27f, 0.13f, 0.05f), true, false },
    { BlockId::FARMLAND_2,    "Wet Farmland",  glm::vec3(0.27f, 0.13f, 0.05f), true, false },
    { BlockId::FARMLAND_3,    "Wet Farmland",  glm::vec3(0.27f, 0.13f, 0.05f), true, false },
    { BlockId::FARMLAND_4,    "Wet Farmland",  glm::vec3(0.27f, 0.13f, 0.05f), true, false },
    { BlockId::FARMLAND_5,    "Wet Farmland",  glm::vec3(0.27f, 0.13f, 0.05f), true, false },
    { BlockId::FARMLAND_6,    "Wet Farmland",  glm::vec3(0.27f, 0.13f, 0.05f), true, false },
    { BlockId::FARMLAND_7,    "Wet Farmland",  glm::vec3(0.27f, 0.13f, 0.05f), true, false },
    { BlockId::OAK_SAPLING,   "Oak Sapling",   glm::vec3(0.25f, 0.62f, 0.18f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::BIRCH_SAPLING, "Birch Sapling", glm::vec3(0.46f, 0.70f, 0.22f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::SPRUCE_SAPLING,"Spruce Sapling",glm::vec3(0.18f, 0.46f, 0.25f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::JUNGLE_SAPLING,"Jungle Sapling",glm::vec3(0.20f, 0.68f, 0.16f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::ACACIA_SAPLING,"Acacia Sapling",glm::vec3(0.42f, 0.60f, 0.16f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::SNOW_LAYER,     "Snow Layer",     glm::vec3(0.95f), false, true,
      RenderShape::SnowLayer, RenderLayer::Opaque, 1.0f },
    { BlockId::FIRE,           "Fire",           glm::vec3(1.0f, 0.38f, 0.06f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::GLASS,          "Glass",          glm::vec3(0.82f, 0.92f, 0.96f), true, true,
      RenderShape::Cube, RenderLayer::Translucent, 0.45f },
    { BlockId::TNT,            "TNT",            glm::vec3(0.78f, 0.18f, 0.12f), true, false },
    { BlockId::OBSIDIAN,       "Obsidian",       glm::vec3(0.12f, 0.08f, 0.18f), true, false },
    { BlockId::DANDELION,      "Dandelion",      glm::vec3(0.95f, 0.80f, 0.12f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::BLUE_ORCHID,    "Blue Orchid",    glm::vec3(0.25f, 0.65f, 0.88f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::ALLIUM,         "Allium",         glm::vec3(0.68f, 0.36f, 0.78f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::OXEYE_DAISY,    "Oxeye Daisy",    glm::vec3(0.92f, 0.92f, 0.82f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::SUNFLOWER_BOTTOM,"Sunflower",     glm::vec3(0.45f, 0.65f, 0.18f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::SUNFLOWER_TOP,  "Sunflower Top",  glm::vec3(0.95f, 0.72f, 0.12f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
#define FLUID_ENTRY(id, label, r, g, b, a) \
    { id, label, glm::vec3(r, g, b), false, true, RenderShape::Fluid, \
      RenderLayer::Translucent, a }
    FLUID_ENTRY(BlockId::FLOWING_WATER_1, "Flowing Water", .20f, .40f, .90f, .62f),
    FLUID_ENTRY(BlockId::FLOWING_WATER_2, "Flowing Water", .20f, .40f, .90f, .62f),
    FLUID_ENTRY(BlockId::FLOWING_WATER_3, "Flowing Water", .20f, .40f, .90f, .62f),
    FLUID_ENTRY(BlockId::FLOWING_WATER_4, "Flowing Water", .20f, .40f, .90f, .62f),
    FLUID_ENTRY(BlockId::FLOWING_WATER_5, "Flowing Water", .20f, .40f, .90f, .62f),
    FLUID_ENTRY(BlockId::FLOWING_WATER_6, "Flowing Water", .20f, .40f, .90f, .62f),
    FLUID_ENTRY(BlockId::FLOWING_WATER_7, "Flowing Water", .20f, .40f, .90f, .62f),
    FLUID_ENTRY(BlockId::FLOWING_LAVA_1, "Flowing Lava", .95f, .50f, .10f, .86f),
    FLUID_ENTRY(BlockId::FLOWING_LAVA_2, "Flowing Lava", .95f, .50f, .10f, .86f),
    FLUID_ENTRY(BlockId::FLOWING_LAVA_3, "Flowing Lava", .95f, .50f, .10f, .86f),
    FLUID_ENTRY(BlockId::FLOWING_LAVA_4, "Flowing Lava", .95f, .50f, .10f, .86f),
    FLUID_ENTRY(BlockId::FLOWING_LAVA_5, "Flowing Lava", .95f, .50f, .10f, .86f),
    FLUID_ENTRY(BlockId::FLOWING_LAVA_6, "Flowing Lava", .95f, .50f, .10f, .86f),
    FLUID_ENTRY(BlockId::FLOWING_LAVA_7, "Flowing Lava", .95f, .50f, .10f, .86f),
#undef FLUID_ENTRY
    { BlockId::LIMESTONE,   "Limestone",   glm::vec3(.72f, .71f, .64f), true, false },
    { BlockId::BASALT,      "Basalt",      glm::vec3(.16f, .17f, .18f), true, false },
    { BlockId::TUFF,        "Tuff",        glm::vec3(.31f, .36f, .33f), true, false },
    { BlockId::COARSE_DIRT, "Coarse Dirt", glm::vec3(.43f, .30f, .17f), true, false },
    { BlockId::MUD,         "Mud",         glm::vec3(.24f, .22f, .20f), true, false },
    { BlockId::PACKED_ICE,  "Packed Ice",  glm::vec3(.42f, .67f, .84f), true, false },
    { BlockId::BLACK_SAND,  "Black Sand",  glm::vec3(.17f, .16f, .17f), true, false },
    { BlockId::GRANITE,     "Granite",     glm::vec3(.57f, .37f, .31f), true, false },
    { BlockId::WHITE_BED_FOOT_EAST,  "White Bed Foot East",  glm::vec3(.88f), true, true,
      RenderShape::Bed, RenderLayer::Opaque, 1.0f },
    { BlockId::WHITE_BED_FOOT_SOUTH, "White Bed Foot South", glm::vec3(.88f), true, true,
      RenderShape::Bed, RenderLayer::Opaque, 1.0f },
    { BlockId::WHITE_BED_FOOT_WEST,  "White Bed Foot West",  glm::vec3(.88f), true, true,
      RenderShape::Bed, RenderLayer::Opaque, 1.0f },
    { BlockId::WHITE_BED_HEAD_NORTH, "White Bed Head North", glm::vec3(.88f), true, true,
      RenderShape::Bed, RenderLayer::Opaque, 1.0f },
    { BlockId::WHITE_BED_HEAD_EAST,  "White Bed Head East",  glm::vec3(.88f), true, true,
      RenderShape::Bed, RenderLayer::Opaque, 1.0f },
    { BlockId::WHITE_BED_HEAD_SOUTH, "White Bed Head South", glm::vec3(.88f), true, true,
      RenderShape::Bed, RenderLayer::Opaque, 1.0f },
    { BlockId::WHITE_BED_HEAD_WEST,  "White Bed Head West",  glm::vec3(.88f), true, true,
      RenderShape::Bed, RenderLayer::Opaque, 1.0f },
    { BlockId::FALLING_WATER, "Falling Water", glm::vec3(.20f, .40f, .90f), false, true,
      RenderShape::Fluid, RenderLayer::Translucent, .62f },
    { BlockId::FALLING_LAVA, "Falling Lava", glm::vec3(.95f, .50f, .10f), false, true,
      RenderShape::Fluid, RenderLayer::Translucent, .86f },
    { BlockId::AETHER_GRASS, "Aether Grass", glm::vec3(.42f, .78f, .40f), true, false },
    { BlockId::AETHER_SOIL, "Aether Soil", glm::vec3(.50f, .36f, .22f), true, false },
    { BlockId::CLOUDSTONE, "Cloudstone", glm::vec3(.70f, .78f, .82f), true, false },
    { BlockId::SUNSTONE, "Sunstone", glm::vec3(.88f, .71f, .35f), true, false },
    { BlockId::SKYROOT_WOOD, "Skyroot Wood", glm::vec3(.62f, .48f, .30f), true, false },
    { BlockId::SKYROOT_LEAVES, "Skyroot Leaves", glm::vec3(.42f, .72f, .38f), true, false,
      RenderShape::Cube, RenderLayer::Cutout, 1.0f },
    { BlockId::STAR_CRYSTAL, "Star Crystal", glm::vec3(.30f, .78f, .92f), true, true,
      RenderShape::Cube, RenderLayer::Translucent, .68f },
    { BlockId::STARFLOWER, "Starflower", glm::vec3(.64f, .52f, .96f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::CLOUD_BLOOM, "Cloud Bloom", glm::vec3(.92f, .94f, .98f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::GLOWSHROOM, "Glowshroom", glm::vec3(.50f, .88f, .78f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
#define ARCH_FAMILY(prefix, label, color) \
    {BlockId::prefix##_SLAB_BOTTOM, label " Slab", color, true, true, RenderShape::Slab, RenderLayer::Opaque, 1.0f}, \
    {BlockId::prefix##_SLAB_TOP, label " Slab", color, true, true, RenderShape::Slab, RenderLayer::Opaque, 1.0f}, \
    {BlockId::prefix##_STAIRS_BOTTOM_NORTH, label " Stairs", color, true, true, RenderShape::Stair, RenderLayer::Opaque, 1.0f}, \
    {BlockId::prefix##_STAIRS_BOTTOM_EAST, label " Stairs", color, true, true, RenderShape::Stair, RenderLayer::Opaque, 1.0f}, \
    {BlockId::prefix##_STAIRS_BOTTOM_SOUTH, label " Stairs", color, true, true, RenderShape::Stair, RenderLayer::Opaque, 1.0f}, \
    {BlockId::prefix##_STAIRS_BOTTOM_WEST, label " Stairs", color, true, true, RenderShape::Stair, RenderLayer::Opaque, 1.0f}, \
    {BlockId::prefix##_STAIRS_TOP_NORTH, label " Stairs", color, true, true, RenderShape::Stair, RenderLayer::Opaque, 1.0f}, \
    {BlockId::prefix##_STAIRS_TOP_EAST, label " Stairs", color, true, true, RenderShape::Stair, RenderLayer::Opaque, 1.0f}, \
    {BlockId::prefix##_STAIRS_TOP_SOUTH, label " Stairs", color, true, true, RenderShape::Stair, RenderLayer::Opaque, 1.0f}, \
    {BlockId::prefix##_STAIRS_TOP_WEST, label " Stairs", color, true, true, RenderShape::Stair, RenderLayer::Opaque, 1.0f}
    ARCH_FAMILY(PLANKS, "Planks", glm::vec3(.70f, .55f, .30f)),
    ARCH_FAMILY(COBBLESTONE, "Cobblestone", glm::vec3(.43f)),
    ARCH_FAMILY(TERRACOTTA, "Terracotta", glm::vec3(.60f, .30f, .20f)),
    ARCH_FAMILY(SUNSTONE, "Sunstone", glm::vec3(.88f, .71f, .35f)),
    ARCH_FAMILY(CLOUDSTONE, "Cloudstone", glm::vec3(.70f, .78f, .82f)),
#undef ARCH_FAMILY
    {BlockId::EMERALD_ORE, "Emerald Ore", glm::vec3(.18f,.74f,.42f), true, false},
    {BlockId::DEEPSLATE_EMERALD_ORE, "Deepslate Emerald Ore", glm::vec3(.16f,.58f,.36f), true, false},
    {BlockId::COMPOSTER, "Composter", glm::vec3(.48f,.31f,.15f), true, false},
    {BlockId::FLETCHING_TABLE, "Fletching Table", glm::vec3(.66f,.55f,.35f), true, false},
    {BlockId::LOOM, "Loom", glm::vec3(.67f,.57f,.41f), true, false},
    {BlockId::CAULDRON, "Cauldron", glm::vec3(.26f,.27f,.28f), true, false},
    {BlockId::BLAST_FURNACE, "Blast Furnace", glm::vec3(.31f,.32f,.33f), true, false},
    {BlockId::SMITHING_TABLE, "Smithing Table", glm::vec3(.25f,.34f,.35f), true, false},
    {BlockId::GRINDSTONE, "Grindstone", glm::vec3(.49f,.48f,.44f), true, false},
}};

BlockTexture getFaceTexture(BlockId id, FaceDir face) {
    if (g_definitionFacesReady) {
        const BlockTexture defined = g_definitionFaces[static_cast<size_t>(id)]
                                                       [static_cast<size_t>(face)];
        if (defined != BlockTexture::Count) return defined;
    }
    ArchitecturalBlockState architectural;
    if (decodeArchitecturalBlock(id, architectural))
        return getFaceTexture(architecturalBaseBlock(architectural.material), face);
    if (isBed(id)) return BlockTexture::WhiteBed;
    const bool top = face == FaceDir::TOP;
    const bool bottom = face == FaceDir::BOTTOM;
    switch (id) {
        case BlockId::GRASS:
            return top ? BlockTexture::GrassTop :
                   bottom ? BlockTexture::Dirt : BlockTexture::GrassSide;
        case BlockId::DIRT:          return BlockTexture::Dirt;
        case BlockId::STONE:         return BlockTexture::Stone;
        case BlockId::WOOD:          return top || bottom ? BlockTexture::LogTop : BlockTexture::OakLog;
        case BlockId::LEAVES:        return BlockTexture::Leaves;
        case BlockId::SAND:          return BlockTexture::Sand;
        case BlockId::BEDROCK:       return BlockTexture::Bedrock;
        case BlockId::WATER:         return BlockTexture::Water;
        case BlockId::SNOW:          return BlockTexture::Snow;
        case BlockId::PLANKS:        return BlockTexture::Planks;
        case BlockId::DEEPSLATE:     return BlockTexture::Deepslate;
        case BlockId::CACTUS_BLOCK:  return top || bottom ? BlockTexture::CactusTop : BlockTexture::CactusSide;
        case BlockId::COAL_ORE:      return BlockTexture::CoalOre;
        case BlockId::IRON_ORE:      return BlockTexture::IronOre;
        case BlockId::GOLD_ORE:      return BlockTexture::GoldOre;
        case BlockId::DIAMOND_ORE:   return BlockTexture::DiamondOre;
        case BlockId::EMERALD_ORE:   return BlockTexture::EmeraldOre;
        case BlockId::DEEPSLATE_EMERALD_ORE:
            return BlockTexture::DeepslateEmeraldOre;
        case BlockId::LAVA:          return BlockTexture::Lava;
        case BlockId::ICE:           return BlockTexture::Ice;
        case BlockId::GRAVEL:        return BlockTexture::Gravel;
        case BlockId::CLAY:          return BlockTexture::Clay;
        case BlockId::RED_SAND:      return BlockTexture::RedSand;
        case BlockId::TERRACOTTA:    return BlockTexture::Terracotta;
        case BlockId::PODZOL:        return top ? BlockTexture::PodzolTop : BlockTexture::Dirt;
        case BlockId::MOSS:          return BlockTexture::Moss;
        case BlockId::TALL_GRASS:    return BlockTexture::TallGrass;
        case BlockId::FLOWER:        return BlockTexture::Flower;
        case BlockId::REEDS:         return BlockTexture::Reeds;
        case BlockId::BIRCH_WOOD:    return top || bottom ? BlockTexture::LogTop : BlockTexture::BirchLog;
        case BlockId::BIRCH_LEAVES:  return BlockTexture::BirchLeaves;
        case BlockId::SPRUCE_WOOD:   return top || bottom ? BlockTexture::LogTop : BlockTexture::SpruceLog;
        case BlockId::SPRUCE_LEAVES: return BlockTexture::SpruceLeaves;
        case BlockId::JUNGLE_WOOD:   return top || bottom ? BlockTexture::LogTop : BlockTexture::JungleLog;
        case BlockId::JUNGLE_LEAVES: return BlockTexture::JungleLeaves;
        case BlockId::ACACIA_WOOD:   return top || bottom ? BlockTexture::LogTop : BlockTexture::AcaciaLog;
        case BlockId::ACACIA_LEAVES: return BlockTexture::AcaciaLeaves;
        case BlockId::COBBLESTONE:   return BlockTexture::Cobblestone;
        case BlockId::CRAFTING_TABLE:return BlockTexture::CraftingTable;
        case BlockId::FURNACE:       return BlockTexture::Furnace;
        case BlockId::CHEST:         return BlockTexture::Chest;
        case BlockId::TORCH:         return BlockTexture::Torch;
        case BlockId::WHITE_WOOL:    return BlockTexture::WhiteWool;
        case BlockId::WHITE_BED:     return BlockTexture::WhiteBed;
        case BlockId::FARMLAND:      return BlockTexture::Farmland;
        case BlockId::FARMLAND_1: case BlockId::FARMLAND_2:
        case BlockId::FARMLAND_3: case BlockId::FARMLAND_4:
        case BlockId::FARMLAND_5: case BlockId::FARMLAND_6:
        case BlockId::FARMLAND_7:    return BlockTexture::WetFarmland;
        case BlockId::WHEAT_0:
        case BlockId::WHEAT_1:
        case BlockId::WHEAT_2:       return BlockTexture::WheatYoung;
        case BlockId::WHEAT_3:
        case BlockId::WHEAT_4:
        case BlockId::WHEAT_5:       return BlockTexture::WheatMiddle;
        case BlockId::WHEAT_6:
        case BlockId::WHEAT_7:       return BlockTexture::WheatMature;
        case BlockId::OAK_SAPLING:   return BlockTexture::OakSapling;
        case BlockId::BIRCH_SAPLING: return BlockTexture::BirchSapling;
        case BlockId::SPRUCE_SAPLING:return BlockTexture::SpruceSapling;
        case BlockId::JUNGLE_SAPLING:return BlockTexture::JungleSapling;
        case BlockId::ACACIA_SAPLING:return BlockTexture::AcaciaSapling;
        case BlockId::SNOW_LAYER:    return BlockTexture::SnowLayer;
        case BlockId::FIRE:          return BlockTexture::Fire;
        case BlockId::GLASS:         return BlockTexture::Glass;
        case BlockId::TNT:           return BlockTexture::Tnt;
        case BlockId::OBSIDIAN:      return BlockTexture::Obsidian;
        case BlockId::DANDELION:     return BlockTexture::Dandelion;
        case BlockId::BLUE_ORCHID:   return BlockTexture::BlueOrchid;
        case BlockId::ALLIUM:        return BlockTexture::Allium;
        case BlockId::OXEYE_DAISY:   return BlockTexture::OxeyeDaisy;
        case BlockId::SUNFLOWER_BOTTOM: return BlockTexture::SunflowerBottom;
        case BlockId::SUNFLOWER_TOP: return BlockTexture::SunflowerTop;
        case BlockId::COMPOSTER: return BlockTexture::Composter;
        case BlockId::FLETCHING_TABLE: return BlockTexture::FletchingTable;
        case BlockId::LOOM: return BlockTexture::Loom;
        case BlockId::CAULDRON: return BlockTexture::Cauldron;
        case BlockId::BLAST_FURNACE: return BlockTexture::BlastFurnace;
        case BlockId::SMITHING_TABLE: return BlockTexture::SmithingTable;
        case BlockId::GRINDSTONE: return BlockTexture::Grindstone;
        case BlockId::FLOWING_WATER_1: case BlockId::FLOWING_WATER_2:
        case BlockId::FLOWING_WATER_3: case BlockId::FLOWING_WATER_4:
        case BlockId::FLOWING_WATER_5: case BlockId::FLOWING_WATER_6:
        case BlockId::FLOWING_WATER_7: return BlockTexture::Water;
        case BlockId::FLOWING_LAVA_1: case BlockId::FLOWING_LAVA_2:
        case BlockId::FLOWING_LAVA_3: case BlockId::FLOWING_LAVA_4:
        case BlockId::FLOWING_LAVA_5: case BlockId::FLOWING_LAVA_6:
        case BlockId::FLOWING_LAVA_7: return BlockTexture::Lava;
        case BlockId::FALLING_WATER: return BlockTexture::Water;
        case BlockId::FALLING_LAVA:  return BlockTexture::Lava;
        case BlockId::LIMESTONE:      return BlockTexture::Limestone;
        case BlockId::BASALT:         return BlockTexture::Basalt;
        case BlockId::TUFF:           return BlockTexture::Tuff;
        case BlockId::COARSE_DIRT:     return BlockTexture::CoarseDirt;
        case BlockId::MUD:             return BlockTexture::Mud;
        case BlockId::PACKED_ICE:      return BlockTexture::PackedIce;
        case BlockId::BLACK_SAND:      return BlockTexture::BlackSand;
        case BlockId::GRANITE:         return BlockTexture::Granite;
        case BlockId::AETHER_GRASS:    return top ? BlockTexture::AetherGrassTop :
                                             bottom ? BlockTexture::AetherSoil :
                                             BlockTexture::AetherGrassSide;
        case BlockId::AETHER_SOIL:     return BlockTexture::AetherSoil;
        case BlockId::CLOUDSTONE:      return BlockTexture::Cloudstone;
        case BlockId::SUNSTONE:        return BlockTexture::Sunstone;
        case BlockId::SKYROOT_WOOD:    return top || bottom ? BlockTexture::SkyrootLogTop :
                                             BlockTexture::SkyrootLog;
        case BlockId::SKYROOT_LEAVES:  return BlockTexture::SkyrootLeaves;
        case BlockId::STAR_CRYSTAL:    return BlockTexture::StarCrystal;
        case BlockId::STARFLOWER:      return BlockTexture::Starflower;
        case BlockId::CLOUD_BLOOM:     return BlockTexture::CloudBloom;
        case BlockId::GLOWSHROOM:      return BlockTexture::Glowshroom;
        default:                     return BlockTexture::Dirt;
    }
}

uint8_t getAtlasTextureIndex(BlockTexture texture) {
    const size_t index = static_cast<size_t>(texture);
    return index < g_atlasIndices.size() ? g_atlasIndices[index] : 0;
}

const char* getBlockTextureAssetName(BlockTexture texture) {
    const size_t index = static_cast<size_t>(texture);
    return index < TEXTURE_ASSET_NAMES.size() ? TEXTURE_ASSET_NAMES[index] : "dirt";
}

uint8_t getFaceTextureIndex(BlockId id, FaceDir face) {
    return getAtlasTextureIndex(getFaceTexture(id, face));
}

bool loadTextureAssetDefinitions(const std::filesystem::path& atlasMetadataPath,
                                 const std::filesystem::path& blockDefinitionsPath,
                                 const std::filesystem::path& itemDefinitionsPath) {
    const std::string atlas = readTextFile(atlasMetadataPath);
    const std::string blocks = readTextFile(blockDefinitionsPath);
    const std::string items = readTextFile(itemDefinitionsPath);
    if (atlas.empty() || blocks.empty() || items.empty()) return false;

    std::array<int, TEXTURE_COUNT> requested{};
    requested.fill(-1);
    const std::regex atlasEntry(
        "\\\"([^\\\"]+)\\\"\\s*:\\s*\\{[^{}]*\\\"index\\\"\\s*:\\s*([0-9]+)[^{}]*\\}");
    for (std::sregex_iterator it(atlas.begin(), atlas.end(), atlasEntry), end;
         it != end; ++it) {
        const auto texture = textureNames().find((*it)[1].str());
        if (texture == textureNames().end()) continue;
        const int index = std::stoi((*it)[2].str());
        if (index >= 0 && index < static_cast<int>(TEXTURE_COUNT))
            requested[static_cast<size_t>(texture->second)] = index;
    }
    std::array<bool, TEXTURE_COUNT> used{};
    for (size_t texture = 0; texture < TEXTURE_COUNT; ++texture) {
        if (requested[texture] >= 0 && !used[requested[texture]]) {
            g_atlasIndices[texture] = static_cast<uint8_t>(requested[texture]);
            used[requested[texture]] = true;
        }
    }
    size_t next = 0;
    for (size_t texture = 0; texture < TEXTURE_COUNT; ++texture) {
        if (requested[texture] >= 0) continue;
        while (next < used.size() && used[next]) ++next;
        if (next < used.size()) {
            g_atlasIndices[texture] = static_cast<uint8_t>(next);
            used[next] = true;
        }
    }

    for (auto& faces : g_definitionFaces) faces.fill(BlockTexture::Count);
    const std::regex blockEntry("\\\"([a-z0-9_]+)\\\"\\s*:\\s*\\{([^{}]*)\\}");
    for (std::sregex_iterator it(blocks.begin(), blocks.end(), blockEntry), end;
         it != end; ++it) {
        const auto block = blockNames().find((*it)[1].str());
        if (block == blockNames().end()) continue;
        auto& faces = g_definitionFaces[static_cast<size_t>(block->second)];
        const std::string object = (*it)[2].str();
        std::string value;
        if (quotedField(object, "all", value)) {
            const auto texture = textureNames().find(value);
            if (texture != textureNames().end()) faces.fill(texture->second);
        }
        auto apply = [&](const char* field, std::initializer_list<FaceDir> targets) {
            std::string name;
            if (!quotedField(object, field, name)) return;
            const auto texture = textureNames().find(name);
            if (texture == textureNames().end()) return;
            for (FaceDir face : targets) faces[static_cast<size_t>(face)] = texture->second;
        };
        apply("top", {FaceDir::TOP});
        apply("bottom", {FaceDir::BOTTOM});
        apply("side", {FaceDir::FRONT, FaceDir::BACK, FaceDir::RIGHT, FaceDir::LEFT});
    }
    g_definitionFacesReady = true;
    return true;
}

bool isFarmland(BlockId id) {
    return id == BlockId::FARMLAND ||
           (id >= BlockId::FARMLAND_1 && id <= BlockId::FARMLAND_7);
}

uint8_t farmlandMoisture(BlockId id) {
    if (id == BlockId::FARMLAND) return 0;
    if (id >= BlockId::FARMLAND_1 && id <= BlockId::FARMLAND_7)
        return static_cast<uint8_t>(id) - static_cast<uint8_t>(BlockId::FARMLAND_1) + 1;
    return 0;
}

BlockId farmlandForMoisture(uint8_t moisture) {
    if (moisture == 0) return BlockId::FARMLAND;
    moisture = std::min<uint8_t>(moisture, 7);
    return static_cast<BlockId>(static_cast<uint8_t>(BlockId::FARMLAND_1) + moisture - 1);
}

bool isSapling(BlockId id) {
    return id >= BlockId::OAK_SAPLING && id <= BlockId::ACACIA_SAPLING;
}

bool isBed(BlockId id) {
    return id == BlockId::WHITE_BED ||
           (id >= BlockId::WHITE_BED_FOOT_EAST &&
            id <= BlockId::WHITE_BED_HEAD_WEST);
}

bool isVillagerWorkstation(BlockId id) {
    return id >= BlockId::COMPOSTER && id <= BlockId::GRINDSTONE;
}

bool decodeBed(BlockId id, BedPart& part, BedDirection& direction) {
    switch (id) {
        case BlockId::WHITE_BED:
            part = BedPart::Foot; direction = BedDirection::North; return true;
        case BlockId::WHITE_BED_FOOT_EAST:
            part = BedPart::Foot; direction = BedDirection::East; return true;
        case BlockId::WHITE_BED_FOOT_SOUTH:
            part = BedPart::Foot; direction = BedDirection::South; return true;
        case BlockId::WHITE_BED_FOOT_WEST:
            part = BedPart::Foot; direction = BedDirection::West; return true;
        case BlockId::WHITE_BED_HEAD_NORTH:
            part = BedPart::Head; direction = BedDirection::North; return true;
        case BlockId::WHITE_BED_HEAD_EAST:
            part = BedPart::Head; direction = BedDirection::East; return true;
        case BlockId::WHITE_BED_HEAD_SOUTH:
            part = BedPart::Head; direction = BedDirection::South; return true;
        case BlockId::WHITE_BED_HEAD_WEST:
            part = BedPart::Head; direction = BedDirection::West; return true;
        default: return false;
    }
}

BlockId bedBlock(BedPart part, BedDirection direction) {
    if (part == BedPart::Foot) {
        switch (direction) {
            case BedDirection::North: return BlockId::WHITE_BED;
            case BedDirection::East: return BlockId::WHITE_BED_FOOT_EAST;
            case BedDirection::South: return BlockId::WHITE_BED_FOOT_SOUTH;
            case BedDirection::West: return BlockId::WHITE_BED_FOOT_WEST;
        }
    }
    switch (direction) {
        case BedDirection::North: return BlockId::WHITE_BED_HEAD_NORTH;
        case BedDirection::East: return BlockId::WHITE_BED_HEAD_EAST;
        case BedDirection::South: return BlockId::WHITE_BED_HEAD_SOUTH;
        case BedDirection::West: return BlockId::WHITE_BED_HEAD_WEST;
    }
    return BlockId::WHITE_BED_HEAD_NORTH;
}

glm::ivec3 bedDirectionOffset(BedDirection direction) {
    switch (direction) {
        case BedDirection::North: return {0, 0, -1};
        case BedDirection::East: return {1, 0, 0};
        case BedDirection::South: return {0, 0, 1};
        case BedDirection::West: return {-1, 0, 0};
    }
    return {0, 0, -1};
}

BedDirection bedDirectionFromHorizontal(const glm::vec2& direction) {
    if (std::abs(direction.x) > std::abs(direction.y))
        return direction.x >= 0.0f ? BedDirection::East : BedDirection::West;
    return direction.y >= 0.0f ? BedDirection::South : BedDirection::North;
}

glm::ivec3 bedPartnerOffset(BlockId id) {
    BedPart part = BedPart::Foot;
    BedDirection direction = BedDirection::North;
    if (!decodeBed(id, part, direction)) return {0, 0, 0};
    const glm::ivec3 towardHead = bedDirectionOffset(direction);
    return part == BedPart::Foot ? towardHead : -towardHead;
}

namespace {
constexpr uint8_t ARCHITECTURAL_FIRST =
    static_cast<uint8_t>(BlockId::PLANKS_SLAB_BOTTOM);
constexpr uint8_t ARCHITECTURAL_STRIDE = 10;
}

bool decodeArchitecturalBlock(BlockId id, ArchitecturalBlockState& state) {
    const uint8_t raw = static_cast<uint8_t>(id);
    if (raw < ARCHITECTURAL_FIRST ||
        raw >= static_cast<uint8_t>(BlockId::COUNT)) return false;
    const uint8_t offset = raw - ARCHITECTURAL_FIRST;
    const uint8_t material = offset / ARCHITECTURAL_STRIDE;
    const uint8_t local = offset % ARCHITECTURAL_STRIDE;
    if (material >= static_cast<uint8_t>(ArchitecturalMaterial::Count))
        return false;
    state.material = static_cast<ArchitecturalMaterial>(material);
    if (local < 2) {
        state.shape = RenderShape::Slab;
        state.half = local == 0 ? BlockHalf::Bottom : BlockHalf::Top;
        state.direction = BedDirection::North;
    } else {
        state.shape = RenderShape::Stair;
        state.half = local < 6 ? BlockHalf::Bottom : BlockHalf::Top;
        state.direction = static_cast<BedDirection>((local - 2) % 4);
    }
    return true;
}

BlockId slabBlock(ArchitecturalMaterial material, BlockHalf half) {
    const uint8_t raw = ARCHITECTURAL_FIRST +
        static_cast<uint8_t>(material) * ARCHITECTURAL_STRIDE +
        (half == BlockHalf::Top ? 1 : 0);
    return static_cast<BlockId>(raw);
}

BlockId stairBlock(ArchitecturalMaterial material, BlockHalf half,
                   BedDirection direction) {
    const uint8_t raw = ARCHITECTURAL_FIRST +
        static_cast<uint8_t>(material) * ARCHITECTURAL_STRIDE + 2 +
        (half == BlockHalf::Top ? 4 : 0) + static_cast<uint8_t>(direction);
    return static_cast<BlockId>(raw);
}

BlockId architecturalBaseBlock(ArchitecturalMaterial material) {
    switch (material) {
        case ArchitecturalMaterial::Planks: return BlockId::PLANKS;
        case ArchitecturalMaterial::Cobblestone: return BlockId::COBBLESTONE;
        case ArchitecturalMaterial::Terracotta: return BlockId::TERRACOTTA;
        case ArchitecturalMaterial::Sunstone: return BlockId::SUNSTONE;
        case ArchitecturalMaterial::Cloudstone: return BlockId::CLOUDSTONE;
        default: return BlockId::PLANKS;
    }
}

BlockCollisionBoxes blockCollisionBoxes(BlockId id) {
    BlockCollisionBoxes result;
    const BlockProperties& props = getBlockProps(id);
    if (!props.solid) return result;
    ArchitecturalBlockState state;
    if (decodeArchitecturalBlock(id, state)) {
        if (state.shape == RenderShape::Slab) {
            result.count = 1;
            result.boxes[0] = state.half == BlockHalf::Bottom
                ? BlockCollisionBox{{0, 0, 0}, {1, .5f, 1}}
                : BlockCollisionBox{{0, .5f, 0}, {1, 1, 1}};
            return result;
        }
        result.count = 2;
        result.boxes[0] = state.half == BlockHalf::Bottom
            ? BlockCollisionBox{{0, 0, 0}, {1, .5f, 1}}
            : BlockCollisionBox{{0, .5f, 0}, {1, 1, 1}};
        glm::vec3 min(0.0f), max(1.0f);
        if (state.half == BlockHalf::Bottom) min.y = .5f;
        else max.y = .5f;
        switch (state.direction) {
            case BedDirection::North: max.z = .5f; break;
            case BedDirection::East: min.x = .5f; break;
            case BedDirection::South: min.z = .5f; break;
            case BedDirection::West: max.x = .5f; break;
        }
        result.boxes[1] = {min, max};
        return result;
    }
    result.count = 1;
    const float height = isBed(id) ? 9.0f / 16.0f : 1.0f;
    result.boxes[0] = {{0, 0, 0}, {1, height, 1}};
    return result;
}

float blockCollisionHeight(BlockId id) {
    const BlockCollisionBoxes boxes = blockCollisionBoxes(id);
    float height = 0.0f;
    for (uint8_t i = 0; i < boxes.count; ++i)
        height = std::max(height, boxes.boxes[i].max.y);
    return height;
}

bool pointInsideBlockCollision(BlockId id, float localY) {
    const BlockCollisionBoxes boxes = blockCollisionBoxes(id);
    for (uint8_t i = 0; i < boxes.count; ++i)
        if (localY >= boxes.boxes[i].min.y &&
            localY <= boxes.boxes[i].max.y) return true;
    return false;
}

bool pointInsideBlockCollision(BlockId id, const glm::vec3& localPosition) {
    const BlockCollisionBoxes boxes = blockCollisionBoxes(id);
    for (uint8_t i = 0; i < boxes.count; ++i) {
        const BlockCollisionBox& box = boxes.boxes[i];
        if (localPosition.x >= box.min.x && localPosition.x <= box.max.x &&
            localPosition.y >= box.min.y && localPosition.y <= box.max.y &&
            localPosition.z >= box.min.z && localPosition.z <= box.max.z)
            return true;
    }
    return false;
}

bool isWater(BlockId id) {
    return id == BlockId::WATER ||
           (id >= BlockId::FLOWING_WATER_1 && id <= BlockId::FLOWING_WATER_7) ||
           id == BlockId::FALLING_WATER;
}

bool isLava(BlockId id) {
    return id == BlockId::LAVA ||
           (id >= BlockId::FLOWING_LAVA_1 && id <= BlockId::FLOWING_LAVA_7) ||
           id == BlockId::FALLING_LAVA;
}

uint8_t getLightEmission(BlockId id) {
    if (id == BlockId::TORCH) return 14;
    if (id == BlockId::STAR_CRYSTAL) return 8;
    if (id == BlockId::STARFLOWER) return 5;
    if (id == BlockId::CLOUD_BLOOM) return 4;
    if (id == BlockId::GLOWSHROOM) return 6;
    return id == BlockId::FIRE || isLava(id) ? 15 : 0;
}

uint8_t getLightDampening(BlockId id) {
    if (id == BlockId::AIR || id == BlockId::GLASS || isBed(id) ||
        getBlockProps(id).shape == RenderShape::Cross ||
        getBlockProps(id).shape == RenderShape::Slab ||
        getBlockProps(id).shape == RenderShape::Stair) return 0;
    if (id == BlockId::LEAVES || id == BlockId::BIRCH_LEAVES ||
        id == BlockId::SPRUCE_LEAVES || id == BlockId::JUNGLE_LEAVES ||
        id == BlockId::ACACIA_LEAVES || id == BlockId::SNOW_LAYER ||
        id == BlockId::SKYROOT_LEAVES || isLava(id)) return 1;
    if (isWater(id) || id == BlockId::ICE) return 2;
    return getBlockProps(id).transparent ? 0 : 15;
}

std::optional<FluidStateInfo> decodeFluidState(BlockId id) {
    if (id == BlockId::WATER) return FluidStateInfo{false, 8, true, false};
    if (id == BlockId::LAVA) return FluidStateInfo{true, 8, true, false};
    if (id >= BlockId::FLOWING_WATER_1 && id <= BlockId::FLOWING_WATER_7)
        return FluidStateInfo{false, static_cast<uint8_t>(8 -
            (static_cast<uint8_t>(id) - static_cast<uint8_t>(BlockId::FLOWING_WATER_1) + 1)),
            false, false};
    if (id >= BlockId::FLOWING_LAVA_1 && id <= BlockId::FLOWING_LAVA_7)
        return FluidStateInfo{true, static_cast<uint8_t>(8 -
            (static_cast<uint8_t>(id) - static_cast<uint8_t>(BlockId::FLOWING_LAVA_1) + 1)),
            false, false};
    if (id == BlockId::FALLING_WATER)
        return FluidStateInfo{false, 8, false, true};
    if (id == BlockId::FALLING_LAVA)
        return FluidStateInfo{true, 8, false, true};
    return std::nullopt;
}

bool isFallingFluid(BlockId id) {
    return id == BlockId::FALLING_WATER || id == BlockId::FALLING_LAVA;
}

uint8_t fluidAmount(BlockId id) {
    const auto state = decodeFluidState(id);
    return state ? state->amount : 0;
}

uint8_t fluidLevel(BlockId id) {
    if (!isFluid(id)) return 0;
    if (id == BlockId::WATER || id == BlockId::LAVA) return 0;
    if (isFallingFluid(id)) return 8;
    if (id >= BlockId::FLOWING_WATER_1 && id <= BlockId::FLOWING_WATER_7)
        return static_cast<uint8_t>(id) - static_cast<uint8_t>(BlockId::FLOWING_WATER_1) + 1;
    if (id >= BlockId::FLOWING_LAVA_1 && id <= BlockId::FLOWING_LAVA_7)
        return static_cast<uint8_t>(id) - static_cast<uint8_t>(BlockId::FLOWING_LAVA_1) + 1;
    return 0;
}

BlockId fluidBlockFromAmount(bool lava, uint8_t amount, bool falling) {
    if (amount == 0) return BlockId::AIR;
    amount = std::min<uint8_t>(amount, 8);
    if (amount == 8) {
        if (falling) return lava ? BlockId::FALLING_LAVA : BlockId::FALLING_WATER;
        return lava ? BlockId::LAVA : BlockId::WATER;
    }
    const uint8_t first = static_cast<uint8_t>(
        lava ? BlockId::FLOWING_LAVA_1 : BlockId::FLOWING_WATER_1);
    const uint8_t level = static_cast<uint8_t>(8 - amount);
    return static_cast<BlockId>(first + level - 1);
}

BlockId fluidBlock(bool lava, uint8_t level) {
    if (level == 0) return lava ? BlockId::LAVA : BlockId::WATER;
    if (level >= 8) return lava ? BlockId::FALLING_LAVA : BlockId::FALLING_WATER;
    const uint8_t first = static_cast<uint8_t>(
        lava ? BlockId::FLOWING_LAVA_1 : BlockId::FLOWING_WATER_1);
    return static_cast<BlockId>(first + level - 1);
}

float fluidSurfaceHeight(BlockId id) {
    const uint8_t amount = fluidAmount(id);
    return amount == 0 ? 0.0f : static_cast<float>(amount) / 9.0f;
}

bool isSunflower(BlockId id) {
    return id == BlockId::SUNFLOWER_BOTTOM || id == BlockId::SUNFLOWER_TOP;
}

bool isFlower(BlockId id) {
    return id == BlockId::FLOWER || id == BlockId::DANDELION ||
           id == BlockId::BLUE_ORCHID || id == BlockId::ALLIUM ||
           id == BlockId::OXEYE_DAISY || id == BlockId::STARFLOWER ||
           id == BlockId::CLOUD_BLOOM || id == BlockId::GLOWSHROOM ||
           isSunflower(id);
}

bool isReplaceableByFluid(BlockId id) {
    // Java's flowing-fluid passability admits non-collision blocks, with
    // sugar cane (REEDS) as the notable plant exception.  Crops and torches
    // are destroyed by the incoming fluid; solid farmland and beds are not.
    if (id == BlockId::REEDS) return false;
    if (id == BlockId::AIR || id == BlockId::FIRE || id == BlockId::SNOW_LAYER ||
        id == BlockId::TORCH || id == BlockId::TALL_GRASS || isFlower(id) ||
        isSapling(id)) return true;
    return id >= BlockId::WHEAT_0 && id <= BlockId::WHEAT_7;
}

uint8_t fireEncouragement(BlockId id) {
    if (isBed(id)) return 30;
    ArchitecturalBlockState architectural;
    if (decodeArchitecturalBlock(id,architectural) &&
        architectural.material==ArchitecturalMaterial::Planks) return 5;
    switch (id) {
        case BlockId::WOOD: case BlockId::BIRCH_WOOD:
        case BlockId::SPRUCE_WOOD: case BlockId::JUNGLE_WOOD:
        case BlockId::ACACIA_WOOD: case BlockId::SKYROOT_WOOD: return 5;
        case BlockId::PLANKS: case BlockId::CRAFTING_TABLE:
        case BlockId::CHEST: case BlockId::COMPOSTER:
        case BlockId::FLETCHING_TABLE: case BlockId::LOOM: return 5;
        case BlockId::LEAVES: case BlockId::BIRCH_LEAVES:
        case BlockId::SPRUCE_LEAVES: case BlockId::JUNGLE_LEAVES:
        case BlockId::ACACIA_LEAVES: case BlockId::SKYROOT_LEAVES:
        case BlockId::WHITE_WOOL: return 30;
        case BlockId::OAK_SAPLING: case BlockId::BIRCH_SAPLING:
        case BlockId::SPRUCE_SAPLING: case BlockId::JUNGLE_SAPLING:
        case BlockId::ACACIA_SAPLING: case BlockId::TALL_GRASS:
        case BlockId::FLOWER: case BlockId::WHEAT_0:
        case BlockId::WHEAT_1: case BlockId::WHEAT_2:
        case BlockId::WHEAT_3: case BlockId::WHEAT_4:
        case BlockId::WHEAT_5: case BlockId::WHEAT_6:
        case BlockId::WHEAT_7: case BlockId::DANDELION:
        case BlockId::BLUE_ORCHID: case BlockId::ALLIUM:
        case BlockId::OXEYE_DAISY: case BlockId::SUNFLOWER_BOTTOM:
        case BlockId::SUNFLOWER_TOP: case BlockId::STARFLOWER:
        case BlockId::CLOUD_BLOOM: case BlockId::GLOWSHROOM: return 60;
        case BlockId::TNT: return 100;
        default: return 0;
    }
}

uint8_t burnOdds(BlockId id) {
    const uint8_t encouragement = fireEncouragement(id);
    if (encouragement == 0) return 0;
    if (encouragement == 5) {
        switch (id) {
            case BlockId::WOOD: case BlockId::BIRCH_WOOD:
            case BlockId::SPRUCE_WOOD: case BlockId::JUNGLE_WOOD:
            case BlockId::ACACIA_WOOD: case BlockId::SKYROOT_WOOD: return 5;
            default: return 20;
        }
    }
    return encouragement == 30 ? 60 : 100;
}

bool shouldRenderCubeFace(BlockId current, BlockId neighbor) {
    if (current == BlockId::AIR) return false;
    const auto& currentProps = getBlockProps(current);
    if (currentProps.shape != RenderShape::Cube) return false;
    if (neighbor == BlockId::AIR) return true;

    const auto& neighborProps = getBlockProps(neighbor);
    if (neighborProps.shape != RenderShape::Cube) return true;
    if (!currentProps.solid)
        return !neighborProps.solid && neighbor != current;
    if (!neighborProps.solid) return true;

    // Preserve the opaque surface behind ice/leaves. Without this interface
    // face, gaps or alpha in the non-opaque material reveal missing terrain
    // geometry and look like an x-ray into caves below.
    return currentProps.layer == RenderLayer::Opaque &&
           neighborProps.layer != RenderLayer::Opaque;
}

// ── Face direction offsets ────────────────────────────────────────────

const std::array<glm::ivec3, 6> FACE_OFFSETS = {{
    { 0,  1,  0},   // TOP
    { 0, -1,  0},   // BOTTOM
    { 0,  0, -1},   // FRONT
    { 0,  0,  1},   // BACK
    { 1,  0,  0},   // RIGHT
    {-1,  0,  0},   // LEFT
}};

// ── Unit cube corners (8 vertices of 1×1×1 cube at origin) ────────────

const std::array<glm::vec3, 8> CUBE_CORNERS = {{
    {0, 0, 0},  // 0: left-bottom-front
    {1, 0, 0},  // 1: right-bottom-front
    {1, 0, 1},  // 2: right-bottom-back
    {0, 0, 1},  // 3: left-bottom-back
    {0, 1, 0},  // 4: left-top-front
    {1, 1, 0},  // 5: right-top-front
    {1, 1, 1},  // 6: right-top-back
    {0, 1, 1},  // 7: left-top-back
}};

// ── Face vertex indices (6 faces × 6 indices for 2 triangles) ─────────
// Winding: CCW from outside the cube (matches the renderer convention with
// standard lookAt view matrix which has det=-1, flipping to screen-CCW)

const std::array<std::array<int, 6>, 6> FACE_INDICES = {{
    {4, 6, 7, 4, 5, 6},   // TOP
    {0, 2, 1, 0, 3, 2},   // BOTTOM
    {0, 5, 4, 0, 1, 5},   // FRONT
    {3, 6, 2, 3, 7, 6},   // BACK
    {1, 6, 5, 1, 2, 6},   // RIGHT
    {0, 7, 3, 0, 4, 7},   // LEFT
}};

// ── Wireframe cube (12 line segments = 24 vertices) ───────────────────

const std::array<glm::vec3, 24> WIRE_CUBE_VERTICES = {{
    // Bottom face
    {0,0,0}, {1,0,0},   {1,0,0}, {1,0,1},
    {1,0,1}, {0,0,1},   {0,0,1}, {0,0,0},
    // Top face
    {0,1,0}, {1,1,0},   {1,1,0}, {1,1,1},
    {1,1,1}, {0,1,1},   {0,1,1}, {0,1,0},
    // Vertical edges
    {0,0,0}, {0,1,0},   {1,0,0}, {1,1,0},
    {1,0,1}, {1,1,1},   {0,0,1}, {0,1,1},
}};
