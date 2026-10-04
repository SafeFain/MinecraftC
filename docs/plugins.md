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
`recipes` arrays. New keys must be `plugin_id:local_name`, using lowercase letters,
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
existing melee tool types; armor and spawn eggs are rejected.

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
and callbacks run on the main thread; plugins must marshal their own worker
results back before using the host. Return zero on success; use `last_error` for
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

Builtin-only v2–v15 saves retain their existing readers, generation compatibility
rules and IDs. Adding a gameplay plugin to an old unmodded world is refused in
this first version; create a new world with the desired set. Generated caches are
rebuildable when incompatible. Unmodded terrain remains generation v17.

ABI 1 has no hot reload, arbitrary shaders/render passes, new entity types,
dimensions, world generation hooks, network protocol, bytecode patching or custom
block-entity schemas. Plugins can compose registered content, recipes, events,
commands, controlled operations, environment materials and HUD drawing.
