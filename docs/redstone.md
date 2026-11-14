# Redstone circuits

Redstone runs on the authoritative World at20 game ticks per second. A redstone
tick is two game ticks. Each dimension owns its scheduler and never powers another
dimension. Replicas send interactions and receive committed terrain/motion state.

## Building circuits

Place redstone dust on a supported top face. It branches horizontally and follows
unobstructed one-block steps; strength falls by one per dust connection. Interact
with dust to toggle its automatic cross/point setting. Water and support removal
break attached components and produce the normal material drop.

Levers and redstone blocks supply continuous power. Existing wood/stone buttons,
arrow-triggered wood buttons, wood/iron doors and TNT use the same directional
signal queries. Multiple inputs use their maximum strength.

- Torches invert their support's power after2 game ticks. Eight off transitions
  within60 active ticks burn a torch out for160 active ticks.
- Repeaters face from input to output, restore strength15 and cycle through1–4
  redstone ticks on interaction. Side repeaters/comparators lock them. A short
  on pulse receives the configured minimum output duration.
- Comparators face from rear to front. Interaction toggles comparison/subtraction;
  rear and side signals are sampled with a2-game-tick delay. Chests, furnaces and
  brewing stands report normalized inventory fullness, including through one
  conductive block; strength15 on that block takes precedence. Inventory changes
  invalidate comparator readings without firing observers.
- Observers watch their front and pulse out their back:2 ticks of delay followed
  by2 ticks at strength15. Repeated notifications coalesce; chunk loading does not
  count as a block change.
- Lamps turn on immediately when their circuit commits, and turn off after4 ticks
  without power. TNT is handed to the existing authoritative ignition queue once.

## Pistons

Ordinary and sticky pistons work in all six directions. They validate the entire
chain before moving up to12 blocks;13 movable blocks, unavailable chunks, world
bounds or an immovable block block the push. Containers, spawners, bedrock,
obsidian, portals and protected workstations block movement. Fragile attachments
break once; ordinary movable block states retain their orientation/state.

Movement takes two game ticks. Ordinary pistons retract without pulling; sticky
pistons pull one movable block. Collision and entity/item displacement use the
simulation's swept shapes. Rendering interpolates those results. Motion reserves
all source/destination cells and pins their chunks until it completes. The short
reservation prevents mining a second reward from an in-flight block/base.
Spectators are excluded from sweeps; static obstacles stop displacement. Source,
destination and border meshes are refreshed together before the next draw.

An atomic per-piston journal owns blocks in flight and their progress. It is
written before voxel changes; every affected chunk load overlays the same record.
Completed records are removed only after all affected chunk files are saved. The
older aggregate journal remains readable. Saves finish an incomplete logical
circuit tick before flushing its block states/timers. A loaded motion resumes
only when its required chunks are available; loading never generates a neighbor
solely to run a circuit.
Explicit plugin recovery settles surviving in-flight content in its staged copy
and retires the journals, leaving the original world available for recovery.

LAN transfers the complete cross-chunk result and motion snapshot as one logical
transaction. Large payloads use bounded fragments and publish only after complete
validation/assembly. Duplicate fragments have no additional world side effects;
stale epochs are ignored and malformed/out-of-order fragments are rejected.

## Survival and compatibility

Generation23 adds normal/deepslate redstone ore with independent seeded veins,
iron-pickaxe harvesting, XP, Fortune and Silk Touch. Lit ore emits light, not a
redstone signal. Underground slime chunks provide the three slime sizes and
slimeballs; Peaceful suppresses spawning/splitting. Sticky pistons and all circuit
components have survival recipes; redstone blocks can be packed/unpacked.

IDs are appended. World format25 and LAN10 carry circuit state; generation23 is
required for play. Profile7, plugin ABI1, Heaven9 and Nether terrain1 are unchanged.
Older generation worlds remain inspectable and incompatible; refusal does not
write or migrate their files.

The behavior target is common Java1.21.11 circuits. Quasi-connectivity, zero-tick
or duplication exploits, update-order tricks, sticky short-pulse block spitting,
slime-block structures and swamp/moon slime spawning are outside this feature.

## Validation

World traces cover default and tiny work budgets, source removal, component
identity, signal timing, comparisons, burnout, motion, boundaries, support drops
and save/reload. Separate tests cover generation/material/entity rules, atomic
wire codecs, two real LAN clients with reconnect, and Vulkan pixel readbacks for
wire strength, repeater settings, moving blocks and the slime model. Full execution
results and warnings belong in the active task record and its final archive.
