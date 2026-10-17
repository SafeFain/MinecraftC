# Plugin gameplay and presentation capabilities

ABI 1 remains unchanged. Include `minecraftc/extensions.h` and query extensions
through `MC_Host::query_extension` (version 1 unless specified below).
C++ helpers live in `minecraftc/plugin.hpp`.
The declarations are in [capabilities.h](../sdk/minecraftc/capabilities.h).
Unknown names/versions return null; every descriptor/output buffer needs its `size` field.
Successful calls return 0; failures return -1 and expose the reason through `last_error`.
Register during load/REGISTER only. Registration copies strings and arrays, validates references,
rolls back the entire package on failure and freezes before initialization.
Own content uses the package namespace; foreign gameplay references require a declared dependency.
Gameplay definitions participate in save requirements and LAN admission fingerprints.

| Extension | C++ helper | JSON arrays |
|---|---|---|
| `minecraftc.behaviors` | `behaviors()` | `block_behaviors`, `item_behaviors` |
| `minecraftc.structures` | `structures()` | `structures`, `loot_tables` |
| `minecraftc.player_actions` | `playerActions()` | `player_actions` |
| `minecraftc.environment` | `environment()` | `environments` |
| `minecraftc.presentation` | `presentation()` | `particles`, `held_models`, `playlists` |

See the complete [data example](../examples/plugins/capability_demo/content.json) and
[native example](../examples/plugins/capability_native/plugin.cpp). The data example is
opt-in in the plugin menu; restarting freezes its registered content.

## Block and item behavior

A zeroed block behavior is inert. Climbable blocks permit climbing while intersecting
and holding jump, limit falling speed and support sneak to hold position. Replaceable
blocks can be replaced by placement or fluids. `support`: 0 none, 1 supporting block below,
2 supporting block above. Placement checks the appropriate collision face; losing a resident supporting face removes
the dependent block under the existing block-drop rule. Unknown neighboring chunks never
remove supported blocks.
Fire encouragement/flammability are percentages 0–100; contact damage is 0–1000 per
half-second contact interval under normal Survival damage rules.
Harvesting requires item/count/replacement; inventory capacity is checked before replacing
and awarding the reward. Count must fit the registered stack limit (at most 64).
A reset of 1–72000 simulation ticks restores the original block while its chunk is resident;
remaining ticks persist and an intervening edit cancels the reset. Zero means no reset.
Blocks can bind a special use action. Items can bind an action and restrict use to a dimension.
Only owned registered blocks/items can receive behavior. Existing interaction PRE cancellation
still runs before plugin behavior.

### Behavior V2

Query version 2 as `MC_BehaviorsV2` (`behaviorsV2()`); its `v1` pointer retains
the frozen V1 table and descriptor layouts. V2 wraps the V1 behavior and adds
an explicit `fields` mask. An absent field inherits the ordinary behavior;
present zero light/fuel/tags or an empty drop list are intentional overrides.

- `MC_BLOCK_LIGHT`: light dampening 0–15, used by real chunk lighting.
- `MC_BLOCK_TAGS` / `MC_ITEM_TAGS`: `MC_TAG_*` bit flags for leaves, planks,
  fertile/sandy/natural-rock substrates, Heaven soil groups and plants. Leaves
  affect face data and default light dampening; item planks match plank-variant recipes.
- `MC_BLOCK_SUBSTRATE`: accept any matching `substrate_tags` below a plant.
  Generation, manual placement and resident support changes share this predicate.
- `MC_BLOCK_DROPS`: up to 16 break-drop entries, separate from use harvesting.
  Existing unbreakable/tool-tier checks run first. A rule succeeds when
  `randomValue % denominator < numerator`; its count is
  `minimum + randomValue % (maximum-minimum+1)`. No additional RNG is consumed.
  Denominators must be nonzero, probabilities bounded, and counts fit the item stack.
- `MC_ITEM_FUEL`: furnace fuel ticks 0–65535, checked before plank/tool defaults.

JSON uses the same validators through `light_dampening`, integer `tags`,
`substrate_tags`, `fuel_ticks`, and `break_drops` (entries contain `item`,
`minimum`, `maximum`, `numerator`, `denominator`). All fields participate in LAN
identity; invalid registration rolls back the package. Reserved Heaven bindings
are available only to the compiled-in official owner, preserving V1 namespace rules
for external plugins.

## Structures and chest loot

Structures use a dimension key/handle, revision, seed domain and square cell policy.
Cell size is 16–1024, chance 0–100, radius 0–64, height 1–128 and terrain tolerance 0–128.
The anchor is derived from world seed, key/domain and mathematical floor cell coordinates;
center/corner terrain samples reject underwater, out-of-height or uneven footprints.
Overlapping plugin footprints resolve by a deterministic total priority order.
Duplicate relative voxels are rejected. Foreign native block/loot output also requires declared dependencies.
Templates and native builders share the same accepted anchors and final overwrite phase.
Builtin decorations cannot overwrite the plugin's final voxels.

A template has at most 16384 relative blocks, with X/Z within radius and Y in `[0,height)`.
A native builder receives terrain-authority surface samples, generation parameters, anchor
and 64-bit variant; set output count, return 0 and stay within the same limits. It may run
concurrently or repeatedly on generation/cache/LOD workers. Use immutable data and coordinate
hashes; never use scheduling, mutable RNG, host mutations or GPU operations. Returned strings
must remain valid until the host copies the output after the callback.
Changing seeded output requires a structure revision and an explicit generation-version decision;
builtin generation remains Overworld v20 / Heaven v9 in this change.
`locate` and `/locate namespace:structure` use the same acceptance algorithm without loading
chunks; search radius is at most 8192 blocks.

Loot tables have a key/revision and 1–256 entries (chance 0–100, minimum ≤ maximum ≤ stack limit).
Each entry rolls a deterministic count and chest slot from its independent seed domain.
Only chest voxels may reference loot. Generated chests store loot key/revision, structure
key/revision, seed and initialized state. An already registered or restored chest never refills.
Save v21 persists these values, resource timers and dimension time overrides. v2–v20 metadata
can be inspected, but opening incompatible saves refuses without writes; no migration occurs.

## Accepted structures (extension version 2)

Query `minecraftc.structures` version 2 as `MC_StructuresV2`, or use
`structuresV2()`. Its `v1` member retains the frozen V1 table, including loot
registration and locate. V1 descriptors, acceptance and output are unchanged.
Register `MC_StructureDefinitionV2` with pure `placements` and `build` callbacks.
This native interface does not change the V1 JSON template schema.

`placements` receives the already-derived dimension seed, actual generation
parameters, undecorated terrain reader and signed cell coordinates. Cell size
is 16–1024. Return up to 16 final accepted instances per cell, each with an
anchor, variant and order. Anchor X/Z must belong to that cell under mathematical
floor division; Y must be in the world. The provider owns terrain acceptance,
retry and reservations, including interactions with its other structures.
The host does not reapply V1 chance, corner checks or footprint competition to
V2 instances; V1 reservations only compete with other V1 definitions.
Generation and locate enumerate the same instances without loading chunks.

Relative X/Z must fit radius 0–64. Inclusive signed `min_y`/`max_y` each lie in
[-383,383], span at most 384 blocks, and may cross the anchor in both directions.
For example, Skyway platforms need -88 through +90 including their standing
volumes. Emitted absolute Y outside [-64,319] is clipped, matching native
Heaven; horizontal world coordinates outside ±30000000 are rejected.

Writes sort ascending by definition `(order,key)`, then instance
`(order,cellZ,cellX,anchorX,anchorY,anchorZ,variant)`, then callback output order.
Duplicate instance identities in one cell are rejected; duplicate block
coordinates are allowed. Later writes own both the voxel and its metadata,
including AIR clearing an earlier chest. Only final voxel owners enter the
streaming metadata channel. `MC_StructureBlockV2.loot_seed` is exact, including
zero; V1 retains its automatic variant/position hash. Count remains bounded by
16384 blocks per instance. Callback strings remain valid until output copying.

Definition revision, bounds, ordering and callback presence participate in LAN
identity and structure/cache fingerprints. Change revision whenever callback
algorithms or immutable inputs change. Keep callbacks thread-safe and free of
host mutations. Errors, unwritten/invalid blocks, foreign undeclared content and
buffer overflow counts fail generation before a staged request is published.

See [native V2 fixture](../examples/plugins/worldgen_policy_v2/README.md).

## Host-authorized actions and client preview

Actions bind to a held item or a reachable block. Teleport actions use a target callback or
a top-face ray; travel actions use the existing destination dimension entry policy.
Target callbacks receive only a loaded-block reader and current actor pose/item. They run on
the main thread for preview and execution and cannot queue mutations or write plugin state.
Return a valid target or `valid=0`; clients never submit authoritative coordinates.
Teleport requires finite bounded coordinates, the current dimension, range, resident clear
head/body space and safe solid support under every foot column. Hazard/fluid/void landings fail.

`request` returns a receipt for the next host mutation flush. Poll `result`; a completed result
is consumed, pending results remain queryable. There are at most 4096 outstanding receipts.
The host rechecks the actor, selected item/slot/dimension, binding, cooldown and safe target at
execution. Departed actors, changed items, blocked targets and failed travel incur no costs.
Success starts cooldown (0–3600 seconds) and applies durability (0–65535); Creative ignores
wear. Survival wear requires a durable held item. Cooldowns are per actor/action, transient
for the session, continue across travel and clear when leaving/reopening a world.
There are at most 256 action definitions. The explicit `teleport` API queues a host-authorized
safe same-dimension move. Dimension switching remains available through the dimensions API.
LAN v6 synchronizes host cooldown/time state; right-use already routes through host actor input.
Client preview and its target marker use the local replica and never consume items or cooldown.

### Action V2

Query version 2 as `MC_PlayerActionsV2` (`playerActionsV2()`). It retains the V1
table and adds `MC_PlayerActionV2` with a read-only `MC_ActionViewV2` callback.
The view contains effective sneak intent (including toggle sneak), item/block
use source, preview/execution phase, and the host-computed hit position, block and face.
The generation context uses the generator's already-derived seed and actual parameters.
Layer sampling is exact and does not load chunks; native Heaven returns five stable
slots including absent layers, other generators return contiguous terrain spans.
Block flags expose full/empty collision without allowing world mutation. All readers
are callback-lifetime only, bounded, and reject unavailable loaded blocks.

V2 targets report `handled` independently from `valid`, so a recognized Skyway
endpoint consumes use without teleporting or charging costs. Execution recomputes
the current target, including LAN actor sneak intent and hit; preview coordinates
are never authoritative. Item durability also participates in queued-action validation.
V2 optionally measures range from the eye, applies a general use cooldown, selects
the standard tool hold pose and requests a valid/invalid target marker.
These registration values participate in LAN identity.

`official_heaven` registers its Skyway and Starstep callbacks through this interface.
Skyway geometry and actions share a pure anchor policy. Starstep retains the original
96-block eye ray and ordered 3×3 candidate search, 3-second cooldown and one Survival
durability cost, with the host's shared safe-landing checks applied before execution.
Its original five-box mesh and face palette are exported to embedded-image glTF by
`tools/heaven_model.py`, registered through the existing held-model capability.

## Environment and presentation

Dimension time includes phase `[0,1)`, duration 0–86400000 seconds (0 inherits world duration),
and advancing policy 0 inherit world rule / 1 pause / 2 advance. `set_time` is a queued host
mutation. Overrides persist per dimension and synchronize to clients.
Environment profiles may change sky style (0 Overworld / 1 Heaven), cloud permission,
sun/moon size (0 hides, up to 8), colors, lighting, celestial directions, daylight/stars and
weather presentation. Override bits are colors=1, celestial=2, weather=4; unset groups retain
the evaluated DayNightCycle/weather values. Native callbacks receive all evaluated parameters.
Clouds require both the user's cloud setting and the profile permission. Finite bounded output
and nonzero directions are validated; invalid callback output sets a runtime fault.
Presentation changes do not alter authoritative weather or gameplay daylight rules.

Particles use a logical material, RGBA, lifetime .01–60 seconds, scale .001–4, gravity and
velocity components -100–100. At most 256 types; an emission contains 1–128 particles and
finite bounded coordinates. Emission has a per-frame budget and a 2048 live-particle cap.
Local emissions are allowed during HUD/environment/update callbacks; host world emissions are
queued and sent only to guests in the matching dimension and stream epoch. Clients cannot
emit world particles. Missing visual particle types on a guest are ignored.

Held models use package-contained `.gltf`/`.glb`, static bind pose, and column-major first/third
person 4×4 grip transforms (identity by default in JSON). They render in held and dropped
item paths, with textures/material colors and skin bind transforms; GPU resources are created
and destroyed on the render thread. Animated custom grips are outside this interface.
Playlists bind either the menu or one dimension; conflicting bindings are rejected.
They contain 1–32 package-contained WAV files, at most 64 MiB each, random/sequential ordering
and a 0–600 second gap. Decoded plugin audio is bounded to 64 MiB per track and 128 MiB total. Decode happens before audio streaming; existing music/master volume
and pause controls apply. Unavailable audio or invalid WAV decoding keeps the builtin fallback.
Visual-only packages may register profiles, particles and playlists; held models attach to
owned gameplay items. No resource reload after registry freeze is supported.
