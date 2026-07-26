#include "world/Block.h"

#include <algorithm>

// ── Block properties table ────────────────────────────────────────────

const std::array<BlockProperties, static_cast<size_t>(BlockId::COUNT)> BLOCK_TABLE = {{
    { BlockId::AIR,          "Air",          glm::vec3(0.0f, 0.0f, 0.0f), false, false },
    { BlockId::GRASS,        "Grass",        glm::vec3(0.34f, 0.68f, 0.24f), true, false },
    { BlockId::DIRT,         "Dirt",         glm::vec3(0.56f, 0.37f, 0.18f), true, false },
    { BlockId::STONE,        "Stone",        glm::vec3(0.50f, 0.50f, 0.50f), true, false },
    { BlockId::WOOD,         "Wood",         glm::vec3(0.55f, 0.40f, 0.20f), true, false },
    { BlockId::LEAVES,       "Leaves",       glm::vec3(0.15f, 0.55f, 0.15f), true, false,
      RenderShape::Cube, RenderLayer::Translucent, 0.86f },
    { BlockId::SAND,         "Sand",         glm::vec3(0.90f, 0.84f, 0.60f), true, false },
    { BlockId::BEDROCK,      "Bedrock",      glm::vec3(0.20f, 0.20f, 0.20f), true, false },
    { BlockId::WATER,        "Water",        glm::vec3(0.20f, 0.40f, 0.90f), false, true,
      RenderShape::Cube, RenderLayer::Translucent, 0.62f },
    { BlockId::SNOW,         "Snow",         glm::vec3(0.95f, 0.95f, 0.95f), true, false },
    { BlockId::PLANKS,       "Planks",       glm::vec3(0.70f, 0.55f, 0.30f), true, false },
    { BlockId::DEEPSLATE,    "Deepslate",    glm::vec3(0.25f, 0.25f, 0.27f), true, false },
    { BlockId::CACTUS_BLOCK, "Cactus",       glm::vec3(0.33f, 0.55f, 0.27f), true, false },
    { BlockId::COAL_ORE,     "Coal Ore",     glm::vec3(0.15f, 0.15f, 0.15f), true, false },
    { BlockId::IRON_ORE,     "Iron Ore",     glm::vec3(0.65f, 0.55f, 0.45f), true, false },
    { BlockId::GOLD_ORE,     "Gold Ore",     glm::vec3(0.85f, 0.75f, 0.25f), true, false },
    { BlockId::DIAMOND_ORE,  "Diamond Ore",  glm::vec3(0.40f, 0.80f, 0.85f), true, false },
    { BlockId::LAVA,         "Lava",         glm::vec3(0.95f, 0.50f, 0.10f), false, true,
      RenderShape::Cube, RenderLayer::Translucent, 0.86f },
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
      RenderShape::Cube, RenderLayer::Translucent, 0.86f },
    { BlockId::SPRUCE_WOOD,  "Spruce Wood",   glm::vec3(0.32f, 0.22f, 0.12f), true, false },
    { BlockId::SPRUCE_LEAVES,"Spruce Leaves", glm::vec3(0.12f, 0.40f, 0.22f), true, false,
      RenderShape::Cube, RenderLayer::Translucent, 0.88f },
    { BlockId::JUNGLE_WOOD,  "Jungle Wood",   glm::vec3(0.46f, 0.30f, 0.14f), true, false },
    { BlockId::JUNGLE_LEAVES,"Jungle Leaves", glm::vec3(0.10f, 0.58f, 0.15f), true, false,
      RenderShape::Cube, RenderLayer::Translucent, 0.84f },
    { BlockId::ACACIA_WOOD,  "Acacia Wood",   glm::vec3(0.62f, 0.30f, 0.14f), true, false },
    { BlockId::ACACIA_LEAVES,"Acacia Leaves", glm::vec3(0.34f, 0.55f, 0.17f), true, false,
      RenderShape::Cube, RenderLayer::Translucent, 0.86f },
    { BlockId::COBBLESTONE,   "Cobblestone",   glm::vec3(0.43f), true, false },
    { BlockId::CRAFTING_TABLE,"Crafting Table",glm::vec3(0.55f, 0.36f, 0.18f), true, false },
    { BlockId::FURNACE,       "Furnace",       glm::vec3(0.38f), true, false },
    { BlockId::CHEST,         "Chest",         glm::vec3(0.58f, 0.36f, 0.12f), true, false },
    { BlockId::TORCH,         "Torch",         glm::vec3(0.95f, 0.72f, 0.25f), false, true,
      RenderShape::Cross, RenderLayer::Cutout, 1.0f },
    { BlockId::WHITE_WOOL,    "White Wool",    glm::vec3(0.92f), true, false },
    { BlockId::WHITE_BED,     "White Bed",     glm::vec3(0.88f), true, false },
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
}};

BlockTexture getFaceTexture(BlockId id, FaceDir face) {
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
        default:                     return BlockTexture::Dirt;
    }
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

uint8_t fireEncouragement(BlockId id) {
    switch (id) {
        case BlockId::WOOD: case BlockId::BIRCH_WOOD:
        case BlockId::SPRUCE_WOOD: case BlockId::JUNGLE_WOOD:
        case BlockId::ACACIA_WOOD: return 5;
        case BlockId::PLANKS: case BlockId::CRAFTING_TABLE:
        case BlockId::CHEST: return 5;
        case BlockId::LEAVES: case BlockId::BIRCH_LEAVES:
        case BlockId::SPRUCE_LEAVES: case BlockId::JUNGLE_LEAVES:
        case BlockId::ACACIA_LEAVES: case BlockId::WHITE_WOOL:
        case BlockId::WHITE_BED: return 30;
        case BlockId::OAK_SAPLING: case BlockId::BIRCH_SAPLING:
        case BlockId::SPRUCE_SAPLING: case BlockId::JUNGLE_SAPLING:
        case BlockId::ACACIA_SAPLING: case BlockId::TALL_GRASS:
        case BlockId::FLOWER: case BlockId::WHEAT_0:
        case BlockId::WHEAT_1: case BlockId::WHEAT_2:
        case BlockId::WHEAT_3: case BlockId::WHEAT_4:
        case BlockId::WHEAT_5: case BlockId::WHEAT_6:
        case BlockId::WHEAT_7: return 60;
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
            case BlockId::ACACIA_WOOD: return 5;
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
    if (!currentProps.solid)
        return !neighborProps.solid && neighbor != current;
    if (!neighborProps.solid) return true;

    // Preserve the opaque surface behind ice/leaves. Without this interface
    // face, gaps or alpha in the translucent material reveal missing terrain
    // geometry and look like an x-ray into caves below.
    return currentProps.layer == RenderLayer::Opaque &&
           neighborProps.layer == RenderLayer::Translucent;
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
// Winding: CCW from outside the cube (matches OpenGL default with
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
