#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace Config {

// ── Window ────────────────────────────────────────────────────────────
constexpr int   WINDOW_WIDTH    = 1280;
constexpr int   WINDOW_HEIGHT   = 720;
constexpr float FOV             = 70.0f;
constexpr float NEAR_PLANE      = 0.1f;
constexpr float FAR_PLANE       = 500.0f;

// ── World ─────────────────────────────────────────────────────────────
constexpr int   CHUNK_SIZE_X    = 16;
constexpr int   CHUNK_SIZE_Y    = 128;
constexpr int   CHUNK_SIZE_Z    = 16;
constexpr int   CHUNK_VOLUME    = CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z;
inline int     RENDER_DISTANCE = 6;    // runtime-mutable (changed via Settings menu)
constexpr int   WORLD_HEIGHT    = CHUNK_SIZE_Y;

// ── Async generation ──────────────────────────────────────────────────
constexpr int   MESH_UPLOADS_PER_FRAME = 4;   // max GL uploads per frame (avoids GPU stalls)
constexpr int   CHUNK_GEN_PER_FRAME    = 8;   // max chunks to enqueue for generation per frame

constexpr int   RENDER_DISTANCE_OPTIONS[] = {2, 4, 6, 8, 10, 12, 16};
constexpr int   RENDER_DISTANCE_OPTION_COUNT = 7;

// ── Visual environment ──────────────────────────────────────────────────
constexpr int   MSAA_SAMPLES = 4;
inline int      DAY_CYCLE_MINUTES = 20;
constexpr int   DAY_CYCLE_OPTIONS[] = {0, 10, 20, 40}; // 0 = static noon
constexpr int   DAY_CYCLE_OPTION_COUNT = 4;
constexpr float NIGHT_AMBIENT_MIN = 0.20f;
constexpr float FOG_START_FRACTION = 0.65f;

// ── Region Generation ────────────────────────────────────────────────────
// Chunks are generated in N×N "regions" to ensure perfect continuity
// across internal chunk boundaries. Padding extends the pre-computed
// column grid so all smoothing and river-flow operations
// have full neighbor data without cross-region queries.
constexpr int   REGION_SIZE_CHUNKS   = 3;     // N×N chunks per region (must be odd, >= 3)
constexpr int   REGION_PADDING       = 6;     // blocks of padding beyond region edge
                                              // covers: river_edge_extend(4) + biome_blend(2)
                                              //       + tree_leaf(3) + safety
constexpr int   REGION_SIZE_BLOCKS   = REGION_SIZE_CHUNKS * CHUNK_SIZE_X;        // 48
constexpr int   REGION_PADDED_SIZE   = REGION_SIZE_BLOCKS + 2 * REGION_PADDING;   // 60

// ── World Seed ─────────────────────────────────────────────────────────
inline uint64_t WORLD_SEED = 1234567890ULL;

// ── Terrain Generation ────────────────────────────────────────────────
constexpr int   SEA_LEVEL         = 40;
constexpr int   TERRAIN_MAX_HEIGHT = CHUNK_SIZE_Y - 5;  // max Y for terrain columns (123)

// Terrain/climate/router constants are versioned with WorldGenContext and
// intentionally live beside the algorithm in HeightPipeline.cpp. Changing
// them changes every seed and therefore requires a generation-version bump.

// ── Caves ─────────────────────────────────────────────────────────────
// Hybrid cave density fields, branching carvers, and aquifers.
constexpr int   BEDROCK_LEVEL              = 3;
constexpr int   CAVE_MIN_Y                 = BEDROCK_LEVEL + 2;
constexpr int   CAVE_TOP_MARGIN            = 10;
constexpr int   CAVE_DRY_ROOF              = 6;
constexpr int   CAVE_WET_ROOF              = 5;
constexpr float CAVE_CHEESE_SCALE_XZ       = 0.018f;
constexpr float CAVE_CHEESE_SCALE_Y        = 0.024f;
constexpr float CAVE_CHEESE_THRESHOLD      = 0.56f;
constexpr float CAVE_CHEESE_DEPTH_BONUS    = 0.16f;
constexpr float CAVE_SPAGHETTI_SCALE_XZ    = 0.028f;
constexpr float CAVE_SPAGHETTI_SCALE_Y     = 0.035f;
constexpr float CAVE_SPAGHETTI_THICKNESS   = 0.088f;
constexpr float CAVE_NOODLE_SCALE_XZ       = 0.055f;
constexpr float CAVE_NOODLE_SCALE_Y        = 0.075f;
constexpr float CAVE_NOODLE_THICKNESS      = 0.052f;
constexpr int   CAVE_CARVER_CELL_SIZE      = 64;
constexpr int   CAVE_CARVER_MAX_REACH      = 128;
constexpr float CAVE_CARVER_CHANCE         = 0.28f;
constexpr int   CAVE_LAVA_LEVEL            = 8;
constexpr int   CAVE_AQUIFER_LEVEL_BASE    = 28;
constexpr float CAVE_AQUIFER_THRESHOLD     = 0.02f;

// ── Snow ──────────────────────────────────────────────────────────────
constexpr int   SNOW_LINE_BASE           = 75;
constexpr float SNOW_TEMP_FACTOR         = 0.15f;
constexpr int   SNOW_LINE_DISABLED       = 999;

// ── Deepslate ─────────────────────────────────────────────────────────
constexpr int   DEEPSLATE_DEPTH          = 8;     // y below which stone becomes deepslate

// ── Ore Generation ────────────────────────────────────────────────────
constexpr int   ORE_MAX_PER_CHUNK        = 50;

constexpr float ORE_COAL_SCALE           = 0.05f;
constexpr float ORE_COAL_THRESHOLD       = 0.55f;
constexpr int   ORE_COAL_MIN_Y           = 1;
constexpr int   ORE_COAL_MAX_Y           = 120;

constexpr float ORE_IRON_SCALE           = 0.04f;
constexpr float ORE_IRON_THRESHOLD       = 0.65f;
constexpr int   ORE_IRON_MIN_Y           = 1;
constexpr int   ORE_IRON_MAX_Y           = 60;

constexpr float ORE_GOLD_SCALE           = 0.03f;
constexpr float ORE_GOLD_THRESHOLD       = 0.70f;
constexpr int   ORE_GOLD_MIN_Y           = 1;
constexpr int   ORE_GOLD_MAX_Y           = 30;

constexpr float ORE_DIAMOND_SCALE        = 0.025f;
constexpr float ORE_DIAMOND_THRESHOLD    = 0.75f;
constexpr int   ORE_DIAMOND_MIN_Y        = 1;
constexpr int   ORE_DIAMOND_MAX_Y        = 15;

// ── Trees ─────────────────────────────────────────────────────────────
constexpr int   TREE_MAX_CANDIDATES      = 30;
constexpr int   TREE_MAX_PLACEMENTS      = 50;
constexpr float TREE_SLOPE_MAX           = 2.0f;

// ── Ice ───────────────────────────────────────────────────────────────
constexpr int   ICE_FREEZE_MAX_Y         = 50;

// ── Biome ─────────────────────────────────────────────────────────────
// ── Player ────────────────────────────────────────────────────────────
constexpr float PLAYER_SPEED      = 8.0f;
constexpr float SPRINT_SPEED      = 14.0f;
constexpr float CREATIVE_FLY_SPEED = 10.9f;
constexpr float CREATIVE_FLY_SPRINT_SPEED = 21.6f;
constexpr float CREATIVE_FLY_VERTICAL_SPEED = 7.5f;
constexpr float JUMP_SPEED        = 10.0f;
constexpr float GRAVITY           = 25.0f;
constexpr float PLAYER_HEIGHT     = 1.8f;
constexpr float PLAYER_WIDTH      = 0.6f;
constexpr float EYE_HEIGHT        = 1.6f;
constexpr float MOUSE_SENSITIVITY = 0.15f;
constexpr float REACH_DISTANCE    = 6.0f;
constexpr float WATER_HORIZONTAL_FACTOR = 0.45f;
constexpr float WATER_RISE_SPEED = 4.0f;
constexpr float WATER_DIVE_SPEED = 3.0f;
constexpr float WATER_SINK_SPEED = 0.6f;
constexpr float WATER_ENTRY_MAX_FALL_SPEED = 3.0f;
inline bool     AUTO_JUMP         = true;

// ── UI ──────────────────────────────────────────────────────────────────
constexpr float UI_BUTTON_WIDTH      = 280.0f;
constexpr float UI_BUTTON_HEIGHT     = 44.0f;
constexpr float UI_BUTTON_SPACING    = 12.0f;
constexpr float UI_FONT_SCALE        = 1.8f;
constexpr float UI_TITLE_SCALE       = 4.5f;
constexpr float UI_OVERLAY_ALPHA     = 0.55f;

namespace UIColors {
    constexpr glm::vec4 BACKGROUND(0.08f, 0.08f, 0.12f, 1.0f);
    constexpr glm::vec4 OVERLAY(0.0f, 0.0f, 0.0f, 0.55f);
    constexpr glm::vec4 BUTTON_NORMAL(0.25f, 0.25f, 0.35f, 0.85f);
    constexpr glm::vec4 BUTTON_HOVER(0.35f, 0.35f, 0.50f, 0.90f);
    constexpr glm::vec4 BUTTON_SELECTED(0.45f, 0.45f, 0.60f, 0.90f);
    constexpr glm::vec3 TEXT_NORMAL(1.0f, 1.0f, 1.0f);
    constexpr glm::vec3 TEXT_HOVER(1.0f, 1.0f, 0.6f);
    constexpr glm::vec3 TEXT_TITLE(1.0f, 0.85f, 0.3f);
}

// ── Hotbar ──────────────────────────────────────────────────────────────
constexpr int   HOTBAR_SLOTS        = 9;
constexpr float HOTBAR_SLOT_SIZE    = 48.0f;
constexpr float HOTBAR_GAP          = 4.0f;
constexpr float HOTBAR_PAD_X        = 8.0f;
constexpr float HOTBAR_PAD_Y        = 6.0f;
constexpr float HOTBAR_BAR_HEIGHT   = 58.0f;

// ── Creative Inventory ───────────────────────────────────────────────────
constexpr int   INV_COLS            = 5;
constexpr float INV_SLOT_SIZE       = 48.0f;
constexpr float INV_PADDING         = 8.0f;
constexpr float INV_LABEL_HEIGHT    = 16.0f;
constexpr float INV_TITLE_SCALE     = 2.5f;
constexpr float INV_INSTR_SCALE     = 1.2f;

// ── Colors ────────────────────────────────────────────────────────────
constexpr glm::vec4 SKY_COLOR(0.53f, 0.81f, 0.92f, 1.0f);

// ── Debug / Logging ─────────────────────────────────────────────────────
namespace LogConfig {
    constexpr bool FILE_OUTPUT   = false;          // also write log to file
    constexpr bool COLOR_OUTPUT  = true;           // ANSI color codes in terminal
    constexpr const char* LOG_PATH = "minecraftc.log";
}

// ── Profiler ──────────────────────────────────────────────────────────
namespace ProfileConfig {
    constexpr int  FRAME_LOG_INTERVAL = 300;       // frames between periodic frame-time stats
    constexpr float SLOW_FRAME_THRESHOLD_MS = 33.0f; // warn if frame takes longer than this (~30 FPS)
}

} // namespace Config
