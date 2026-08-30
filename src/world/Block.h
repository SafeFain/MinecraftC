#pragma once

#include <cstdint>
#include <string>
#include <array>
#include <filesystem>
#include <optional>
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
    LIMESTONE    = 89,
    BASALT       = 90,
    TUFF         = 91,
    COARSE_DIRT  = 92,
    MUD          = 93,
    PACKED_ICE   = 94,
    BLACK_SAND   = 95,
    GRANITE      = 96,
    WHITE_BED_FOOT_EAST  = 97,
    WHITE_BED_FOOT_SOUTH = 98,
    WHITE_BED_FOOT_WEST  = 99,
    WHITE_BED_HEAD_NORTH = 100,
    WHITE_BED_HEAD_EAST  = 101,
    WHITE_BED_HEAD_SOUTH = 102,
    WHITE_BED_HEAD_WEST  = 103,
    // Flowing fluids that are vertically falling have a distinct Java-style
    // state.  These are appended so every pre-existing serialized ID remains
    // stable; they are derived and never written to block overrides.
    FALLING_WATER = 104,
    FALLING_LAVA  = 105,
    // Heaven materials are appended so every pre-existing serialized ID
    // remains stable.  They are shared by generated terrain and the creative
    // inventory, while the dimension decides where they naturally appear.
    AETHER_GRASS  = 106,
    AETHER_SOIL   = 107,
    CLOUDSTONE    = 108,
    SUNSTONE      = 109,
    SKYROOT_WOOD  = 110,
    SKYROOT_LEAVES = 111,
    STAR_CRYSTAL  = 112,
    STARFLOWER    = 113,
    CLOUD_BLOOM   = 114,
    GLOWSHROOM    = 115,
    // Architectural states are appended in five contiguous ten-state
    // families: bottom/top slab, then bottom/top stairs in NESW order.
    PLANKS_SLAB_BOTTOM = 116, PLANKS_SLAB_TOP,
    PLANKS_STAIRS_BOTTOM_NORTH, PLANKS_STAIRS_BOTTOM_EAST,
    PLANKS_STAIRS_BOTTOM_SOUTH, PLANKS_STAIRS_BOTTOM_WEST,
    PLANKS_STAIRS_TOP_NORTH, PLANKS_STAIRS_TOP_EAST,
    PLANKS_STAIRS_TOP_SOUTH, PLANKS_STAIRS_TOP_WEST,
    COBBLESTONE_SLAB_BOTTOM, COBBLESTONE_SLAB_TOP,
    COBBLESTONE_STAIRS_BOTTOM_NORTH, COBBLESTONE_STAIRS_BOTTOM_EAST,
    COBBLESTONE_STAIRS_BOTTOM_SOUTH, COBBLESTONE_STAIRS_BOTTOM_WEST,
    COBBLESTONE_STAIRS_TOP_NORTH, COBBLESTONE_STAIRS_TOP_EAST,
    COBBLESTONE_STAIRS_TOP_SOUTH, COBBLESTONE_STAIRS_TOP_WEST,
    TERRACOTTA_SLAB_BOTTOM, TERRACOTTA_SLAB_TOP,
    TERRACOTTA_STAIRS_BOTTOM_NORTH, TERRACOTTA_STAIRS_BOTTOM_EAST,
    TERRACOTTA_STAIRS_BOTTOM_SOUTH, TERRACOTTA_STAIRS_BOTTOM_WEST,
    TERRACOTTA_STAIRS_TOP_NORTH, TERRACOTTA_STAIRS_TOP_EAST,
    TERRACOTTA_STAIRS_TOP_SOUTH, TERRACOTTA_STAIRS_TOP_WEST,
    SUNSTONE_SLAB_BOTTOM, SUNSTONE_SLAB_TOP,
    SUNSTONE_STAIRS_BOTTOM_NORTH, SUNSTONE_STAIRS_BOTTOM_EAST,
    SUNSTONE_STAIRS_BOTTOM_SOUTH, SUNSTONE_STAIRS_BOTTOM_WEST,
    SUNSTONE_STAIRS_TOP_NORTH, SUNSTONE_STAIRS_TOP_EAST,
    SUNSTONE_STAIRS_TOP_SOUTH, SUNSTONE_STAIRS_TOP_WEST,
    CLOUDSTONE_SLAB_BOTTOM, CLOUDSTONE_SLAB_TOP,
    CLOUDSTONE_STAIRS_BOTTOM_NORTH, CLOUDSTONE_STAIRS_BOTTOM_EAST,
    CLOUDSTONE_STAIRS_BOTTOM_SOUTH, CLOUDSTONE_STAIRS_BOTTOM_WEST,
    CLOUDSTONE_STAIRS_TOP_NORTH, CLOUDSTONE_STAIRS_TOP_EAST,
    CLOUDSTONE_STAIRS_TOP_SOUTH, CLOUDSTONE_STAIRS_TOP_WEST,
    // Villager economy blocks are appended to preserve every serialized ID.
    EMERALD_ORE = 166,
    DEEPSLATE_EMERALD_ORE,
    COMPOSTER,
    FLETCHING_TABLE,
    LOOM,
    CAULDRON,
    BLAST_FURNACE,
    SMITHING_TABLE,
    GRINDSTONE,
    COUNT        = 175,
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
    Fluid,
    Bed,
    Slab,
    Stair
};

enum class BedPart : uint8_t { Foot, Head };
enum class BedDirection : uint8_t { North, East, South, West };
enum class ArchitecturalMaterial : uint8_t {
    Planks, Cobblestone, Terracotta, Sunstone, Cloudstone, Count
};
enum class BlockHalf : uint8_t { Bottom, Top };

struct ArchitecturalBlockState {
    ArchitecturalMaterial material = ArchitecturalMaterial::Planks;
    RenderShape shape = RenderShape::Cube;
    BlockHalf half = BlockHalf::Bottom;
    BedDirection direction = BedDirection::North;
};

struct BlockCollisionBox {
    glm::vec3 min{0.0f};
    glm::vec3 max{1.0f};
};

struct BlockCollisionBoxes {
    std::array<BlockCollisionBox, 2> boxes{};
    uint8_t count = 0;
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
    Limestone, Basalt, Tuff, CoarseDirt, Mud, PackedIce, BlackSand, Granite,
    AetherGrassTop, AetherGrassSide, AetherSoil, Cloudstone, Sunstone,
    SkyrootLog, SkyrootLogTop, SkyrootLeaves, StarCrystal, Starflower,
    CloudBloom, Glowshroom,
    EmeraldOre, DeepslateEmeraldOre, Composter, FletchingTable, Loom,
    Cauldron, BlastFurnace, SmithingTable, Grindstone,
    Count
};

// Global registry (defined in Block.cpp)
extern const std::array<BlockProperties, static_cast<size_t>(BlockId::COUNT)> BLOCK_TABLE;

// Quick lookup
inline const BlockProperties& getBlockProps(BlockId id) {
    return BLOCK_TABLE[static_cast<uint8_t>(id)];
}

inline bool isLeafBlock(BlockId id) {
    return id == BlockId::LEAVES || id == BlockId::BIRCH_LEAVES ||
           id == BlockId::SPRUCE_LEAVES || id == BlockId::JUNGLE_LEAVES ||
           id == BlockId::ACACIA_LEAVES || id == BlockId::SKYROOT_LEAVES;
}

inline bool isSolid(BlockId id) {
    return getBlockProps(id).solid;
}

bool isBed(BlockId id);
bool isVillagerWorkstation(BlockId id);
bool decodeBed(BlockId id, BedPart& part, BedDirection& direction);
BlockId bedBlock(BedPart part, BedDirection direction);
glm::ivec3 bedDirectionOffset(BedDirection direction);
BedDirection bedDirectionFromHorizontal(const glm::vec2& direction);
glm::ivec3 bedPartnerOffset(BlockId id);
bool decodeArchitecturalBlock(BlockId id, ArchitecturalBlockState& state);
BlockId slabBlock(ArchitecturalMaterial material, BlockHalf half);
BlockId stairBlock(ArchitecturalMaterial material, BlockHalf half,
                   BedDirection direction);
BlockId architecturalBaseBlock(ArchitecturalMaterial material);
BlockCollisionBoxes blockCollisionBoxes(BlockId id);
float blockCollisionHeight(BlockId id);
inline bool isFullCollisionBlock(BlockId id) {
    const BlockCollisionBoxes boxes = blockCollisionBoxes(id);
    if (boxes.count != 1) return false;
    const BlockCollisionBox& box = boxes.boxes[0];
    return box.min.x <= 0.0f && box.min.y <= 0.0f && box.min.z <= 0.0f &&
           box.max.x >= 1.0f && box.max.y >= 1.0f && box.max.z >= 1.0f;
}
bool pointInsideBlockCollision(BlockId id, float localY);
bool pointInsideBlockCollision(BlockId id, const glm::vec3& localPosition);

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

// Java-style fluid state decoded from the compact BlockId representation.
// amount is 1..8 for a fluid; sources and falling states both carry amount 8.
struct FluidStateInfo {
    bool lava = false;
    uint8_t amount = 0;
    bool source = false;
    bool falling = false;
};

std::optional<FluidStateInfo> decodeFluidState(BlockId id);
bool isFallingFluid(BlockId id);
uint8_t fluidAmount(BlockId id);
uint8_t fluidLevel(BlockId id);
BlockId fluidBlockFromAmount(bool lava, uint8_t amount, bool falling = false);
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
