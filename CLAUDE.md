# MinecraftC — C++17 / OpenGL 3.3 Voxel Engine

A from-scratch Minecraft-style block world with greedy meshing, frustum culling,
multi-layer procedural terrain, **region-based async chunk generation** (3×3 atomic
regions), and a complete UI/menu system (19 block types).

## Quick Start

```bash
cd build && cmake .. && make -j$(nproc)
cd .. && ./build/minecraftc   # must run from project root (shaders in assets/shaders/)
```

## Tech Stack

| Layer | Choice |
|-------|--------|
| Language | C++17 (`-Wall -Wextra -Wpedantic`) |
| Build | CMake 3.16+, `file(GLOB_RECURSE SOURCES src/*.cpp src/*.h)` — new sources auto-discovered |
| Windowing | GLFW 3 (resizable, 1280×720 default) |
| OpenGL | 3.3 Core Profile (GLAD loader in `external/glad/`) |
| Math | GLM |
| Font | 8×14 bitmap in `FontRenderer.cpp` |
| Threading | `std::thread` pool with priority queue (gen=1, mesh=0) |

## Architecture

### Entry Point — `src/main.cpp`

`class Application` owns everything. Game loop branches on `m_gameState`:

| State | Input | Update | 3D Render | UI Render |
|-------|-------|--------|-----------|-----------|
| `MainMenu` | Menu keys/mouse | — | clear | Menu |
| `Playing` | Player input | world + meshes | full 3D | Hotbar |
| `Playing + Inv` | Inventory clicks | world + meshes | full 3D | Inventory |
| `Paused` | Menu keys/mouse | — | frozen 3D | PauseMenu |

**Transitions:** Startup→MainMenu, StartGame→Playing, ESC→Paused, ESC/Resume→Playing, BackToMenu→MainMenu.
**Inventory:** E toggles; ESC closes inventory first, then pauses.
**Hotbar:** 1-9 or scroll wheel; right-click places selected block.
**Deferred terrain:** generation runs on first StartGame; returning to menu keeps world; StartGame again regenerates.

### Subsystems

```
src/
├── main.cpp                     — Application, game loop, input routing, UI orchestration
├── Config.h                     — all tunable constants (~100 params)
├── core/Window                  — GLFW window, input callbacks, cursor lock
├── renderer/
│   ├── Renderer                 — 3D chunk + wireframe, VAO helpers
│   ├── Shader                   — file compiler, uniform cache (memoized)
│   ├── Camera                   — FPS camera, cached view/proj matrices (dirty-flag)
│   └── Frustum                  — VP plane extraction, AABB culling
├── world/
│   ├── Block                    — BlockId enum (19 types), face colors/brightness
│   ├── Chunk                    — 16×128×16 uint8_t array, column max-Y, atomic gen flags
│   ├── ChunkMesh                — greedy mesh builder (template), interleaved VBO (24B stride)
│   ├── World                    — ∞ chunk map, region async gen + pending-block queue, raycast
│   ├── RegionGenerationData     — 60×60 padded column grid, tree placements, worm segments (header-only)
│   ├── RegionGenerator          — 6-phase region gen (precomputeColumns→caves→trees→populate→connectivity→finalize)
│   ├── WorldGenerator           — sub-generator owner + legacy singleton fallback
│   ├── HeightPipeline           — dual-mode: padded region (primary) + per-chunk fallback
│   ├── BiomeMap                 — 12 biomes, 7 tree types, BIOME_TABLE
│   ├── Noise                    — 2D/3D Perlin, fractal octaves, domain warping
│   ├── CaveGenerator            — worm-based tunnels + CaveSpatialIndex (8-block 3D binning)
│   ├── TreeGenerator            — Poisson disk: region-wide placedGrid (primary) + per-chunk fallback
│   └── OreGenerator             — 4-ore 3D noise thresholding
├── player/Player                — FPS movement, AABB collision, physics, flight, raycast
├── threading/ThreadPool         — priority queue, auto-sized workers, exception-safe tasks
├── debug/
│   ├── Log                      — 6-level thread-safe logging (Trace→Fatal), ANSI colors, file output
│   ├── Assert                   — MC_ASSERT / MC_CHECK / MC_VERIFY macros (header-only)
│   ├── OpenGL                   — GL_CHECK() macro, glErrorString() translator (header-only)
│   ├── Profiler                 — ScopedTimer RAII + FrameTimer periodic stats (header-only)
│   └── CrashHandler             — SIGSEGV/SIGABRT/SIGFPE/SIGILL handlers, backtrace() stack trace
└── ui/
    ├── Menu                     — GameState enum, Button, MainMenu, PauseMenu
    ├── SettingsMenu             — render distance + seed settings
    ├── Hotbar                   — 9-slot bottom HUD
    ├── Inventory                — creative grid (5 cols, all non-AIR blocks)
    ├── UIRenderer               — 2D ortho, state save/restore
    └── FontRenderer             — 95-char bitmap → RGBA atlas (760×14), GLYPH_ prefix
```

### Data Flow (Playing state)

```
pollEvents → keys[] + cursorDelta
  → Player::handleMovement(keys, dt) — WASD, Space (jump/fly), Ctrl (sprint)
  → Player::handleMouseDelta(dx, dy) → camera look
  → Player::update(dt) — physics, gravity, block highlight
  → World::update(playerPos) — load/unload chunks in cylindrical radius
  → World::enqueueGeneration() — group ungenerated→3×3 regions→RegionGenerator (priority=1)
      Phase 1:  precomputeColumns() — HeightPipeline::computePaddedRegion() (60×60 grid)
      Phase 1b: precomputeCaves() — CaveGenerator::generateTunnels() + build CaveSpatialIndex
      Phase 2:  placeTreesRegion() — Poisson disk over 48×48 region core
      Phase 3a: populateChunk() ×9 — bedrock→stone→ores→caves→water→snow→ice
      Phase 3b: place all trees — cross-chunk leaves via setLeaf lambda
      Phase 4:  caveConnectivityPass() — 6-face check, bounds-guarded, 15% carve limit
      Phase 5:  finalizeChunks() — mark generated, dirty for mesh
  → Pending blocks (leaves outside region) → m_pendingBlocks
  → World::processCompletedGenerations() — applyPendingBlocks()
  → World::enqueueMeshBuilds() — threaded greedy meshing (priority=0)
  → World::processCompletedMeshes(renderer, maxUploads=4/frame)
  → Camera::sync to player → Renderer::renderChunks(frustum-culled) → UI → swapBuffers
```

---

## Region-Based World Generation

The generation unit is a **3×3 chunk region** (48×48 blocks) with 6-block padding
(60×60 effective). Within a region, all cross-chunk operations are perfectly
continuous. Between regions, a pending-block queue propagates tree leaves.

**Why regions?** Old per-chunk gen had 6 boundary bugs (cave walls, missing leaves,
tree clumping, biome cliffs, river misalignment, river bank seams) — all caused by
missing neighbor data. Regions make all neighbors available by construction.

**Singleton fallback:** chunks that can't form a full 3×3 region use legacy
`WorldGenerator::generate()` with `NeighborQuery` + `BlockSetter` callbacks.

### How the 6 Boundary Bugs Are Fixed

| Bug | Fix |
|-----|-----|
| Cave walls at edges | Region-wide connectivity pass, 6-face check across all 9 chunks, bounds-guarded |
| Tree leaves at boundaries | Pending-block queue → `applyPendingBlocks()` when target chunk generates |
| Tree Poisson disk per-chunk | Region-wide `placedGrid[48×48]` |
| Biome smoothing order | `smoothBiomeBoundariesPadded()` on padded 60×60 grid |
| River flow misalignment | `computeRiversPadded()` with pre-computed heights |
| River bank seams | `smoothRiverBanksPadded()` — full river data in blend radius |

---

## Terrain Pipeline

### baseHeightRaw(wx, wz) — Three-layer synthesis

1. **Continental noise** (4 octaves, freq 0.0003) → `continentalness ∈ [0,1]`
2. **Domain-warped terrain** (5 octaves, freq 0.0015, warp 80.0)
3. **Detail perturbation** (3 octaves, freq 0.008, amp 3.0)

Zone split by continentalness: `<0.35` OCEAN (baseH 15-30), `0.35-0.45` BEACH (30-42), `>0.45` LAND (42-97).
Return: `baseH + terrain*terrainAmp + detail` → typically [7, 125].

### Erosion → Biome → Height Modulation → Rivers → Block Fill

- **Erosion:** thermal, 4-neighbor slope; steep (>2.0)→erode at 0.3; depressions→fill at 0.2
- **Biome:** continentalness for ocean/beach/land split; elev overrides (rawH>75→MOUNTAINS, >55→HILLS); temp/humidity 4×3 table → 12 biomes
- **Height modulation:** `finalH = rawH*biome.heightMul + biome.heightOffset` (mul≈1.0 pass-through, offsets small — old mul values like 0.0/0.30 CRUSHED terrain)
- **Rivers:** ridge-noise flow-field, threshold 0.72, carve 3 blocks, floor ≥ seaLevel−2=38
- **Block fill:** bedrock(0-3)→stone→deepslate(≤8)→ores→lava(y 1-10, 2%)→subsoil→surface→water(≤40/45)→snow→ice(≤50, cold biomes)

### Caves (worm-based)

3 worm types (main 50%/branch 35%/shaft 15%), cell hash → splitmix64 PRNG, junction rooms (30% chance), wall noise ±0.8.
Carves STONE/DIRT/SAND/DEEPSLATE; per-column limit 67%.
Y range: BEDROCK_LEVEL+2 (5) to min(height-5, 118).

**CaveSpatialIndex:** 8-block 3D grid bins segments; `isInTunnel()` checks ~10-30 segments instead of ~1776 (~20-35x faster).

**generateTunnels cell query:** two-radius design — `kMaxRadiusXZ=125.3` (full worm travel), `kMaxRadiusY=5.3` (worms are Y-clamped, so Y expansion only needs radius+noise). Single-radius (both 5.3 or both 125.3) causes missing caves or saturated carve limits.

### Trees (Poisson disk, 7 shapes)

Region-wide: `placedGrid[48×48]` with `placedList`; per-chunk fallback: 16×16 grid.
Shapes: OAK (4-6), BIRCH (5-7), SPRUCE (6-10 conical), JUNGLE (8-12 wide), ACACIA (5-7 flat), SWAMP_OAK (4-6), CACTUS (1-3 pillar).

---

## Chunk System

- **Chunk:** 16×128×16 = 32,768 blocks, `uint8_t` flat array (`x + z*16 + y*256`), `columnMaxY[16][16]`, dirty flag, double-buffered mesh, 4 atomic flags
- **World:** `unordered_map<{cx,cz}, unique_ptr<Chunk>>`, `shared_mutex` (shared read, unique write), double-checked locking, `m_pendingBlocks` for cross-region leaves
- **Greedy mesh:** 6-face 2D visibility masks → merge adjacent same-type quads → interleaved VBO (pos+color, 24B stride)
- **Coords:** `chunkWorldX = cx*16`, `worldToChunkX(wx) = floor(wx/16)`, Y: 0=bedrock, 127=sky, 40=sea

---

## Player System

- **Movement:** WASD, Space=jump/fly-up, Ctrl=sprint (14 vs 8), double-tap Space=toggle flight
- **Flight:** full 3D at sprint×1.5, no gravity/collision, Shift=descend
- **Physics:** gravity 25 m/s², ground detection via downward scan, per-axis AABB sweep (0.6×1.8), 1-block auto step-up
- **Interaction:** DDA raycast (6 blocks), left-click=break, right-click=place, 0.15s cooldown

---

## UI System

- **Coords:** (0,0)=bottom-left; mouse Y: `fbHeight - glfwY`
- **UIRenderer:** ortho projection, depth/blend state save/restore, `drawRect()` + `renderText()`
- **FontRenderer:** 95-char 8×14 bitmap→RGBA atlas (760×14), `GL_NEAREST`, `GLYPH_` prefix (avoids C23 `CHAR_WIDTH` collision)
- **Menus:** Button class (label, hit-test, hover, activate callback), keyboard nav with wrapping
- **Hotbar:** 9×48px slots, 4px gap, centered bottom; scroll/key select; syncs to player selected block
- **Inventory:** 5-col grid, auto-populated from BLOCK_TABLE, semi-transparent overlay

---

## Rendering

- **Renderer:** block shader bound once/frame (not per chunk), VAO stays bound between draws. Wireframe via shared cube VAO.
- **Shader:** file-based (.vert/.frag), `unordered_map<string,GLint>` uniform cache, move-only, RAII cleanup
- **Camera:** yaw/pitch FPS-style, cached view matrix (dirty when moved/rotated), cached projection (dirty when aspect changes)
- **Frustum:** 6-plane extraction from VP, normalized, AABB p-vertex test

---

## Block System

19 types: `AIR=0, GRASS, DIRT, STONE, WOOD, LEAVES, SAND, BEDROCK, WATER, SNOW, PLANKS, DEEPSLATE, CACTUS_BLOCK, COAL_ORE, IRON_ORE, GOLD_ORE, DIAMOND_ORE, LAVA, ICE`

6 face directions with brightness multipliers (TOP=1.0, BOTTOM=0.35, sides 0.5-0.6). GRASS has green top, brown bottom, dark-green sides.

---

## Threading

`ThreadPool`: auto-sized workers, priority queue (gen=1 > mesh=0), `condition_variable` wait, fire-and-forget `enqueuePriority()` + future-returning `enqueue()`.

---

## Shader Inventory

| Shader | Purpose |
|--------|---------|
| `block.vert/frag` | 3D blocks — pos+color (loc 0,1), MVP uniform |
| `wireframe.vert/frag` | Black outline — pos only (loc 0), MVP uniform |
| `ui.vert/frag` | 2D rects — screen pos (loc 0), uProjection+uColor |
| `text.vert/frag` | Font glyphs — pos+UV (loc 0,1), `discard` for alpha |

All `#version 330 core` with explicit `layout(location=N)`.

---

## Key Config Constants

See `Config.h` for full list (~100 params). Notable:

| Constant | Value | Description |
|----------|-------|-------------|
| CHUNK_SIZE_X/Y/Z | 16/128/16 | Voxel dimensions |
| RENDER_DISTANCE | 6 (mutable) | Chunk load radius |
| WORLD_SEED | 1234567890 (mutable) | uint64_t |
| SEA_LEVEL | 40 | Water fill level |
| REGION_SIZE_CHUNKS | 3 | N×N per generation region |
| REGION_PADDING | 6 | Blocks beyond region edge |
| TERRAIN_MAX_HEIGHT | 123 | Max column height |
| CAVE_WORM_CELL_SIZE_XZ/Y | 48/24 | Worm cell grid |
| CAVE_MAX_CARVE_RATIO | 67% | Max column cave carve (Phase 3a) |
| CAVE_CONNECT_CARVE_RATIO | 15% | Max connectivity carve (Phase 4) |
| DEEPSLATE_DEPTH | 8 | Y below which stone→deepslate |
| RIVER_CARVE_DEPTH | 2 | Blocks below seaLevel (floor=38) |
| PLAYER_SPEED / SPRINT_SPEED | 8.0 / 14.0 | Movement speed |
| GRAVITY | 25.0 | Gravity acceleration |
| PLAYER_WIDTH / HEIGHT | 0.6 / 1.8 | Collision box |
| MESH_UPLOADS_PER_FRAME | 4 | Rate-limited GPU upload |

---

## Performance (2026-06 optimization pass)

**Generation:** CaveSpatialIndex (20-35x faster carving), two-radius cell query (343 cells vs 833), eliminated duplicate continentalness recomputation, 3-phase erosion (no noise recompute), incremental columnMaxY (no rescan), splitmix64 PRNG.

**Rendering:** block shader once/frame (~113 fewer glUseProgram calls), interleaved VBO (24B stride), cached camera matrices, tighter frustum AABB (maxY+1 vs always 128).

**World:** O(1) chunk removal via `unordered_set` lookup.

Expected: 60-80% faster region generation, 20-40% lower per-frame CPU.

---

## Debug System

All debug infrastructure lives in `src/debug/`. The system is designed to be
**zero-overhead in Release** — Trace/Debug log macros and assertion checks are
stripped entirely by the preprocessor; GL_CHECK reduces to a bare GL call.

### Logging (`debug/Log.h`, `debug/Log.cpp`)

6 severity levels. Macros capture `__FILE__` and `__LINE__` automatically.

```cpp
#include "debug/Log.h"

LOG_TRACE("Vertex buffer uploaded: " << vertexCount << " verts");  // stripped in Release
LOG_DEBUG("Enqueuing " << regions.size() << " region generations");
LOG_INFO("MinecraftC initialized");                                 // kept in Release
LOG_WARN("Falling back to singleton generation for chunk " << cx);
LOG_ERROR("Shader compilation error (" << type << "):\n" << log);
LOG_FATAL("Failed to initialize GLFW");
```

**Output format:** `[2026-06-16 14:32:01.234] [INFO ] [main.cpp:201] message`

**Features:**
- **Thread-safe:** internal mutex; each log line is atomic even under concurrent writes
- **ANSI colors** in terminal (auto-detected via `isatty`); plain text in file/pipe
- **Optional file output:** set `LogConfig::FILE_OUTPUT = true` in `Config.h`
- **Runtime filtering:** `Debug::Log::setMinLevel(LogLevel::Warn)` suppresses Info/Trace/Debug
- **Compile-time gating:** CMake defines `LOG_COMPILE_LEVEL` — Debug=0 (all), Release=2 (Info+)

**Initialization** (called once in `Application::initialize()`):
```cpp
Debug::Log::init(LogLevel::Trace, Config::LogConfig::FILE_OUTPUT, Config::LogConfig::LOG_PATH);
Debug::installCrashHandlers();
// ...
Debug::Log::shutdown();  // in cleanup()
```

### Assertions (`debug/Assert.h`)

| Macro | Debug (no NDEBUG) | Release (NDEBUG) |
|-------|-------------------|-------------------|
| `MC_ASSERT(cond, msg)` | `LOG_FATAL` + `abort()` | stripped to `((void)0)` |
| `MC_CHECK(cond, msg)` | `LOG_ERROR` (no abort) | `LOG_ERROR` (no abort) |
| `MC_VERIFY(cond, msg)` | `LOG_FATAL` + `abort()` | `LOG_FATAL` + `abort()` |

```cpp
MC_ASSERT(indexCount > 0, "Mesh built with zero indices — chunk " << cx);
MC_CHECK(glfwInit(), "GLFW initialization failed");
MC_VERIFY(gladLoadGL(loader), "Failed to load OpenGL functions");
```

### OpenGL Error Checking (`debug/OpenGL.h`)

`GL_CHECK(expr)` executes the GL call, then drains `glGetError()` and logs any
errors with file, line number, and the expression text.

```cpp
GL_CHECK(glBindVertexArray(vao));
GL_CHECK(glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr));
GL_CHECK(glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW));
```

- **Debug:** executes expr, checks for errors, logs with file:line if any found
- **Release:** executes expr only — error checking compiles out entirely

All GL calls in `Renderer.cpp`, `ChunkMesh.cpp`, `UIRenderer.cpp`, `FontRenderer.cpp`,
and `Shader.cpp` are wrapped with `GL_CHECK()`.

Error codes are translated to human-readable strings: `GL_INVALID_ENUM`, `GL_INVALID_VALUE`,
`GL_INVALID_OPERATION`, `GL_OUT_OF_MEMORY`, `GL_INVALID_FRAMEBUFFER_OPERATION`.

### Performance Profiling (`debug/Profiler.h`)

**ScopedTimer** — RAII timer that logs elapsed time on destruction:
```cpp
{
    MC_PROFILE_SCOPE("MeshBuild");        // logs at Trace level
    Debug::ScopedTimer t("Gen", LogLevel::Debug, 16.0f);  // warns if >16ms
    // ... work ...
}  // auto-logs on scope exit

MC_PROFILE_FUNC();  // logs function name + elapsed time
```

**FrameTimer** — accumulates per-frame stats, logs min/max/avg every 300 frames:
```cpp
Debug::FrameTimer m_frameTimer;
// in main loop:
m_frameTimer.beginFrame();
// ... game loop ...
m_frameTimer.endFrame();  // prints periodic summary
```

### Crash Handling (`debug/CrashHandler.h`, `debug/CrashHandler.cpp`)

Signal handlers for SIGSEGV, SIGABRT, SIGFPE, SIGILL. On crash:
1. Writes crash banner + signal name to stderr (async-signal-safe)
2. Captures backtrace via glibc `backtrace()` + `backtrace_symbols_fd()` (Linux)
3. Re-raises with default handler to produce a core dump

```cpp
Debug::installCrashHandlers();     // call once early in main()
Debug::printStackTrace(32);        // non-fatal diagnostic dump
```

`-rdynamic` is added to Debug link flags so `backtrace_symbols()` resolves
function names.

### ThreadPool Exception Safety

Worker thread loop and `enqueuePriority()` both wrap user tasks in try-catch.
Exceptions are logged via `LOG_ERROR` instead of propagating to `std::terminate()`.
This is a **defense-in-depth** fix — both layers catch independently so a
missed catch in one path doesn't crash the process.

### Config Constants (in `Config.h`)

| Constant | Default | Description |
|----------|---------|-------------|
| `LogConfig::FILE_OUTPUT` | `false` | Also write log to `minecraftc.log` |
| `LogConfig::COLOR_OUTPUT` | `true` | ANSI color codes in terminal |
| `LogConfig::LOG_PATH` | `"minecraftc.log"` | Log file path |
| `ProfileConfig::FRAME_LOG_INTERVAL` | `300` | Frames between frame-time stats |
| `ProfileConfig::SLOW_FRAME_THRESHOLD_MS` | `33.0f` | Warn threshold (~30 FPS) |

---

## Notes

- **GLAD:** hand-written OpenGL 3.3 Core loader in `external/glad/` (~47 functions, ~25 constants); no external dependency. Previously a shim for 3 missing functions — eliminated.
- **C23 collision:** `CHAR_WIDTH` from `<limits.h>` (transitive via GLM) collides with font constants → use `GLYPH_` prefix.
- **Build:** new `.cpp/.h` under `src/` auto-discovered via `GLOB_RECURSE`; `GLFW_INCLUDE_NONE` prevents GLFW from pulling `<GL/gl.h>`.
