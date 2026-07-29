#pragma once

#include <cstdint>
#include <string>
#include <array>
#include <filesystem>
#include <glm/glm.hpp>

// ── Block ID enum ─────────────────────────────────────────────────────

enum class BlockId : uint8_t {
    AIR          = 0,
    GRASS        = 1,
    DIRT         = 2,
    STONE        = 3,
    WOOD         = 4,
    LEAVES       = 5,
    SAND         = 6,
    BEDROCK      = 7,
    WATER        = 8,
    SNOW         = 9,
    PLANKS       = 10,
    DEEPSLATE    = 11,
    CACTUS_BLOCK = 12,
    COAL_ORE     = 13,
    IRON_ORE     = 14,
    GOLD_ORE     = 15,
    DIAMOND_ORE  = 16,
    LAVA         = 17,
    ICE          = 18,
    GRAVEL       = 19,
    CLAY         = 20,
    RED_SAND     = 21,
    TERRACOTTA   = 22,
    PODZOL       = 23,
    MOSS         = 24,
    TALL_GRASS   = 25,
    FLOWER       = 26,
    REEDS        = 27,
    BIRCH_WOOD   = 28,
    BIRCH_LEAVES = 29,
    SPRUCE_WOOD  = 30,
    SPRUCE_LEAVES= 31,
    JUNGLE_WOOD  = 32,
    JUNGLE_LEAVES= 33,
    ACACIA_WOOD  = 34,
    ACACIA_LEAVES= 35,
    COBBLESTONE  = 36,
    CRAFTING_TABLE = 37,
    FURNACE      = 38,
    CHEST        = 39,
    TORCH        = 40,
    WHITE_WOOL   = 41,
    WHITE_BED    = 42,
    FARMLAND     = 43,
    WHEAT_0      = 44,
    WHEAT_1      = 45,
    WHEAT_2      = 46,
    WHEAT_3      = 47,
    WHEAT_4      = 48,
    WHEAT_5      = 49,
    WHEAT_6      = 50,
    WHEAT_7      = 51,
    FARMLAND_1   = 52,
    FARMLAND_2   = 53,
    FARMLAND_3   = 54,
    FARMLAND_4   = 55,
    FARMLAND_5   = 56,
    FARMLAND_6   = 57,
    FARMLAND_7   = 58,
    OAK_SAPLING  = 59,
    BIRCH_SAPLING = 60,
    SPRUCE_SAPLING = 61,
    JUNGLE_SAPLING = 62,
    ACACIA_SAPLING = 63,
    SNOW_LAYER   = 64,
    FIRE         = 65,
    GLASS        = 66,
    TNT          = 67,
    OBSIDIAN     = 68,
    DANDELION    = 69,
    BLUE_ORCHID  = 70,
    ALLIUM       = 71,
    OXEYE_DAISY  = 72,
    SUNFLOWER_BOTTOM = 73,
    SUNFLOWER_TOP = 74,
    FLOWING_WATER_1 = 75,
    FLOWING_WATER_2 = 76,
    FLOWING_WATER_3 = 77,
    FLOWING_WATER_4 = 78,
    FLOWING_WATER_5 = 79,
    FLOWING_WATER_6 = 80,
    FLOWING_WATER_7 = 81,
    FLOWING_LAVA_1 = 82,
    FLOWING_LAVA_2 = 83,
    FLOWING_LAVA_3 = 84,
    FLOWING_LAVA_4 = 85,
    FLOWING_LAVA_5 = 86,
    FLOWING_LAVA_6 = 87,
    FLOWING_LAVA_7 = 88,
    COUNT        = 89,
    POPPY        = FLOWER
};

// ── Face direction ────────────────────────────────────────────────────

enum class FaceDir : uint8_t {
    TOP    = 0,
    BOTTOM = 1,
    FRONT  = 2,
    BACK   = 3,
    RIGHT  = 4,
    LEFT   = 5
};

constexpr int FACE_COUNT = 6;

// ── Block properties ──────────────────────────────────────────────────

enum class RenderShape : uint8_t {
    Cube,
    Cross,
    SnowLayer,
    Fluid
};

enum class RenderLayer : uint8_t {
    Opaque,
    Cutout,
    Translucent
};

struct BlockProperties {
    BlockId   id;
    std::string name;
    glm::vec3 color;       // base RGB (0..1)
    bool      solid;
    bool      transparent;
    RenderShape shape = RenderShape::Cube;
    RenderLayer layer = RenderLayer::Opaque;
    float alpha = 1.0f;
};

// Lighting is deliberately independent from render transparency.  For
// example, leaves and water transmit light but attenuate it.
uint8_t getLightEmission(BlockId id);
uint8_t getLightDampening(BlockId id);

// Material tiles in the dynamically sized shared block atlas.
enum class BlockTexture : uint8_t {
    Dirt, GrassTop, GrassSide, Stone, OakLog, LogTop, Leaves, Sand,
    Bedrock, Water, Snow, Planks, Deepslate, CactusSide, CactusTop,
    CoalOre, IronOre, GoldOre, DiamondOre, Lava, Ice, Gravel, Clay,
    RedSand, Terracotta, PodzolTop, Moss, TallGrass, Flower, Reeds,
    BirchLog, BirchLeaves, SpruceLog, SpruceLeaves, JungleLog,
    JungleLeaves, AcaciaLog, AcaciaLeaves,
    Cobblestone, CraftingTable, Furnace, Chest, Torch, WhiteWool,
    WhiteBed, Farmland, WetFarmland, WheatYoung, WheatMiddle, WheatMature,
    OakSapling, BirchSapling, SpruceSapling, JungleSapling, AcaciaSapling,
    SnowLayer, Fire, Glass, Tnt, Obsidian, Dandelion, BlueOrchid, Allium,
    OxeyeDaisy, SunflowerBottom, SunflowerTop, Cloud,
    Count
};

// Global registry (defined in Block.cpp)
extern const std::array<BlockProperties, static_cast<size_t>(BlockId::COUNT)> BLOCK_TABLE;

// Quick lookup
inline const BlockProperties& getBlockProps(BlockId id) {
    return BLOCK_TABLE[static_cast<uint8_t>(id)];
}

inline bool isSolid(BlockId id) {
    return getBlockProps(id).solid;
}

BlockTexture getFaceTexture(BlockId id, FaceDir face);
uint8_t getAtlasTextureIndex(BlockTexture texture);
uint8_t getFaceTextureIndex(BlockId id, FaceDir face);
const char* getBlockTextureAssetName(BlockTexture texture);
bool loadTextureAssetDefinitions(
    const std::filesystem::path& atlasMetadataPath,
    const std::filesystem::path& blockDefinitionsPath,
    const std::filesystem::path& itemDefinitionsPath);

bool isFarmland(BlockId id);
uint8_t farmlandMoisture(BlockId id);
BlockId farmlandForMoisture(uint8_t moisture);
bool isSapling(BlockId id);
bool isWater(BlockId id);
bool isLava(BlockId id);
inline bool isFluid(BlockId id) { return isWater(id) || isLava(id); }
uint8_t fluidLevel(BlockId id);
BlockId fluidBlock(bool lava, uint8_t level);
float fluidSurfaceHeight(BlockId id);
bool isReplaceableByFluid(BlockId id);
bool isFlower(BlockId id);
bool isSunflower(BlockId id);
uint8_t fireEncouragement(BlockId id);
uint8_t burnOdds(BlockId id);
inline bool isFlammable(BlockId id) { return fireEncouragement(id) > 0; }
bool shouldRenderCubeFace(BlockId current, BlockId neighbor);

// ── Face direction offset vectors ─────────────────────────────────────

extern const std::array<glm::ivec3, 6> FACE_OFFSETS;

// ── Unit cube geometry ────────────────────────────────────────────────
// 8 corners of a 1×1×1 cube at origin
extern const std::array<glm::vec3, 8> CUBE_CORNERS;

// 6 faces × 6 vertex indices (2 triangles per face, CCW winding from outside)
extern const std::array<std::array<int, 6>, 6> FACE_INDICES;

// Wireframe cube — 12 line segments = 24 vertices
extern const std::array<glm::vec3, 24> WIRE_CUBE_VERTICES;
