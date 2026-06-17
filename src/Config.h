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

// ── Region Generation ────────────────────────────────────────────────────
// Chunks are generated in N×N "regions" to ensure perfect continuity
// across internal chunk boundaries. Padding extends the pre-computed
// column grid so all smoothing / river flow / connectivity operations
// have full neighbor data without cross-region queries.
constexpr int   REGION_SIZE_CHUNKS   = 3;     // N×N chunks per region (must be odd, >= 3)
constexpr int   REGION_PADDING       = 6;     // blocks of padding beyond region edge
                                              // covers: river_edge_extend(4) + biome_blend(2)
                                              //       + cave_connect(2) + tree_leaf(3) + safety
constexpr int   REGION_SIZE_BLOCKS   = REGION_SIZE_CHUNKS * CHUNK_SIZE_X;        // 48
constexpr int   REGION_PADDED_SIZE   = REGION_SIZE_BLOCKS + 2 * REGION_PADDING;   // 60

// ── World Seed ─────────────────────────────────────────────────────────
inline uint64_t WORLD_SEED = 1234567890ULL;

// ── Terrain Generation ────────────────────────────────────────────────
constexpr int   SEA_LEVEL         = 40;
constexpr int   TERRAIN_MAX_HEIGHT = CHUNK_SIZE_Y - 5;  // max Y for terrain columns (123)

// ── Height Synthesis (baseHeightRaw zone formulas) ───────────────────
constexpr float CONTINENTAL_SCALE        = 0.0003f;
constexpr float OCEAN_THRESHOLD          = 0.35f;
constexpr float BEACH_THRESHOLD          = 0.45f;
constexpr int   CONTINENTAL_OCTAVES      = 4;

// Ocean zone: seaLevel + HEIGHT_OCEAN_BASE + t * HEIGHT_OCEAN_RANGE
constexpr float HEIGHT_OCEAN_BASE        = -25.0f;
constexpr float HEIGHT_OCEAN_RANGE       = 15.0f;
constexpr float HEIGHT_OCEAN_AMP         = 5.0f;

// Beach zone: seaLevel + HEIGHT_BEACH_BASE + t * HEIGHT_BEACH_RANGE
constexpr float HEIGHT_BEACH_BASE        = -10.0f;
constexpr float HEIGHT_BEACH_RANGE       = 12.0f;
constexpr float HEIGHT_BEACH_AMP         = 8.0f;

// Land zone: seaLevel + HEIGHT_LAND_BASE + t * HEIGHT_LAND_RANGE
constexpr float HEIGHT_LAND_BASE         = 2.0f;
constexpr float HEIGHT_LAND_RANGE        = 55.0f;
constexpr float HEIGHT_LAND_AMP          = 25.0f;

// ── Layered terrain noise ─────────────────────────────────────────────
constexpr float TERRAIN_BASE_SCALE       = 0.0015f;
constexpr float TERRAIN_DETAIL_SCALE     = 0.008f;
constexpr float TERRAIN_WARP_SCALE       = 0.002f;
constexpr float TERRAIN_WARP_STRENGTH    = 80.0f;
constexpr float DETAIL_AMP               = 3.0f;
constexpr int   TERRAIN_OCTAVES          = 5;
constexpr int   DETAIL_OCTAVES           = 3;
constexpr int   WARP_OCTAVES             = 2;
constexpr float NOISE_PERSISTENCE        = 0.5f;
constexpr float NOISE_LACUNARITY         = 2.0f;

// ── Erosion ───────────────────────────────────────────────────────────
constexpr float EROSION_SLOPE_THRESHOLD  = 2.0f;
constexpr float EROSION_RATE             = 0.3f;
constexpr float EROSION_FILL_THRESHOLD   = 1.5f;
constexpr float EROSION_FILL_RATE        = 0.2f;
constexpr float EROSION_NEIGHBOR_SPACING = 1.0f;

// ── Biome Classification ──────────────────────────────────────────────
constexpr float BIOME_TEMP_SCALE         = 0.0008f;
constexpr float BIOME_HUMID_SCALE        = 0.0008f;
constexpr int   BIOME_TEMP_OCTAVES       = 3;
constexpr int   BIOME_HUMID_OCTAVES      = 3;
constexpr float TEMP_HOT_THRESHOLD       = 0.4f;
constexpr float TEMP_WARM_THRESHOLD      = 0.0f;
constexpr float TEMP_COOL_THRESHOLD      = -0.3f;
constexpr float HUMID_DRY_THRESHOLD      = -0.3f;
constexpr float HUMID_WET_THRESHOLD      = 0.3f;
constexpr float ELEVATION_MOUNTAIN_MIN   = 75.0f;
constexpr float ELEVATION_HILL_MIN       = 55.0f;
constexpr float OCEAN_HEIGHT_OFFSET      = 8.0f;   // seaLevel - offset → OCEAN biome
constexpr float BEACH_HEIGHT_OFFSET      = 1.0f;   // seaLevel + offset → BEACH limit

// ── Biome Smoothing ───────────────────────────────────────────────────
constexpr float BIOME_BLEND_WEIGHT       = 0.20f;
constexpr int   BIOME_SMOOTH_PASSES      = 2;

// ── Rivers ────────────────────────────────────────────────────────────
constexpr float RIVER_SCALE              = 0.002f;
constexpr float RIVER_THRESHOLD          = 0.72f;
constexpr int   RIVER_MIN_WIDTH          = 1;
constexpr int   RIVER_MAX_WIDTH          = 4;
constexpr int   RIVER_EDGE_EXTEND        = 4;     // padding blocks around chunk for flow detection
constexpr float RIVER_RIDGE_HEIGHT_BOOST = 20.0f; // ridge score height weight
constexpr int   RIVER_CARVE_DEPTH        = 2;     // blocks below seaLevel for river floor
constexpr int   RIVER_CARVE_STEP         = 3;     // blocks to lower per river cell
constexpr int   RIVER_BANK_BLEND_RADIUS  = 3;
constexpr int   RIVER_BANK_PASSES        = 2;

// ── Caves ─────────────────────────────────────────────────────────────
// Worm-based tunnel system: replaces Perlin noise thresholding with
// deterministic 3D random-walk "worms" that carve meandering passages,
// vertical shafts, and junction rooms. Continuity is guaranteed because
// worms are continuous paths; no more scattered noise pockets.
constexpr int   BEDROCK_LEVEL            = 3;
constexpr int   CAVE_MIN_Y               = BEDROCK_LEVEL + 2;

// Cave carving margins
constexpr int   CAVE_CEILING_MARGIN      = 5;
constexpr int   CAVE_TOP_MARGIN          = 10;
constexpr int   CAVE_MAX_CARVE_RATIO     = 67;
constexpr int   CAVE_CONNECT_CARVE_RATIO = 15;     // max % of cave-band connectivity can carve per column

// ── Worm cell grid ─────────────────────────────────────────────────────
// The world is divided into a 3D grid of "worm cells". Each cell
// deterministically spawns 0–N worm paths based on a hashed seed.
constexpr int   CAVE_WORM_CELL_SIZE_XZ   = 48;
constexpr int   CAVE_WORM_CELL_SIZE_Y    = 24;
constexpr int   CAVE_WORMS_PER_CELL_MAX  = 4;
constexpr float CAVE_WORM_TURN_RATE      = 0.35f;   // max direction change per step (radians)

// ── Main tunnels (50% of worms) — wide, long, gently meandering ────────
constexpr float CAVE_MAIN_RADIUS_MIN     = 2.5f;
constexpr float CAVE_MAIN_RADIUS_MAX     = 4.5f;
constexpr int   CAVE_MAIN_STEPS_MIN      = 30;
constexpr int   CAVE_MAIN_STEPS_MAX      = 60;
constexpr float CAVE_MAIN_STEP_SIZE      = 2.0f;
constexpr float CAVE_MAIN_VERTICAL_BIAS  = 0.15f;

// ── Branch passages (35% of worms) — narrow, twisty side tunnels ───────
constexpr float CAVE_BRANCH_RADIUS_MIN    = 1.0f;
constexpr float CAVE_BRANCH_RADIUS_MAX    = 2.5f;
constexpr int   CAVE_BRANCH_STEPS_MIN     = 15;
constexpr int   CAVE_BRANCH_STEPS_MAX     = 35;
constexpr float CAVE_BRANCH_STEP_SIZE     = 1.5f;
constexpr float CAVE_BRANCH_VERTICAL_BIAS = 0.30f;

// ── Vertical shafts (15% of worms) — steeply descending/ascending ──────
constexpr float CAVE_SHAFT_RADIUS_MIN     = 1.5f;
constexpr float CAVE_SHAFT_RADIUS_MAX     = 3.0f;
constexpr int   CAVE_SHAFT_STEPS_MIN      = 20;
constexpr int   CAVE_SHAFT_STEPS_MAX      = 40;
constexpr float CAVE_SHAFT_STEP_SIZE      = 2.0f;
constexpr float CAVE_SHAFT_VERTICAL_BIAS  = 0.85f;

// ── Junction rooms — spherical chambers at random positions ────────────
// Placed on a coarse grid. Only a fraction of cells get a room.
constexpr int   CAVE_ROOM_GRID_XZ         = 128;
constexpr int   CAVE_ROOM_GRID_Y          = 32;
constexpr float CAVE_ROOM_RADIUS_MIN      = 3.5f;
constexpr float CAVE_ROOM_RADIUS_MAX      = 7.0f;
constexpr float CAVE_ROOM_CHANCE          = 0.30f;

// ── Wall noise — organic variation on tunnel surfaces ──────────────────
constexpr float CAVE_WALL_NOISE_SCALE     = 0.15f;
constexpr float CAVE_WALL_NOISE_AMP       = 0.8f;

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

// ── Lava Pools ────────────────────────────────────────────────────────
constexpr int   LAVA_POOL_MIN_Y          = 1;
constexpr int   LAVA_POOL_MAX_Y          = 10;
constexpr float LAVA_POOL_CHANCE         = 0.02f;

// ── Ice ───────────────────────────────────────────────────────────────
constexpr int   ICE_FREEZE_MAX_Y         = 50;

// ── Biome ─────────────────────────────────────────────────────────────
constexpr float BIOME_BLEND_RADIUS       = 3.0f;

// ── Player ────────────────────────────────────────────────────────────
constexpr float PLAYER_SPEED      = 8.0f;
constexpr float SPRINT_SPEED      = 14.0f;
constexpr float JUMP_SPEED        = 10.0f;
constexpr float GRAVITY           = 25.0f;
constexpr float PLAYER_HEIGHT     = 1.8f;
constexpr float PLAYER_WIDTH      = 0.6f;
constexpr float EYE_HEIGHT        = 1.6f;
constexpr float MOUSE_SENSITIVITY = 0.15f;
constexpr float REACH_DISTANCE    = 6.0f;

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
