#include "world/Block.h"

// ── Block properties table ────────────────────────────────────────────

const std::array<BlockProperties, 19> BLOCK_TABLE = {{
    { BlockId::AIR,          "Air",          glm::vec3(0.0f, 0.0f, 0.0f), false, false },
    { BlockId::GRASS,        "Grass",        glm::vec3(0.34f, 0.68f, 0.24f), true, false },
    { BlockId::DIRT,         "Dirt",         glm::vec3(0.56f, 0.37f, 0.18f), true, false },
    { BlockId::STONE,        "Stone",        glm::vec3(0.50f, 0.50f, 0.50f), true, false },
    { BlockId::WOOD,         "Wood",         glm::vec3(0.55f, 0.40f, 0.20f), true, false },
    { BlockId::LEAVES,       "Leaves",       glm::vec3(0.15f, 0.55f, 0.15f), true, false },
    { BlockId::SAND,         "Sand",         glm::vec3(0.90f, 0.84f, 0.60f), true, false },
    { BlockId::BEDROCK,      "Bedrock",      glm::vec3(0.20f, 0.20f, 0.20f), true, false },
    { BlockId::WATER,        "Water",        glm::vec3(0.20f, 0.40f, 0.90f), false, true },
    { BlockId::SNOW,         "Snow",         glm::vec3(0.95f, 0.95f, 0.95f), true, false },
    { BlockId::PLANKS,       "Planks",       glm::vec3(0.70f, 0.55f, 0.30f), true, false },
    { BlockId::DEEPSLATE,    "Deepslate",    glm::vec3(0.25f, 0.25f, 0.27f), true, false },
    { BlockId::CACTUS_BLOCK, "Cactus",       glm::vec3(0.33f, 0.55f, 0.27f), true, false },
    { BlockId::COAL_ORE,     "Coal Ore",     glm::vec3(0.15f, 0.15f, 0.15f), true, false },
    { BlockId::IRON_ORE,     "Iron Ore",     glm::vec3(0.65f, 0.55f, 0.45f), true, false },
    { BlockId::GOLD_ORE,     "Gold Ore",     glm::vec3(0.85f, 0.75f, 0.25f), true, false },
    { BlockId::DIAMOND_ORE,  "Diamond Ore",  glm::vec3(0.40f, 0.80f, 0.85f), true, false },
    { BlockId::LAVA,         "Lava",         glm::vec3(0.95f, 0.50f, 0.10f), false, true },
    { BlockId::ICE,          "Ice",          glm::vec3(0.70f, 0.85f, 0.95f), true, false },
}};

// ── Face brightness ───────────────────────────────────────────────────

const std::array<float, 6> FACE_BRIGHTNESS = {
    1.0f,   // top — brightest
    0.35f,  // bottom — darkest
    0.6f,   // front
    0.6f,   // back
    0.5f,   // right
    0.5f    // left
};

// ── Grass special colors ──────────────────────────────────────────────

const glm::vec3 GRASS_TOP_COLOR(0.42f, 0.76f, 0.30f);
const glm::vec3 GRASS_SIDE_COLOR(0.45f, 0.55f, 0.25f);
const glm::vec3 DIRT_COLOR(0.56f, 0.37f, 0.18f);

// ── getFaceColor ──────────────────────────────────────────────────────

glm::vec3 getFaceColor(BlockId id, FaceDir face) {
    glm::vec3 base;

    if (id == BlockId::GRASS) {
        switch (face) {
            case FaceDir::TOP:    base = GRASS_TOP_COLOR;  break;
            case FaceDir::BOTTOM: base = DIRT_COLOR;       break;
            default:              base = GRASS_SIDE_COLOR; break;
        }
    } else {
        base = getBlockProps(id).color;
    }

    float brightness = FACE_BRIGHTNESS[static_cast<int>(face)];
    return base * brightness;
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
