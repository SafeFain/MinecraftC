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
  traversal, conservative subcell occupancy, directional coverage and footprint-based
  radiance filtering, linear atlas reflectance, independent colored emission and
  sun/moon direction injection, partial GPU volume updates, time-based temporal
  accumulation, validated alternating-tile history reuse, conservative empty-cell
  hierarchy skips and surface-guided upsampling. Reflected and emissive bounce have
  independent gains; coarse source rebuilds and smooth environment relights are bounded.
  [GI benchmark and validation](gi-performance.md) records frame percentiles and
  asynchronous GPU phases, without claiming unmeasured hardware gains. GI preserves the legacy path when disabled. GI caches survive unrelated
  settings changes. It deliberately retains voxel and flat cirrus
  clouds instead of adding volumetric clouds. The basic page owns five complete
  quality presets plus an automatic Custom state; the advanced page independently
  toggles and scales Bloom, AO, sun shafts, reflections, atmosphere, material
  motion, ambient particles, and GI.
- New and existing worlds show loading progress until the selected render radius is
  ready. After chunk preparation, enabled far-terrain LOD has a separate loading
  status with its coverage percentage. Exact LOD cache extraction and refinements
  of already visible tiles continue during gameplay after initial coverage is ready.
  New worlds persist spawn caches; compatible caches bypass later generation.
- One world seed deterministically controls terrain, biome, cave, ore, surface
  decoration, tree, and overworld-structure placement.
- Generation version 19 retains 15 fixed-anchor, boundary-blended macro terrain
  archetypes, finite volcanic overlays, and a local-level drainage graph across
  Y=-64..319 while preserving the v6 hybrid-cave algorithm, and includes seventeen
  deterministic overworld structures (plains/desert/taiga/snow/savanna villages, traveler huts,
  abandoned camps, desert wells, igloos, ruined towers, lumber camps, desert
  temples, jungle ruins, swamp huts, mountain watchtowers, stone circles,
  abandoned farmsteads). Base cache revision 1 is part of the v19 cache key. Version 14 adds four
  deterministic cave biomes, large chambers/rifts, cave surface materials,
  supported cave flora, dripstone, crystals, and volcanic decoration while
  retaining neutral transition caves. Version 13 seals the wall/roof
  envelopes of village houses, traveler huts, and igloos. Version 11 added
  mountain-family emerald ore and deterministic once-only village population
  requests; version 12 increases accepted plains/desert village density. Heaven
  generation is version 9. Deterministic five-level Skyway Shrines
  and the Heaven-only Starstep Scepter provide safe vertical travel.
- 3×3 region generation uses padded world-coordinate sampling and a singleton
  fallback for incomplete regions.
- 30 surface biomes, five cave biomes, seven vegetation/tree shapes, five ore
  types, and 274 serialized
  non-air block IDs, including level-based water/lava states and 50 oriented
  stair/slab states across five architectural material families.
- Opaque, cutout, and translucent rendering; greedy cubes and crossed plants.
- Scheduled level-based fluids, glass, ignition and chained TNT explosions,
  naturally generated flowers, and seeded moving render-only voxel clouds with
  world-aligned 32/64/128-block LOD cells beyond the selected exact-cloud radius,
  extending to 4096 blocks independently of terrain LOD.
- Separate nearest-filtered block, 292-item, and entity atlases come from JSON.
  Block-item icons share world material mappings and retain runtime fallbacks.
- Independent 0-15 sky/block light, smooth vertex lighting/AO, cross-chunk
  propagation, day/night sky, fog, tile-safe mipmaps, sRGB, and configurable
  1×/2×/4× MSAA through the visual-quality setting.
- First-person movement, collision, flight, raycast interaction, configurable
  auto jump, hotbar, settings, and a creative inventory with Minecraft-style
  category tabs. F5 cycles first person
  and front/back third person; the animated player and first-person right hand
  display the selected block or item and swing during interaction.
- All five tiers of swords, pickaxes, axes, shovels and hoes have complete
  block-style held models, alongside bows, shields, flint and steel and the
  Starstep Scepter. Both perspectives show continuous bow bending, string draw
  and a nocked arrow while charging, and the offhand shield smoothly raises
  during blocking. Inventory icons retain their existing appearance.
- Creative and Survival share the persisted player hotbar and backpack. New
  worlds start empty; the Creative catalog supplies full stacks into the real
  hotbar, and its player-inventory tab exposes the same storage used by containers.
- The bindable Drop Item action defaults to Q and removes one item from the
  selected real hotbar stack in Creative and Survival. Creative hoes can till
  valid grass/dirt top faces without durability loss.
- Dropped items show their own block, weapon/tool model or extruded item icon,
  with centered rotation, a gentle hover and world lighting. Dropped bows stay
  in their rest pose independently of the player's charging animation.
- Java-style non-debug keyboard and mouse controls are configurable: T opens
  chat, `/` opens a slash-prefilled command, middle click picks a block, F swaps
  the selected/offhand stacks, F5 changes perspective, F11 toggles fullscreen,
  and Ctrl+Q drops a whole stack. Inventory screens support hovered-slot 1-9/F/Q
  shortcuts, Shift-click, double-click gathering, and left/right drag
  distribution; Creative additionally supports middle-click cloning/filling.
- Bows charge while Use is held and fire on release. Charge controls speed,
  damage, first-person FOV narrowing, and a collision-clipped trajectory preview;
  player and skeleton arrows share gravity, inherited shooter velocity, and
  analytic frame-rate-independent projectile motion. Continuous parabolic contact
  selects the nearest block/entity impact, respects stairs/slabs and player pose
  height, and shares exact block tracing with the preview; shields reflect the
  velocity at contact and arrows retain their impact orientation when embedded.
- Persistent Survival, Creative, and Spectator worlds with crafting, furnaces,
  containers, farming, weather, combat, passive/hostile entities, and commands.
  `/locate structure` queries the nearest deterministic structure in the
  Overworld or Heaven;
  command input supports Java-style Tab/Shift+Tab completion and a touch Tab.
- Villagers and zombie villagers have spawn eggs and persistent original models.
  Runtime villages include living adult/child villagers with exclusive reachable
  beds; workstation regions merge dynamically. Thirteen professions trade five-level
  offers with bounded reputation/demand pricing and twice-daily workplace restocking.
  Five village styles receive deterministic first-load populations independently
  of spare beds; zombie infection preserves resident data. See the v19 lifecycle below.
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
- Modern block UI uses charcoal rounded surfaces, green action/focus accents,
  smooth baseline-aligned Noto text and short interaction/opening transitions.
  Menus, inventories, containers, trades, chat, HUD and touch share one theme;
  narrow settings/forms scroll, world cards paginate, and portrait equipment
  moves above the nine-column backpack. Item and survival icons retain pixel art.
  Hover/focus borders, pressed surfaces and quick activation pulses use eased,
  frame-time feedback with fixed hit rectangles. Keyboard/controller focus and
  pointer hover switch with input; Tab/Shift+Tab traverse menu controls. Touch
  menus show contact feedback, cancel taps on scrolling/focus loss and clear
  hover on release. Inventory controller focus follows fitted slots on resize.
- Ten UI languages (Arabic, Simplified Chinese, English, French, German, Japanese,
  Korean, Portuguese, Russian, Spanish) directly selectable in a language submenu
  opened from the main menu, with native names, current-language highlighting,
  immediate saving and compact-screen scrolling in English-name order; Arabic strings are shaped (joined forms, lam-alef
  ligatures, right-to-left runs) at runtime with a bundled Noto Naskh fallback
  face. The main menu also provides refreshable world selection with confirmed
  deletion and a localized About page linking to the project repository.
- Optional SDL3 Audio playback with original asset-backed menu/gameplay music
  and procedural weather/effect synthesis; persistent master, music, weather,
  and sound-effect volume controls cover every mixed game sound. Audio
  initialization failure does not prevent the client from starting.
- Determinism/boundary tests for caves and complete world generation.

## Crafted masonry and dyed wool

18 crafted full-cube blocks add stone brick variants (plain, mossy, cracked,
chiseled), bricks, polished granite/basalt/limestone/tuff, deepslate bricks,
and sandstone/cut sandstone/smooth sandstone plus red/yellow/blue/green/black wool.
Eight material items add clay balls, bricks, five dyes and bone meal.
All have original generator-produced 16×16 materials/icons, ten-language names,
creative entries and survival crafting/smelting paths. Masonry requires at least
a wooden pickaxe and drops itself; dyed wool retains white wool mining/fire rules.
This crafted pack introduced no generation or save-format changes.
The subsequent natural biome expansion advances Overworld generation to v15;
At that stage Heaven remained v8 and save format remained v12; current versions are v9/v14.

`/give <item_name> [1..64]` supplies the current player's inventory when cheats
are enabled, defaulting to one item. Names use lowercase English with underscores,
for example `mossy_stone_bricks`, `red_wool` and `bone_meal`, with Tab completion.
Full inventories retain their contents; feedback reports the quantity actually added.

## Natural surface ecology (generation v15)

24 collectable/placeable blocks enrich all 30 Overworld surface biomes:
rooted dirt, leaf litter soil, peat, silt, dry grass block, permafrost, blue ice,
shale, red sandstone, ochre/white terracotta, volcanic ash, coral rock, fern,
dead bush, dry grass, brown/red mushrooms, lavender, bellflower, alpine flower,
tropical flower, cattail and beach grass. Soil/stone patches use smooth
world-coordinate seed fields; red canyon/badlands layers follow world Y.
Existing sandstone/calcite and other materials are reused where appropriate.

Plants use single-cell cutout models and shared substrate checks for generation
and placement; cattails generate only on dry banks near water. All drop their
own item; rock/terracotta requires a wooden or better pickaxe. No recipes,
growth, underwater waterlogging, cave-ecology or Heaven expansion is added.
Snow cover is resolved by the same surface rules used by near terrain and LOD.

The v15 content remains available in current v19 worlds. Old generation versions
are incompatible; current compatibility rules are described below.
Seed layout, terrain heights, tree/structure anchors and Heaven v8 are retained.


## Surface and cave ecology (generation v16)

28 additional collectable blocks provide 18 surface and 10 cave materials/plants:
andesite, diorite, gneiss, marble, laterite, red clay, cracked mud, salt crust;
clover, heather, wild mint, nettle, desert flower, small cactus, reed flower,
tundra moss, fallen twigs and jungle fern; cave moss, wet limestone, gypsum,
amethyst/quartz blocks, iron-stained rock, sulfur rock, amethyst/quartz clusters
and cave glowshroom. The fungus emits level-5 light; the new crystals do not emit.

Materials form continuous world-coordinate patches. Rubble has one candidate per
16×16 cell at 20% probability, a cross within a 3×3 footprint, and a maximum
height of two. It avoids steep/wet ground, tree canopies and structure reservations.
Cave crystal groups have one candidate per 8×8 cell per floor plane at 15%
probability and at most five supported single-cell clusters. Each chunk reconstructs
anchors independently, including negative coordinates and boundary crossings.
Decorations occupy dry air and never replace water or occupied feature space.
Wild mint and reed flowers generate near water. Shared substrate checks apply
to generation and manual placement; crystals require natural rock support.
Small cactus is non-colliding and causes no contact damage.

New materials use opaque cubes; plants and crystal clusters use crossed cutout
meshes. All drop themselves and stack to 64. Soil/salt/cave moss uses shovel
acceleration without a harvest requirement; rock requires wooden or better
pickaxes; crossed decorations can be collected by hand. No recipes or growth
mechanics are added. Ten language catalogs include all new items.

Only generation v19 worlds can load. v14/v15 and other previous or future
versions stay listed as incompatible, retaining their untouched files. No world
migration remains. The v16 update retained save format v12, seed layout 5, terrain heights, cave-carving
algorithms, tree/structure anchors and Heaven generation v8. The v17 update below
changes Heaven and block encoding while retaining Overworld generation output.

## Heaven ecology and resources (generation v17 / Heaven v9)

- Sixteen appended blocks: Moonstone, Skystone, Aether Moss, Glimmer Silt,
  Star Crystal Ore, Skyroot Planks, Cloudstone/Sunstone/Moonstone Bricks,
  Star Crystal Lamp, Sky Fern, Dawn Bell, Moonflower, Glimmer Reed,
  Cloudberry Bush and Hanging Cloud Vine.
- Five appended items: Star Crystal Shard, Cloudberry, Roasted Cloudberry,
  Cloudberry Bread and Roasted Glowshroom. All 286 items have icons and names
  in all ten languages; 268 non-air block IDs retain existing numeric values.
- Existing eight biomes/five layers gain clustered vegetation, distinct soils,
  ore pockets, rock pillars, sealed one-block-deep pools and supported underside
  vines. Primary island shapes and shrine transport remain; ecology and fine LOD
  share coordinate-owned feature sampling. Crafted blocks do not generate naturally.
- Skyroot logs yield four Skyroot Planks, usable alongside oak planks in basic
  recipes and as furnace fuel. Four base stones yield four corresponding bricks.
  Four shards yield one Star Crystal (reversible); four Cloudstone Bricks around
  a crystal yield one level-15 Star Crystal Lamp. Moonflowers emit level 4.
- Hanging Cloud Vines bear cyan-white stardew beads and emit level 12 beneath
  Skyroot Grove / Cloudbloom Fields islands. Only the beads carry material
  emission; stems retain their natural color. Existing vines gain normal block
  lighting and cyan GI without changing generated blocks, save IDs or versions.
- Star Crystal Ore requires at least a wooden pickaxe and drops two shards;
  breaking a Cloudberry Bush yields two berries without a renewable bush drop.
  One bread plus two berries yields Cloudberry Bread; berries and existing Heaven
  Glowshrooms roast in furnaces. Food/saturation values are 2/0.4, 4/1.2, 7/6,
  and 4/2 for berries, roasted berries, berry bread and roasted glowshrooms.
- Existing Heaven loot gains shards, berries and bricks while the origin shrine
  retains its guaranteed Starstep Scepter. No new mobs, tool tiers or growth timers.
- Save v14 persists a world-wide day/night duration (default 1200 seconds), changed
  using `/gamerule DayNightDuration <seconds>` with cheats enabled; v2-v13 default
  to 1200 seconds. Save v13 introduced 16-bit IDs, retained in v14; packed light remains byte-sized.
  Generated caches support 16-bit raw/RLE streams, and LOD cache revision is 5.
  Old metadata remains readable/listed; worlds outside global generation v19
  remain incompatible and are never migrated or rewritten by refused loads.
  Seed layout, Overworld generation output and application VERSION are unchanged.

## Overworld structures (generation v18)

Global generation v18 adds six structures, bringing the Overworld total to 14.
Each has seeded rotation, mirroring and material or damage variants; generation,
reservations and locate use world-coordinate hashes. Existing structure and loot
enum values are preserved. All new structures reuse existing blocks and supplies;
there are no new mobs, villager population requests, traps, or items.

| Locate name | Biomes | Layout / supplies | Cell / chance / radius / relief / height |
|---|---|---|---|
| `desert_temple` | Desert | Tiered sandstone temple; hall, five-step descent to a shallow chamber; two tower-loot chests | 256 / 35% / 10 / 3 / 12 |
| `jungle_ruins` | Jungle, Karst Forest | Mossy courtyard, broken gateways, sealed side room; one tower-loot chest | 192 / 25% / 8 / 3 / 9 |
| `swamp_hut` | Dry Swamp ground | Raised timber house, piles, three-step entrance and workbench; one traveler-hut chest | 128 / 20% / 6 / 2 / 10 |
| `mountain_watchtower` | Mountains, Hills, Rocky Steppe, Limestone Highlands | Stone/wood tower, four connected stair flights and viewing platform; one tower-loot chest | 192 / 20% / 6 / 5 / 18 |
| `stone_circle` | Meadow, Alpine Tundra, Rocky Steppe | Varied stone pillars, ruined lintels and central altar; one tower-loot chest | 128 / 15% / 7 / 3 / 7 |
| `abandoned_farmstead` | Plains, Sunflower Plains, Savanna, Dry Woodland | Damaged farmhouse, crop plot, open shed and furnace; one village-loot chest | 160 / 15% / 10 / 3 / 9 |

Chance is a per-cell prefilter, not the final world density. Biome, spacing,
village-priority overlap and full-footprint terrain checks still apply. Sites
exclude rivers and submerged ground; graded foundations follow the authoritative
surface sampler. Interior terrain/foliage is carved before walls and stairs.
Temple chambers extend at most five blocks below the surface, with build limits
checked. All supplies and the tower platform are accessible by walking stairs.
Chests/furnaces use the existing pending registration channel; consumed supplies
stay consumed across unload and reload. Commands also accept `minecraftc:` names.

Global generation and base-cache keys advance to v18/revision 1. Terrain heights,
cave carving, ore algorithms, seed layout 5, Heaven v9, block IDs, save format v16
and application VERSION stay unchanged. Added reservations and overlaps can change
local trees and other small structures. At that release, only v18 worlds loaded; older/future worlds
remain listed and refused without rewriting or migration.

## Village lifecycle (generation v19)

- Five biome styles retain the 512-block candidate grid. Four deterministic
  anchor attempts choose the first feasible layout per cell, maintaining village
  availability despite larger footprints. Pure per-worker caches are bounded;
  infeasible village candidates cannot suppress real neighbors.
- Shared layouts supply 12/18/24 buildings, 20/30/40 initial residents and
  24/36/48 beds; maximum reservations are 48/64/80 blocks from the anchor.
  Seeded staggered lots, varied house orientations and variable lane spacing avoid identical housing rows.
  Curved three-block roads connect homes, profession rooms, lit markets and irrigated
  wheat fields; snow farms have glass covers. Terrain checks cover lots and roads,
  avoiding aquatic/cliff layouts without flattening the entire reservation.
- Existing seven professions append Fisherman, Librarian, Cartographer, Cleric,
  Butcher and Mason. Barrel, Lectern, Cartography Table, Brewing Stand, Smoker and
  Stonecutter are craftable full-cube workstations; they confer professions only,
  without adding storage, brewing, map, enchanting or special crafting screens.
  All offers use existing items. Traded professions remain locked.
- Adults collect wheat/seeds/bread into eight slots. Two willing nearby adults
  consume three bread or twelve wheat each only after a reachable free bed and
  successful birth; parent cooldown is 300 simulated seconds. Children grow after
  1200 simulated seconds, cannot work/trade/breed, and have half-scale bodies.
- Farmers discover bounded field slices, harvest mature wheat and retain seeds
  for replanting, bake three wheat into bread, and share surplus bread nearby.
  Generated fields enter the existing random-tick path through bounded discovery.
  `mob_griefing=false` prevents harvesting/replanting and dropped-food collection.
- Personal single-player reputation is -100..100. Successful trades grant up to
  five points per day; player injury/kills subtract 10/25, shared with same-village
  witnesses within 16 blocks and line of sight. Reputation returns one point toward
  zero daily. Prices adjust up to ±25%, with demand adding up to 25%; restocking
  reduces demand. UI quotes and transactional inventory deductions share one function.
- Villagers work in the existing two restock periods, gather in the afternoon and
  return home at night. Long destinations use bounded intermediate navigation;
  unloaded facilities retain claims. Lifecycle schedules at most eight residents
  per second and 64 farm probes per scheduled farmer, alongside existing AI budgets.
- Fully loaded villages with at least five bed-owning adults gain one iron golem.
  Defenders use navigation, sight-gated melee and normal damage/save paths, patrol
  near home and do not naturally despawn. Death imposes a 600-second village cooldown.
  Low-reputation Survival players can become targets; Creative/Spectator cannot.
- Save v17 persists food, growth/cooldowns, reputation and demand for metadata and
  chunk entities; v2–v16 remain readable with defaults. No offline growth or cooldown
  catch-up. No raids, zombie curing, gossip simulation, or automatic construction.
  Only global v19 worlds load; refused older/future worlds are never rewritten.
  Heaven v9, terrain/cave algorithms, existing IDs and application VERSION remain unchanged.

## Classic fishing

- Craft a Fishing Rod at a workbench with three sticks on a diagonal and two
  strings down the right edge; the mirrored recipe also works. Rods stack to one
  and have 64 durability. They appear in the Creative Tools catalog; fish appear
  in Food. Commands use `fishing_rod`, `raw_cod`, `raw_salmon`, `cooked_cod`, and
  `cooked_salmon`.
- Press the bound Use action (mouse, keyboard, controller or touch) to cast;
  press it again to reel. Watch the approaching splashes and red/white bobber.
  When it sinks, the localized bite prompt, sound and optional controller rumble
  signal a one-second catch window. Missing the window restarts the wait.
- Base waiting time is 5–30 seconds, followed by 1–2 seconds of approaching fish.
  Exposed rain makes waiting progress 25% faster; covered water halves progress.
  Natural and artificial water works in both dimensions, including flowing water.
  Lava does not. Each player has one bobber, limited to 32 blocks.
- Open water yields fish/junk/treasure at 85%/10%/5%. Other pools yield 90%/10%/0%.
  Treasure requires a loaded 5×5 source-water surface, another source-water layer
  underneath, and two layers of air above. Fish are 60% cod and 40% salmon.
  Junk is equally weighted sticks, string, bones and rotten flesh; treasure is
  equally weighted unenchanted full-durability bows, rods and emeralds.
- Each catch produces one normal item drop pulled toward the player. A full
  backpack leaves the item in the world. Successful fishing costs one rod
  durability; reeling a stuck bobber costs two; empty reels cost none. Creative
  rods do not wear out.
- Raw cod/salmon restore 2 hunger and 0.4 saturation. A normal 200-tick furnace
  recipe cooks them; cooked cod restores 5/6 and cooked salmon 6/9.6.
- First and third person show the rod, float and curved line attached to its tip,
  including cast/reel swing. Bite effects work with enhancements disabled and
  audio initialization failure does not affect fishing.
- Pausing freezes fishing. Opening inventory follows normal world ticking.
  Switching slots, dropping/losing the rod, sleeping, death, Spectator mode,
  dimension changes, world exit, unloaded water and excess distance cancel it.
  Rods and fish persist using save v14; bobbers and timers are transient. World
  fishing introduced no generation change (v17 at introduction). No enchantments, experience, bait or entity hooking.

### GameRule commands

Complete Java 1.21.11 GameRule registry (59 rules plus DayNightDuration), typed
query/set, old/new names, Tab completion and `/help gamerule [<rule>]`. Rules
are world-wide and persisted in save v17 (introduced in v15); v2–v14 retain readable defaults and
existing duration values. Missing/partial mechanics are explicit in feedback.
See [game-rules.md](game-rules.md) for the full table and limits.

### Plugin loader

ABI 1 supports optional compiled-in examples, desktop native plugins and portable
data packages; frozen block/item/recipe registries, shared material atlases, controlled
gameplay events/operations, environment colors and HUD. Save v16 resolves namespaced
content and checks exact gameplay package requirements. [Installation and SDK](plugins.md).
