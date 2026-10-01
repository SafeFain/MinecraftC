# Project Feature Reference

Detailed current feature facts retained from AGENTS.md during the 2026-10-01 organization.
These describe project behavior; task state belongs in PLAN.md and PROGRESS.md.

- Infinite chunk loading around the player with a runtime-selectable render distance.
- Independent, persistent far-terrain LOD progressively refines deterministic
  approximations with full exact columns at level zero and representative exact
  columns at coarser cell centers. It exposes separate distance, load, and
  precision controls.
- Video settings include a persistent continuously adjustable 30-200 FPS limit
  and an opt-in enhanced-visuals profile. The latter progressively adds
  multiscale bloom, analytic atmospheric scattering, surface-data AO and sun
  shafts, tiered water reflections, soft-shadow filtering, material motion, and
  budgeted Overworld ambience by visual-quality tier. Very High and Ultra add
  voxel-irradiance GI with camera-centered clipmaps, bounded cone-direction
  traversal, conservative coarse opacity, local radiance filtering, partial GPU
  volume updates, time-based temporal accumulation and surface-guided upsampling
  while preserving the legacy path when disabled. GI caches survive unrelated
  settings changes. It deliberately retains voxel and flat cirrus
  clouds instead of adding volumetric clouds. The basic page owns five complete
  quality presets plus an automatic Custom state; the advanced page independently
  toggles and scales Bloom, AO, sun shafts, reflections, atmosphere, material
  motion, ambient particles, and GI.
- New and existing worlds show loading progress until the selected render radius is
  ready. New worlds persist spawn caches; compatible caches bypass later generation.
- One world seed deterministically controls terrain, biome, cave, ore, surface
  decoration, tree, and overworld-structure placement.
- Generation version 14 routes 15 fixed-anchor, boundary-blended macro terrain
  archetypes, finite volcanic overlays, and a local-level drainage graph across
  Y=-64..319 while preserving the v6 hybrid-cave algorithm, and adds eight
  deterministic overworld structures (plains/adobe villages, traveler huts,
  abandoned camps, desert wells, igloos, ruined towers, lumber camps). Base
  cache revision 1 is part of the v14 cache key. Version 14 adds four
  deterministic cave biomes, large chambers/rifts, cave surface materials,
  supported cave flora, dripstone, crystals, and volcanic decoration while
  retaining neutral transition caves. Version 13 seals the wall/roof
  envelopes of village houses, traveler huts, and igloos. Version 11 added
  mountain-family emerald ore and deterministic once-only village population
  requests; version 12 increases accepted plains/desert village density. Heaven
  structure generation is version 8. Deterministic five-level Skyway Shrines
  and the Heaven-only Starstep Scepter provide safe vertical travel.
- 3×3 region generation uses padded world-coordinate sampling and a singleton
  fallback for incomplete regions.
- 30 surface biomes, five cave biomes, seven vegetation/tree shapes, five ore
  types, and 182 serialized
  non-air block IDs, including level-based water/lava states and 50 oriented
  stair/slab states across five architectural material families.
- Opaque, cutout, and translucent rendering; greedy cubes and crossed plants.
- Scheduled level-based fluids, glass, ignition and chained TNT explosions,
  naturally generated flowers, and seeded moving render-only voxel clouds with
  world-aligned 32/64/128-block LOD cells beyond the selected exact-cloud radius,
  extending to 4096 blocks independently of terrain LOD.
- Separate nearest-filtered block, 182-item, and entity atlases come from JSON.
  Block-item icons share world material mappings and retain runtime fallbacks.
- Independent 0-15 sky/block light, smooth vertex lighting/AO, cross-chunk
  propagation, day/night sky, fog, tile-safe mipmaps, sRGB, and configurable
  1×/2×/4× MSAA through the visual-quality setting.
- First-person movement, collision, flight, raycast interaction, configurable
  auto jump, hotbar, settings, and a creative inventory with Minecraft-style
  category tabs. F5 cycles first person
  and front/back third person; the animated player and first-person right hand
  display the selected block or item and swing during interaction.
- Creative and Survival share the persisted player hotbar and backpack. New
  worlds start empty; the Creative catalog supplies full stacks into the real
  hotbar, and its player-inventory tab exposes the same storage used by containers.
- The bindable Drop Item action defaults to Q and removes one item from the
  selected real hotbar stack in Creative and Survival. Creative hoes can till
  valid grass/dirt top faces without durability loss.
- Java-style non-debug keyboard and mouse controls are configurable: T opens
  chat, `/` opens a slash-prefilled command, middle click picks a block, F swaps
  the selected/offhand stacks, F5 changes perspective, F11 toggles fullscreen,
  and Ctrl+Q drops a whole stack. Inventory screens support hovered-slot 1-9/F/Q
  shortcuts, Shift-click, double-click gathering, and left/right drag
  distribution; Creative additionally supports middle-click cloning/filling.
- Bows charge while Use is held and fire on release. Charge controls speed,
  damage, first-person FOV narrowing, and a collision-clipped trajectory preview;
  player and skeleton arrows share gravity, inherited shooter velocity, and
  analytic frame-rate-independent projectile motion.
- Persistent Survival, Creative, and Spectator worlds with crafting, furnaces,
  containers, farming, weather, combat, passive/hostile entities, and commands.
  `/locate structure` queries the nearest deterministic structure in the
  Overworld or Heaven;
  command input supports Java-style Tab/Shift+Tab completion and a touch Tab.
- Villagers and zombie villagers have spawn eggs and persistent original models.
  Runtime villages require a living villager with exclusive reachable bed and
  workstation claims; intersecting POI subchunk regions merge dynamically.
  Seven professions trade fixed five-level emerald offers and restock twice per
  day at their workstation. Plains/adobe village beds receive deterministic,
  revisioned first-load population; zombie infection preserves trade data.
- Living mobs share bounded incremental ground navigation with collision-shape
  support, physical jumps, hazard avoidance, and block-revision path invalidation.
  Near/far decisions run at 10/2 Hz; target acquisition requires sight and retains
  the last seen position for three seconds. Animals flee injury, villagers avoid
  hostiles and navigate to POIs, and skeletons maintain shooting distance.
  Navigation, target IDs, and AI statistics are transient and never serialized.
- Java 1.9-style combat uses per-item attack speed, quadratic 20%-100% attack
  charging, weak/strong/critical/sweeping hits, sprint knockback, shared hurt
  immunity, armor toughness, delayed shields, and combat durability/exhaustion.
- Survival hunger uses hidden saturation and exhaustion, saturation-funded fast
  regeneration, ordinary regeneration, starvation floors, and activity-based
  exhaustion measured from actual movement.
- Ten UI languages (Arabic, Simplified Chinese, English, French, German, Japanese,
  Korean, Portuguese, Russian, Spanish) selectable from the main menu in
  English-name order; Arabic strings are shaped (joined forms, lam-alef
  ligatures, right-to-left runs) at runtime with a bundled Noto Naskh fallback
  face. The main menu also provides refreshable world selection with confirmed
  deletion and a localized About page linking to the project repository.
- Optional SDL3 Audio playback with original asset-backed menu/gameplay music
  and procedural weather/effect synthesis; persistent master, music, weather,
  and sound-effect volume controls cover every mixed game sound. Audio
  initialization failure does not prevent the client from starting.
- Determinism/boundary tests for caves and complete world generation.
