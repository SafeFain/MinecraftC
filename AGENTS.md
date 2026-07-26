# Project Instructions

## Project Overview

MinecraftC is a C++17/OpenGL voxel sandbox. It provides deterministic seeded
terrain and caves, asynchronous region-based chunk generation, greedy meshing,
first-person interaction, and a menu/hotbar/creative-inventory UI.

This file contains model-independent, long-lived project facts and rules.
Task-specific decisions and execution state belong in `PLAN.md` and
`PROGRESS.md`.

## Features

- Infinite chunk loading around the player with a runtime-selectable render
  distance.
- One world seed deterministically controls terrain, biome, cave, ore, surface
  decoration, and tree placement.
- Generation version 2 uses FastNoiseLite OpenSimplex2S climate/terrain fields
  and the existing hybrid density/carver cave generator.
- 3×3 chunk region generation with padded world-coordinate sampling and a
  singleton fallback for incomplete regions.
- 18 biomes, seven vegetation/tree shapes, four ore types, and 36 block IDs.
- Opaque, cutout, and translucent rendering; cube greedy meshing and crossed
  plant geometry.
- Shared 8×8 runtime block texture atlas. World blocks, hotbar thumbnails, and
  creative-inventory thumbnails use the same material mapping.
- Polished vanilla-style lighting with voxel vertex AO, cheap sky visibility,
  dynamic sun/moon light, a configurable day/night cycle, analytic sky,
  render-distance fog, tile-safe mipmaps, sRGB handling, and 4× MSAA.
- First-person movement, collision, flight, raycast breaking/placing, menus,
  hotbar, settings, configurable kinematic auto jump, and creative inventory.
- Determinism/boundary tests for caves and complete world generation.

## Technology Stack

- C++17 and C11, built with CMake 3.16 or newer.
- GLFW 3, OpenGL 3.3 Core, GLM.
- The project-local GLAD subset in `external/glad/`.
- Vendored FastNoiseLite in `external/FastNoiseLite/`.
- Vendored `stb_image` in `external/stb/`.
- `std::thread`-based priority thread pool.
- CTest for regression tests.

Linux development packages used by the verified build are `build-essential`,
`cmake`, `libglfw3-dev`, `libglm-dev`, and `libgl1-mesa-dev`.

## Repository Structure

- `src/main.cpp`: application ownership, state machine, game loop, and input/UI
  routing.
- `src/Config.h`: runtime and compile-time configuration.
- `src/core/`: GLFW window and input integration.
- `src/world/`: blocks, chunks, deterministic generation, caves, vegetation,
  ores, surface rules, and mesh construction.
- `src/renderer/`: shaders, camera, frustum, chunk rendering, and shared block
  texture atlas.
- `src/player/`: movement, physics, collision, and block interaction.
- `src/threading/`: prioritized worker pool.
- `src/ui/`: menus, hotbar, creative inventory, rectangles, and bitmap text.
- `src/debug/`: logging, assertions, OpenGL checking, profiling, and crash
  handlers.
- `assets/shaders/`: GLSL 3.30 shaders loaded by relative path.
- `assets/textures/`: CC0 source textures and provenance.
- `external/`: vendored dependencies; treat them as third-party code.
- `tests/`: standalone CTest regression executables.
- `build-local/`: recommended out-of-source local build directory; generated
  content is not project source.

## Architecture

### Runtime flow

`Application` owns the renderer, UI, world, player, camera, and thread pool.
During play the main thread updates the player and chunk set, queues generation,
accepts completed generation, queues CPU mesh work, uploads a bounded number of
meshes to OpenGL, renders opaque then translucent chunks, and finally renders
the UI.

OpenGL calls and GPU resource ownership stay on the main/render thread. Worker
threads generate blocks and CPU mesh buffers only. A completed `ChunkMesh` must
transfer all CPU geometry metadata, including opaque/translucent index ranges,
before upload.

### World generation

- `WorldGenContext::GENERATION_VERSION` versions deterministic output and
  derives independent seed domains.
- `HeightPipeline::sampleColumn(wx, wz)` is the surface source of truth.
  Batched region sampling must exactly match point sampling at every world
  coordinate, including negative coordinates.
- The normal generation unit is a 3×3 chunk core (48×48 blocks) with six blocks
  of padding. The region pipeline computes columns and caves, selects tree
  anchors, populates/finalizes chunks, and queues blocks that cross unavailable
  boundaries.
- The singleton generator must remain output-equivalent to region generation
  for the same world coordinates.
- Cave generation intentionally retains its legacy `Noise` dependency. Do not
  replace or retune it as a side effect of terrain work.
- Any change that alters seeded world output requires an explicit generation
  version decision and updated determinism tests.

### Blocks, meshes, and materials

- A chunk is 16×128×16 `uint8_t` blocks with flat index
  `x + z*16 + y*256`.
- `BlockId` values are serialized in chunk memory. Do not reorder existing IDs;
  append new IDs before `COUNT`.
- `ChunkMesh` uses a 44-byte interleaved vertex: position (3 floats), AO/sky
  visibility/reserved/alpha (4), repeating tile UV (2), atlas tile index (1),
  and face direction (1).
- Cube faces are greedy-merged. Shader-side fractional tile UVs repeat the
  selected 16×16 material instead of stretching it over a merged quad.
- Greedy face compatibility includes the four-corner AO signature and sky
  visibility. Opaque cubes occlude AO; translucent cubes and crossed plants do
  not. Quad diagonals follow the AO corner sums.
- `BlockTexture` and `getFaceTexture` are the single material mapping used by
  world meshes and UI thumbnails.
- The atlas is created once by `Renderer`; `UIRenderer` borrows its texture ID
  and does not own or delete it.

### Coordinates and rendering

- Chunk coordinates use mathematical floor division for negative world
  coordinates.
- Y=0 is the world bottom and Y=127 is the highest valid block coordinate.
- Opaque/cutout indices render first. Translucent indices render far-to-near
  with blending enabled and depth writes disabled.
- `DayNightCycle` is the source of truth for celestial direction and
  environment colors. The renderer draws an analytic sky first, then lights
  chunks in one pass and blends distance fog toward the current sky.
- The default framebuffer requests 4× MSAA and sRGB. Platforms without an
  sRGB-capable default framebuffer use shader gamma output. All authored UI
  colors and sRGB block textures must remain consistent with this distinction.
- The block atlas stores five independently downsampled mip levels per tile;
  never generate mipmaps by filtering the assembled atlas across tile borders.
- UI coordinates have their origin at bottom-left; GLFW mouse Y must be
  converted from its top-left origin.

### Player physics

- Player position is the bottom-center of its AABB. Downward collision support
  must be resolved across the complete foot footprint, not only the center
  block column.
- Walking beyond a ledge must preserve vertical velocity and fall under
  gravity; collision code must not snap to a lower, non-colliding surface.
- Auto Jump is a runtime option and uses the same vertical impulse as manual
  Space jumping. Never implement one-block traversal by teleporting the player.
- Auto Jump requires grounded state, blocked horizontal movement, and clear
  current/target headroom. Manual jump and double-tap flight remain independent.

## Development Commands

Run commands from the repository root because shader and texture paths are
relative.

```bash
# Configure a clean release build
cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release

# Build (use a conservative job count in memory-constrained environments)
cmake --build build-local -j2

# Run all world-generation and rendering-logic regression tests
ctest --test-dir build-local --output-on-failure

# Run the application
./build-local/minecraftc

# Check patch whitespace
git diff --check
```

The CMake source glob discovers new files under `src/` after reconfiguration.
Tests have explicit source lists, so add dependencies to their targets when a
new non-header implementation is required by a test.

There is currently no repository CI workflow, formatter configuration, or
separate static-analysis target.

## Coding Conventions

- Follow the existing C++17 style and compile cleanly under
  `-Wall -Wextra -Wpedantic`.
- Keep tunable global constants in `Config.h`; keep algorithm-specific,
  generation-versioned constants near their implementation.
- Use deterministic integer hashing/derived seed domains for world generation.
  Never use request order, worker scheduling, or mutable global RNG state to
  decide world output.
- Express terrain, biome, cave, ore, and vegetation decisions in world
  coordinates so chunk/region boundaries cannot affect results.
- Preserve RAII and ownership boundaries for OpenGL and thread resources.
- Route diagnostic output through the logging system. Do not leave ad-hoc
  debug prints in production paths.
- Add or extend regression tests for determinism, negative coordinates,
  boundary equivalence, mesh-layer metadata, material mapping, footprint
  support, or movement gating when those areas change.
- Keep comments focused on invariants and non-obvious reasons.

## Modification Boundaries

- Do not edit generated files under `build/` or `build-local/` as source.
- Do not replace or modify vendored code in `external/` without recording its
  upstream source/license and validating all consumers.
- Preserve existing `BlockId` numeric values and generation-version semantics.
- Do not perform OpenGL uploads/deletion on worker threads.
- Do not weaken cave/world determinism or boundary tests to accommodate an
  implementation regression.
- Preserve user changes in a dirty worktree; do not reset, discard, or
  mechanically rewrite unrelated work.
- Asset licenses and provenance must remain documented.

## Validation Requirements

For normal code changes:

1. Configure if CMake inputs or source discovery changed.
2. Build the affected targets.
3. Run `ctest --test-dir build-local --output-on-failure`.
4. Run `git diff --check`.
5. For renderer/UI changes, perform a real OpenGL startup smoke test where a
   display or virtual display is available.
6. Report warnings separately; never claim an unrun check passed.

World-generation changes additionally require same-seed determinism,
different-seed variation, negative-coordinate and region/singleton equivalence.
Renderer changes require checking index-layer handoff and shader/vertex layout.

## Working Rules

- Understand the relevant implementation before modifying it.
- Prefer fixing root causes over adding local workarounds.
- Make the smallest complete change that satisfies the task.
- Do not expand task scope without a requirement or an identified dependency.
- Do not delete code or documents whose purpose is unclear.
- Do not claim tests passed unless they were actually run.
- When documentation conflicts with code/configuration, verify actual behavior
  and record the correction.
- Update `PLAN.md` and `PROGRESS.md` after important design, scope, stage, or
  validation changes.
- Do not commit, push, rebase, reset, or delete branches unless explicitly
  requested.

## Documentation Sources

- `AGENTS.md`: model-independent project facts, architecture, and working rules.
- `CLAUDE.md`: Claude Code compatibility entry point only.
- `PLAN.md`: scope, decisions, stages, risks, and completion criteria for the
  current task.
- `PROGRESS.md`: verified execution state, changed-file summary, validation
  results, migration record, and recovery instructions.
- `assets/textures/LICENSE.md`: block texture provenance and license.

When resuming work, read all four root context files, then verify them against
`git status`, `git diff`, the current code, and fresh test results.
