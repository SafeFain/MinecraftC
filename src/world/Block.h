#pragma once

#include <cstdint>
#include <string>
#include <array>
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
    COUNT        = 19
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

struct BlockProperties {
    BlockId   id;
    std::string name;
    glm::vec3 color;       // base RGB (0..1)
    bool      solid;
    bool      transparent;
};

// Global registry (defined in Block.cpp)
extern const std::array<BlockProperties, 19> BLOCK_TABLE;

// Quick lookup
inline const BlockProperties& getBlockProps(BlockId id) {
    return BLOCK_TABLE[static_cast<uint8_t>(id)];
}

inline bool isSolid(BlockId id) {
    return getBlockProps(id).solid;
}

// ── Face brightness (fake lighting) ───────────────────────────────────

extern const std::array<float, 6> FACE_BRIGHTNESS; // indexed by FaceDir

// ── Grass special colors ──────────────────────────────────────────────

extern const glm::vec3 GRASS_TOP_COLOR;   // (0.42, 0.76, 0.30)
extern const glm::vec3 GRASS_SIDE_COLOR;  // (0.45, 0.55, 0.25)
extern const glm::vec3 DIRT_COLOR;        // (0.56, 0.37, 0.18) — for grass bottom

// ── Get rendered face color ───────────────────────────────────────────

glm::vec3 getFaceColor(BlockId id, FaceDir face);

// ── Face direction offset vectors ─────────────────────────────────────

extern const std::array<glm::ivec3, 6> FACE_OFFSETS;

// ── Unit cube geometry ────────────────────────────────────────────────
// 8 corners of a 1×1×1 cube at origin
extern const std::array<glm::vec3, 8> CUBE_CORNERS;

// 6 faces × 6 vertex indices (2 triangles per face, CCW winding from outside)
extern const std::array<std::array<int, 6>, 6> FACE_INDICES;

// Wireframe cube — 12 line segments = 24 vertices
extern const std::array<glm::vec3, 24> WIRE_CUBE_VERTICES;
