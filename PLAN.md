# Task Plan

## Task Summary

Add a persistent Survival mode alongside Creative mode, based on the scoped
Minecraft Java 26.2 overworld ruleset. The game remains an open-ended sandbox
without Nether, End, bosses, villages, redstone, enchanting, or trading.

The current focused change replaces the fixed mode launch buttons with a
Minecraft-style singleplayer flow: select an existing save, or create a named
save with a chosen game mode and optional numeric world seed. Seed selection is
a creation-time world property, not a global runtime setting.

Creative flight follows the familiar Minecraft control model: double-tap jump
to toggle, jump/sneak to rise and descend, sprint for fast flight, horizontal
movement based on camera yaw, solid-block collision, and automatic flight exit
on landing.

World creation stores an Allow Cheats rule. Pressing T opens the command line;
`/gamemode 0`, `/gamemode 1`, and `/gamemode 3` select Survival, Creative, and
Spectator respectively; `/tp x y z` teleports to finite absolute coordinates.
Spectator always flies without collision and cannot target or interact with
blocks, entities, or inventories.

Player simulation and saved positions use double precision. Rendering uses a
camera-relative floating origin so GPU transforms remain near zero even at
million-block coordinates.

## Locked Decisions

- Preserve every existing serialized `BlockId`; append Survival blocks only.
- Keep world generation version 2 and the existing 0–127 world height.
- Treat the ore-density reduction as a corrective version-2 balance change so
  existing saves receive stone-dominant terrain in newly generated areas.
  Saved sparse chunk overrides remain authoritative.
- Use stable independent `ItemId` values and ruleset version 2602.
- Support Survival and Creative saves separately, with Normal as the default
  difficulty and Peaceful/Easy/Hard represented by the saved rules.
- Death drops inventory at the death point; item age advances only while the
  entity is simulated.
- Use original/procedural or compatibly licensed assets, never copied Mojang
  assets.

## Implementation Stages

1. Stable item, inventory, rules, save format, and sparse chunk overrides.
2. World catalog and world-selection/create flow.
3. Limited inventory, mining, crafting, furnaces, chests, tools, armor, and
   consumable placement.
4. Health, hunger, environmental damage, beds, farming, lighting, and death.
5. Item entities, four passive mobs, four hostile mobs, combat, spawning,
   persistence, and dynamic rendering.
6. UI polish, complete automated coverage, OpenGL smoke testing, and asset
   provenance.

The current UI-polish work includes keeping pointer hit testing in framebuffer
coordinates so inventory drag-and-drop remains correct on scaled displays.

The current systems pass completes persistent single chests and furnaces,
cross-chunk torch light, light-gated hostile spawning, partitioned entity
loading, bounded voxel movement, deterministic local obstacle steering, and
recoverable ballistic arrows. Save format 5 keeps v2-v4 metadata readable and
stores block entities and simulated entities in per-chunk sidecar files.

The current survival-polish pass adds eight persistent farmland moisture
states, five growable saplings, daytime respawn-point setting and night-only
sleep, simplified water swimming, an armor HUD, and durability bars throughout
the inventory/container UI. These additions remain save-format 5 and world
generation version 2 compatible.

The current interaction-polish pass adds an integer-scaled procedural pixel UI,
a scrollable select-then-play world list, persistent global client options,
configurable keyboard/mouse/wheel gameplay bindings, pixel survival indicators,
unified item tooltips, and inventory quick-transfer, collection, and drag
distribution. Client options live in `saves/options.txt`, independently of
world saves.

## Completion Criteria

- A new player can gather wood, craft a table and tools, mine and smelt iron,
  obtain food and a bed, farm wheat, fight or avoid nighttime mobs, progress to
  diamonds, die/respawn, and continue after restarting the application.
- Unloaded/reloaded and negative-coordinate chunks retain block changes and
  block entities.
- Creative behavior remains available without Survival restrictions.
- Release build, all CTests, `git diff --check`, and an OpenGL startup smoke
  test pass.
