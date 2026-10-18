# MinecraftC plugin loader (ABI 1)

MinecraftC loads official compiled-in plugins and user packages before creating its
renderer or opening a world. It follows the registry, dependency and event-bus
approach of mod loaders such as NeoForge, with a MinecraftC-specific C ABI. Java
mods, JAR files, Mixins and NeoForge binaries are not compatible.

## Install and enable

Put each unpacked package in `<user-data>/mods/<plugin-id>/`, with `mod.json` at
its root. The user-data directory is the parent of the platform's normal `saves`
directory listed in the README. Package directory and manifest ID must match;
paths are relative to that package and symlinks are rejected. ZIP files are not
loaded directly. Package files are read-only to the host.

The main menu's **Plugins** page lists builtin/user plugins, versions, dependencies,
load errors and enabled state. Changes take effect after restarting the game.
`<user-data>/plugins.json` stores enable switches. `--safe-mode` disables all
optional plugins for diagnosis; it cannot open worlds that require those plugins.

The builtin examples are disabled by default:

- `official_content`: crystal block, crystal items, crafting and a use callback.
- `official_atmosphere`: environment colors and a HUD label, marked visual-only.

`official_heaven` is enabled by default. It owns the optional Heaven dimension,
reserved Heaven block/item slots, recipes, v9 generation and LOD, gameplay semantics,
Skyway and Starstep actions, translations, sky/ambient policies, music, material
sequences, item icons and the Starstep model under `assets/plugins/official_heaven/`.
Disabling it (or using safe mode) removes Heaven travel, content, crafting and smelting
from gameplay after restart. Serialized numbers and keys remain reserved; external
packages cannot claim the official identity or dimension handle 1.

On platforms with a virtual Title Storage asset root (Android/iOS), builtin
resource inventories are read through the same packaged asset API and materialized
under `<user-data>/.cache/builtin_plugins/<id>/<inventory-key>/`. The complete
inventory is read before cache publication; intact files retain their timestamps
and damaged files are replaced atomically. This preserves package-relative model,
image and audio paths and the same content fingerprint as desktop installations.
The provider remains statically linked on mobile; this cache contains resources.

Worlds require the exact gameplay plugin set and package fingerprint. The addition
of this provider makes worlds saved with the previous plugin set incompatible.
They remain listed, refuse opening without writes, and are not migrated. Worlds
created with Heaven disabled contain only the dimensions currently registered.
Enabling Heaven later also changes that world's required set. Terrain generation
remains global v20 / Heaven v9; save v21 and numeric content IDs are unchanged.

## Registered localization, travel and presentation

Query extension versions independently; ABI 1 and the existing versioned descriptor
layouts remain unchanged. All registrations below participate in the owning package's
transaction and disappear when it fails, is disabled or is excluded by safe mode.

- `minecraftc.localization`, version 1 (`MC_LocalizationV1`), registers a
  package-relative directory of `xx_xx.json` dictionaries. Keys must use the
  registering package's namespace. `en_us.json` is required; lookup uses the current
  language, then English. Content names bind a content key to a translation key.
  The official package also supplies aliases for historical host translation keys.
- `minecraftc.dimensions`, version 3 (`MC_DimensionsV3`), embeds the V2 definition
  and adds a localized name and seed domain. The host derives the generation seed
  once before invoking callbacks; zero leaves the world seed unchanged. Bed travel
  entries name explicit source and target handles and a localized label. Menus use
  the registered dimensions; LAN travel is revalidated by the server.
- `minecraftc.worldgen`, version 3, extends V2 with pure logical-layer sampling.
  A callback returns at most `MC_MAX_GENERATION_LAYERS` inclusive vertical spans,
  preserving absent slots and biome identifiers independently of decoration LOD.
  A surface height matching a present layer also uses that layer's bottom as its
  density lower bound; generators without this callback retain the world-bottom default.
  `minecraftc.player_actions`, version 3, adds dimension and selected generator
  keys to the read-only V2 action view. Actions can select provider behavior
  without a host builtin-generation flag.
- `minecraftc.material_sequences`, version 1, registers package-contained 16x16
  color/normal/property frames with variant count, frame count, animation rate and
  selection seed. The shared host atlas still owns layout, tile-safe mipmaps and
  GPU lifetime. Provider item icons use `MC_PresentationV2::register_item_icon`;
  existing logical item slots retain their positions and held-item silhouettes.
- `minecraftc.structures`, version 3, reuses V2 placement/build and loot metadata.
  Its coarse LOD policy optionally omits structures above cell size 1 when the
  generator supplies its own coarse silhouettes. The V2 default remains exact
  center sampling; no second batch metadata channel is introduced.
- `minecraftc.presentation`, version 2, adds explicit initial particle phase,
  rotation, angular velocity, color and per-axis sine/cosine acceleration, generic
  shapes and collision policy. Local ambient callbacks receive a bounded emission
  budget and borrowed read services. State is host-owned, bounded and cleared on
  dimension changes; callback failures use the normal plugin fault path.
- `minecraftc.environment`, version 2, retains the V1 environment callback and
  adds generic sky-layer parameters. `MC_SkyParameters` documents the twelve vec4
  slots for cloud-sea and aurora motion, masks, colors, noise and viewing behavior.
  Celestial size, direction, weather and cloud suppression retain V1 parameters.
  V2 also registers independent atmosphere/low-cloud/voxel-cloud/light-shaft flags,
  color grade and particle intensity. Selecting a sky drawing style does not
  implicitly select these policies. The Vulkan post-process retains its 128-byte
  push-constant contract; quality counts and shaft flags share an exact integer
  bitfield carried in a float, while grading uses explicit RGB/strength values.

Authoring dictionaries import package-owned source definitions through asset-relative
`imports`. The texture workbench preserves the original owner on writeback; applying
an export or building the repository atlas synchronizes package material frames and
item icons. Shared generated atlases remain derived caches. Runtime content policy
comes from the enabled package, and reserved numbers retain their historical implicit
save/wire identity rather than entering dynamic content palettes.

Desktop Linux, Windows and macOS can load native shared libraries. Android/iOS
accept data packages and compiled-in code plugins; external native libraries are
unsupported. Native plugins execute trusted code in the game's process. This ABI
is an interoperability contract, not an isolation or security sandbox.

## Manifest

```json
{
  "id": "my_plugin",
  "name": "My Plugin",
  "version": "1.0.0",
  "api": 1,
  "enabled_by_default": true,
  "visual_only": false,
  "native": {
    "linux": "my_plugin.so",
    "windows": "my_plugin.dll",
    "macos": "my_plugin.dylib"
  },
  "data": "content.json",
  "dependencies": [{"id": "other_plugin", "min": "1.0.0", "max": "2.0.0", "optional": false}],
  "after": [],
  "before": [],
  "conflicts": []
}
```

Omit `native` for a data-only package. Omit `data` for native-only packages.
`game_min` and `game_max` optionally restrict the canonical version from VERSION.
Versions use SemVer, with inclusive minimum/exclusive maximum bounds. A present
optional dependency must satisfy its bounds. Duplicate IDs, dependency failures,
conflicts and order cycles prevent affected plugins from loading. Dependencies
load first; otherwise order is stable by ID. Registration failure rolls back that
plugin's content, commands and subscriptions and blocks required dependents.

## Data content

[`examples/plugins/example_content`](../examples/plugins/example_content) is a
complete portable package. Its JSON has `materials`, `blocks`, `items` and
`recipes` arrays. Optional block-family/geometry and entity/generator/render arrays are described in the
[extension reference](plugin-extensions.md); behavior/structures/actions/environment/presentation arrays
are described in the [capability guide](plugin-capabilities.md). New keys must be `plugin_id:local_name`, using lowercase letters,
numbers, underscores, slashes, dots or hyphens. Registries freeze before play.
Runtime IDs start at 4096; never persist them in plugin configuration.

Materials take `key`, optional relative `image`, `normal` and `properties` PNG
paths, or a `color` RGBA array. Each image must be 16×16 and at most 1 MiB.
Normal/property data is linear; color textures are sRGB. The host expands the
shared atlas, generates tile-safe mipmaps, and feeds material colors/emission to
GI. Explicit `replace: true` can override an existing material key (builtin keys
are the logical names from the texture definitions, without a namespace).

Blocks take `key`, `name`, `materials` (one key or six keys ordered top, bottom,
front, back, right, left), `solid`, `cross`, `layer` (0 opaque, 1 cutout, 2
translucent), `alpha`, `hardness`, `preferred_tool`, `minimum_tier`, `unbreakable`,
`emission` (0–15), `color` (RGB), and optional `drop` item key. Items take `key`,
`name`, `material`, optional `placed_block`, `kind`, `category`, `max_stack`,
`durability`, `tool`, `tier`, `food`, `saturation`, `attack_damage`, `attack_speed`.
See the SDK enums for numeric values. ABI 1 supports material/block/food items and
existing melee tool types; armor is rejected. Spawn eggs use the entity extension.

Recipes take `key`, `ingredients`, `output`, `count`, `width`, `height`, `mirror`,
`smelting` and `cook_ticks`. `operation` is 0 add, 1 replace, 2 remove; patching
requires an explicit `target`. Builtin recipe targets are indexed keys
`minecraftc:crafting_N` / `minecraftc:smelting_N` in SurvivalRules.cpp; treat those
as version-specific. Builtin item aliases such as `minecraftc:stone` can be used
in recipes. Serialized builtin keys remain the stable numeric
`minecraftc:block_N` / `minecraftc:item_N` forms.

## Native SDK and events

Include [`sdk/minecraftc/plugin.h`](../sdk/minecraftc/plugin.h) (C) or `plugin.hpp`
(C++). Export `const MC_Plugin* MCPlugin_Query(uint32_t abi)` and return an ABI 1
structure with matching manifest identity. Initialize every descriptor's `size`.
All strings are UTF-8, borrowed during a call; the host copies registration data.
No STL objects, exceptions or allocations cross ownership boundaries. Host calls
and gameplay/render callbacks run on the main thread; plugins must marshal their own
worker results back before using the host. Pure world-generation extension callbacks
run on workers and have no main-thread host access. Return zero on success; use `last_error` for
failed host calls. Never throw across the ABI.

Build the runnable example independently:

```sh
cmake -S examples/plugins/native_demo -B /tmp/native-demo
cmake --build /tmp/native-demo
```

Copy its `mod.json` and platform library into `mods/native_demo/`. It registers a
crystal, `/native_demo:gift`, a use action, environment colors and HUD text. The
module has no link dependency on game internals.

Lifecycle is load → REGISTER → frozen registries → INITIALIZE → WORLD_READY →
frame/gameplay events → WORLD_CLOSE → SHUTDOWN → reverse-order unload. Register
content, subscriptions and namespaced commands only during load/REGISTER.
Listeners run in descending priority, with registration order as the tie-breaker.

BREAK/PLACE/USE/DAMAGE PRE events can cancel default behavior before its inventory,
tool or health effects. Cancellation is monotonic. DAMAGE PRE may change damage;
ENVIRONMENT may change the supplied colors/intensities. Other event inputs are
read-only. Corresponding POST events observe completed operations. UPDATE PRE/POST
receive frame time. HUD callbacks draw bounded rectangles, UTF-8 text and registered
material icons in bottom-left coordinates using the current UI dimensions.

The host offers player snapshots, loaded-block reads and queued block/item writes.
Writes are applied on the main thread after simulation, with a bounded queue;
acceptance into the queue does not guarantee execution (the destination may have
unloaded or inventory may be full). There is no synchronous recursive event
mutation. HUD/environment/close callbacks cannot enqueue world mutations.
Visual-only plugins cannot register gameplay content/events or persist world data.

Per-plugin JSON config lives in `config/plugins/<id>.json`. World JSON state lives
under the active world's `plugin_data/<id>.json`; world writes are queued. Both are
bounded to 64 KiB. Read buffers must accommodate the terminating NUL. Configuration
changes affecting gameplay require restart; its initial bytes contribute to the
world compatibility fingerprint.

A callback error disables that plugin, discards queued operations and returns to
the menu without saving the current session. Restart is required before opening
another world. Existing autosaves are not rolled back. Process crashes or memory
corruption from native code cannot be recovered by this mechanism.

## Saves and limits

Save v16 records namespaced block/item palettes and the exact gameplay plugin set,
versions and package/configuration fingerprints in each persisted file. Runtime
IDs can move between launches without losing content. Missing, changed or extra
gameplay plugins refuse world loading before writable attachment; the world remains
listed with a compatibility reason. Restore the matching package/configuration to
open it. No automatic migration or missing-block substitution is provided.
Visual-only plugins do not contribute a world requirement.

Worlds now require save format v21. v2–v20 metadata remains inspectable, but opening
those worlds is refused without rewriting. Create new worlds with the desired
plugin set; no migration is supplied. Generated caches are rebuildable when
incompatible. Terrain generation stays v20 and builtin Heaven stays v9.

ABI 1 now exposes optional version 1 entity, world-generation, dimension and render tables.
Plugins can register/override mobs, select per-dimension generators and modifiers,
and create Vulkan-owned shader resources and replaceable render stages.
[Extension contracts and examples](plugin-extensions.md) document these APIs.
Save v21 stores stable entity keys/state, generation choices and namespaced dimensions.
No hot reload, raw Vulkan access, arbitrary plugin network
messages, bytecode patching or custom block-entity schemas are provided.

## LAN multiplayer and ABI 1 extension

Plugin worlds can open and join the existing LAN rooms. Every participant must
install and enable the same gameplay plugins and restart before joining. The
handshake compares plugin versions, gameplay configuration at initialization, registered block/item/entity
properties, generator/modifier revisions, recipes, callback order and command permissions before player admission
or any guest-profile write. A mismatch names the differing requirement. Visual-only
plugins and atlas slots may differ. No plugins or native code are downloaded.

Native packages can declare `"network_compatibility": "my-plugin-behavior-1"` in
`mod.json`. This author-provided identifier promises equivalent gameplay across
platform builds; update it when behavior changes. Without it, native packages must
match their package fingerprint. Data packages compare their registered gameplay
and configuration; builtin plugins carry a fixed behavior identifier. The network
signature is a discovery hint, not an authentication mechanism. Existing world-save
fingerprints remain unchanged, including native files and configuration.

LAN protocol 6 uses canonical IDs sorted by namespaced content key and maps them to
each process's frozen local registry. SDK IDs are never reassigned. Guest profiles
version 4 persist an item palette and keyed per-dimension state; versions 1–3 are
refused without rewriting. Unknown content,
invalid stacks and incompatible protocol versions are rejected rather than replaced.

Only the host runs gameplay callbacks. Clients may run initialization/shutdown,
HUD and environment callbacks; visual plugins also receive world lifecycle events.
Client world-data access and gameplay mutations are unavailable. World plugin data
stays on the host and is not copied to clients. Gameplay callbacks cannot exchange
arbitrary custom network payloads in this version.

The original ABI 1 structures keep their field offsets. `MC_Event` adds optional
`player_id`, `dimension` and `role` tail fields; check `event->size` before using them.
Player 0 is the host, and guest IDs expire when they leave. New join/leave events run
on the host. Inside an actor callback, the original `player`, `give_item`, `get_block`
and `set_block` operations refer to that actor and dimension. Outside callbacks they
refer to the local host player. Queued operations capture the actor/dimension at
submission; a departed actor cannot become the host or a different guest.

Discover optional capabilities by checking the host table size before calling
`query_extension(context, MC_MULTIPLAYER_EXTENSION, 1)`. The `MC_MultiplayerV1` table
provides role queries, player enumeration, player snapshots, targeted item grants,
explicit-dimension block access and command registration. Snapshots on a client
are available only for its own player; enumeration includes the full room roster.
The C++ `minecraftc::Host::multiplayer()` helper performs the table-size checks.

Old `register_command` calls remain host-only. To opt a command into guest use,
register it through the extension with `MC_COMMAND_ALLOW_GUEST`. Requests are
sequenced, rate-limited and executed on the host with the authenticated connection's
actor context. Builtin administrative commands remain host-only. Callback faults
end the session and discard queued operations without saving the faulted world;
peers receive a reason when the transport can deliver its terminal frame.

## Dimension registration (ABI 1, extension version 1)

Query `minecraftc.dimensions` as `MC_DimensionsV1`, or use `minecraftc::Host::dimensions()`.
The host's `minecraftc:overworld` keeps handle 0. `official_heaven` publishes
`minecraftc:heaven` at reserved handle 1 only when successfully enabled.
Additional dimensions receive launch-local uint32 handles. Persist keys, never handles.
Their handles start at 2 even when Heaven is absent.
Builtin definitions cannot be replaced. The registry supports up to 1024 dimensions,
including builtins, and freezes before opening worlds.

Register a dimension before its generator, then register that generator against the
returned handle. The default selection, modifier revisions and return targets are
validated at the end of the owning plugin transaction. Failed registration rolls
back the entire package. Cross-package references require a manifest dependency;
visual-only plugins cannot register dimensions. Unknown generators or targets never
fall back to another dimension. `query` requires the frozen registry and returns
host-owned descriptor strings valid until shutdown.

`MC_DimensionDefinition` selects normal/fixed clear/rain/thunder weather, disables
natural spawning optionally, and selects safe-surface or fixed-coordinate spawn.
A zeroed definition defaults to normal weather, natural spawning, safe-surface
spawn, same-dimension death respawn and no automatic void return. Generator key,
generator revision and dimension revision are mandatory for plugin definitions;
parameters default to `{}` and optional default modifiers use explicit revisions.
Fixed coordinates describe the player's AABB bottom-center. Safe landings require
loaded support, clearance and no fluid/fire; search is bounded and failure retains
the original dimension. Weather and natural spawning still obey relevant world
rules and difficulty; explicit entity spawning is independent of natural spawning.

First entry uses the spawn policy; later entries restore the player's dimension
position. Death targets default to the current dimension and prefer a valid target
bed, then target spawn. Optional void targets prefer the target's last grounded
safe position, then target spawn. No void target preserves builtin behavior and
uses void damage in additional dimensions. Each player has independent dimension
positions, safe positions and beds. All dimensions retain separate clocks/weather.

With cheats enabled, `/dimension list` lists keys and `/dimension <key>` travels the
local host player. Builtin commands retain the existing host-only LAN policy.
SDK `travel(context, player_id, dimension)` queues a host-authoritative operation;
0 identifies the host. Acceptance means queued, not successful arrival. Guests
cannot redirect another player's operation, and replicas cannot mutate dimensions.
Host and guest travel share landing validation, loading gates and stream epochs.

Data packages may declare:

```json
{
  "dimensions": [{
    "key": "my_mod:islands", "name": "Islands", "revision": "1",
    "generator": "my_mod:terrain", "generator_revision": "1",
    "parameters": "{}", "weather": 1, "natural_spawning": false,
    "spawn": 0, "death_target": "minecraftc:overworld",
    "void_target": "minecraftc:overworld"
  }],
  "generators": [{
    "key": "my_mod:terrain", "revision": "1", "dimension": "my_mod:islands",
    "height": 96, "water_level": -65
  }]
}
```

Weather values are 0 normal, 1 clear, 2 rain, 3 thunder; spawn is 0 safe surface or
1 fixed with `spawn_position: [x,y,z]`. Optional `modifiers` entries contain `key`
and `revision`. Numeric generator dimension 0/1 remains supported for older plugins.
See [native dimension example](../examples/plugins/dimension_demo/README.md).

Save v21 stores dimension keys, policy revisions, generation selections and player
states. The main world and Heaven retain existing directories; other dimension keys
use segmented hex-encoded directories. Missing dimensions/plugins/revisions refuse
loading without writes. LAN v6 sends keys rather than local dimension handles and
includes canonical dimension policy descriptions in admission fingerprints. Older
protocols cannot join. Player profile v4 persists dimension-keyed positions/beds;
older profiles (including the local LAN identity profile) are explicitly refused.
Additional dimensions use generic terrain/LOD and Overworld environment presentation.
The optional [worldgen V2 interface](plugin-extensions.md#batch-and-multilayer-lod-worldgen-extension-version-2)
adds batch generation and multilayer/decorative LOD sampling. V1 dimension registration does not add portals, arbitrary spawn callbacks or species-specific natural spawn tables.
Optional [gameplay and presentation capabilities](plugin-capabilities.md) add sky, clouds, particles, held models and playlists.


### Seeded dimension spawn (extension version 2)

Query `minecraftc.dimensions` version 2 as `MC_DimensionsV2`, or call
`minecraftc::Host::dimensionsV2()`. The `v1` member exposes the frozen query,
find and authoritative travel operations. Register `MC_DimensionDefinitionV2`
with a sized V1 base, a required `sample_spawn` and immutable userdata.
The host sets the queried spawn policy to `MC_SPAWN_SAMPLED`; `base.spawn` is
ignored for this registration. V1 registration still accepts only fixed or
safe-surface policies and its layout is unchanged.

The pure callback receives `MC_GenerationContext`: the same already-derived
seed and actual parameters used by the selected generator, with borrowed
read-only strings. Fill three doubles with the player's bottom-center and
return zero. Do not derive the dimension seed again, read loaded chunks, mutate
host state or depend on callback order. Keep userdata alive and thread-safe
until plugin shutdown. Non-finite, unwritten, out-of-world output or callback
failure is a generation error; unsafe physical landings refuse travel.

Local travel, respawn, saved-world reopening, LAN travel and saved LAN profile
admission share the host landing validation. A valid bed (for respawn) or valid
saved position takes precedence. Otherwise the sampled position must have full
footprint support, clear body/head space and no fluid/fire. A sampled landing
never falls back to the highest surface, preserving lower island layers after
terrain edits. V1 fixed/safe-surface behavior remains unchanged.

The dimension revision identifies the spawn algorithm and immutable policy;
change it when behavior changes. Sampled policy/callback presence enters the
LAN description; a mismatched saved dimension revision refuses before writes.
No save-format or builtin generation-version change is introduced. See the
[native fixture](../examples/plugins/worldgen_policy_v2/README.md) and
[structure V2 contract](plugin-capabilities.md#accepted-structures-extension-version-2).
