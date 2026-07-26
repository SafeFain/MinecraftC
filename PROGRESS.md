# Task Progress

## Status

Current status: in progress.

## Completed

- Added stable serialized `ItemId`, item properties, 36-slot storage, armor,
  offhand, stacking, durability, food, and tool tiers.
- Added save format v2 with checksums, atomic replacement, player/rules/entity
  state, negative coordinates, and sparse per-chunk block overrides including
  explicit AIR.
- Applied saved overrides after deterministic generation and flushed modified
  chunks before unload/autosave.
- Added multi-world catalog backend with safe unique IDs and path traversal
  rejection.
- Appended Survival blocks without changing existing IDs: cobblestone,
  crafting table, furnace, chest, torch, wool, bed, farmland, and eight wheat
  stages.
- Added mining hardness, harvest tiers, block drops, core 2×2/3×3 recipes,
  smelting/fuel rules, tools, armor, shield, bow, and food data.
- Added Survival/Creative menu entry, Survival flight restriction, hold-to-mine,
  finite placement, tool wear, actual hotbar stacks, inventory manipulation,
  2×2 crafting, and crafting-table 3×3 crafting.
- Added health, hunger, saturation, exhaustion, starvation difficulties,
  armor wear/reduction, shield blocking, fall/suffocation/drowning/lava damage,
  farming, bed spawn, death drops, death UI, and respawn.
- Added item entities and cow/pig/sheep/chicken plus zombie/skeleton/spider/
  blastling entities, basic AI/combat/loot/spawning, persistence, and colored
  cuboid rendering.
- Added local torch lighting through the reserved mesh lighting channel.
- Added a centered high-contrast crosshair and a Survival mining progress bar
  driven by the actual current block hardness and selected tool. Releasing the
  mouse or changing/losing the target resets the progress immediately.
- Fixed Survival inventory input so press, pointer movement, and release are
  routed as a real drag-and-drop gesture. Logs and other stacks can now be
  dragged directly into the 2×2/3×3 crafting grids while click-to-pick-up and
  right-click splitting remain available.
- Replaced the fixed Survival/Creative launch buttons with a singleplayer world
  list and create-world screen. New saves accept a name, Survival/Creative
  mode, and an optional signed decimal seed; an empty seed is randomized.
  Existing saves retain and load their own seed, and the obsolete global seed
  control was removed from Settings.
- Reworked Creative flight to match Minecraft controls and movement: double-tap
  Space toggles flight, Space/Shift move vertically (and cancel when held
  together), Ctrl enables faster flight, forward movement stays horizontal
  regardless of camera pitch, blocks remain collidable, fast movement uses
  substeps to avoid tunneling, and touching down exits flight.
- Fixed inventory pointer hit testing on scaled/high-DPI displays by converting
  GLFW window coordinates to framebuffer coordinates. Mouse press and release
  handlers now refresh the pointer position immediately, so drag-and-drop uses
  the actual endpoints instead of stale per-frame coordinates.
- Fixed Survival collision tunneling and low-ceiling embedding. Horizontal and
  vertical movement now use bounded collision substeps, and a ceiling hit keeps
  the last valid player position instead of snapping the AABB downward into
  nearby floor blocks.
- Replaced plank-derived log sides with CC0 16x16 bark textures, gave crafting
  tables/furnaces/chests distinct panel patterns, and replaced grass-like wheat
  with dedicated multi-stem growth-stage art. Entities now use an original 3x3
  pixel-art texture atlas so passive and hostile species are recognizable
  instead of appearing as solid-colored cuboids.
- Greatly reduced coal, iron, gold, and diamond noise thresholds so ore remains
  a sparse replacement and underground terrain is stone-dominant. Generation
  remains version 2 intentionally so unexplored areas of existing saves receive
  the balance correction.
- Added a persistent Allow Cheats creation option, a T-key command line, and
  `/gamemode 0|1|3`. Spectator mode uses collision-free flight, hides targeting
  and inventory UI, and blocks mining, placement, attacks, and inventory access.
- Added cheat-gated `/tp x y z` with finite coordinate/range validation.
  Teleporting clears velocity and accumulated fall distance and immediately
  schedules chunk loading around the destination.
- Upgraded player and saved positions to double precision and introduced a
  camera-relative render origin for chunks, entities, and block highlights.
  Save format v4 remains backward-readable from v2/v3 and removes the severe
  movement/render jitter around million-block coordinates.
- Added save format v5 partition files for persistent single-chest and furnace
  block entities. Chests expose 27 slots; furnaces persist their three slots,
  fuel state, and cooking progress, tick only while loaded, and both containers
  drop their contents when broken.
- Added shared chest/furnace screens with framebuffer-coordinate drag and drop,
  validated furnace slots, progress indicators, and safe cursor-stack return.
  Survival and Creative can open containers while Spectator remains blocked.
- Replaced mesh-local torch scanning with a 0-15 world block-light field.
  Level-14 torch light propagates through transparent blocks across loaded chunk
  boundaries, is rebuilt after relevant block/chunk changes, participates in
  greedy-mesh compatibility, and prevents hostile spawning unless the spawn
  cell has block light zero.
- Upgraded entity positions to double precision and partitioned entity saves by
  current chunk. Entity simulation pauses on unload, moved entities clear stale
  partitions, and legacy v2-v4 global entities migrate into v5 sidecars.
- Added bounded entity AABB movement, one-block stepping, local stuck avoidance,
  attack line-of-sight checks, separate hostile/passive caps, Peaceful cleanup,
  and deterministic hostile distance despawning.
- Replaced bow and skeleton hitscan attacks with swept ballistic arrow entities.
  Arrows handle gravity, block/entity collision, damage and knockback, persist
  across chunk unloads, expire after 60 simulated seconds, and player-fired
  arrows can be recovered after embedding.

## Remaining

- Add crop hydration/saplings, bed nighttime validation, water movement, armor
  HUD, durability bars, and remaining interaction polish.
- Add broader integration tests for chunk override application, death recovery,
  crop growth, live entity/container interaction, and the complete progression
  path.

## Validation

- Release configure/build: passed.
- CTest: passed, 12/12.
- `git diff --check`: passed.
- OpenGL startup under Xvfb: passed on Mesa OpenGL 4.6 with 4× MSAA and shader
  gamma fallback.
- Existing FastNoiseLite release-LTO warnings remain unchanged.

## Recovery

Read `AGENTS.md`, `PLAN.md`, `PROGRESS.md`, and `CLAUDE.md`; inspect the dirty
worktree; then configure `build-local`, build with `-j2`, and run all CTests
before resuming the remaining stages.
