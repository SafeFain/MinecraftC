# Plugin development

This guide takes a plugin from an example package to a distributable build.
For installation, manifest fields and world recovery, start with
[the loader reference](plugins.md). Public declarations in
[`plugin.h`](../sdk/minecraftc/plugin.h), [`extensions.h`](../sdk/minecraftc/extensions.h)
and [`capabilities.h`](../sdk/minecraftc/capabilities.h) define the exact C layouts.
Use a source checkout for source, build and test links in this documentation;
release packages also include the SDK and plugin examples.

## Choose a package type

| Type | Suitable work | Platform support |
|---|---|---|
| Data (`mod.json` + JSON/assets) | Materials, blocks/items, recipes, declarative entities, flat generators, shapes, capabilities and render passes | All supported platforms |
| Native (`mod.json` + shared library, optionally data/assets) | Custom callbacks, AI, pure terrain/structure algorithms, gameplay actions and render callbacks | Linux, Windows, macOS |
| Compiled-in provider | Code distributed as part of the application, including official Heaven | All supported platforms; requires an application rebuild |

Mobile does not load external native libraries. Native code runs inside the game
process. The SDK exposes opaque host services; do not link game internals or use
raw SDL/Vulkan resources. There is no hot reload: enable/disable and package
changes require restart. `visual_only` packages cannot register gameplay content
or persist world state.

## First data plugin

Copy [`example_content`](../examples/plugins/example_content) to
`<user-data>/mods/example_content/`. The package root must contain `mod.json`:

```json
{
  "id": "example_content",
  "name": "Data Content Example",
  "version": "1.0.0",
  "api": 1,
  "data": "content.json"
}
```

The supplied `content.json` registers a blue-brick material, block, item and
one-slot cobblestone recipe. Enable it in **Plugins**, restart, and create a test
world with cheats. Give yourself `/give example_content:blue_brick 4`, place and
break it, craft it, then save/reopen. Test in a disposable world: changing the
gameplay package changes existing worlds' compatibility requirements.

To rename the example, update the directory, manifest ID and every owned
`example_content:` key together. Keep references in one namespace; declare
dependencies when referencing other plugins. Do not persist runtime IDs or atlas
slots. Material image paths are package-relative; block material PNGs are 16×16.
See [data fields](plugins.md#data-content) and the [asset pipeline](../ASSET_PIPELINE.md).

## Build a native plugin

The complete [`native_demo`](../examples/plugins/native_demo) links only SDK headers.
From the repository root, use a separate build directory:

```sh
cmake -S examples/plugins/native_demo -B build-local/plugins/native_demo
cmake --build build-local/plugins/native_demo --config Release
```

On Windows use a developer environment with a C++17 compiler and CMake. The
Visual Studio DLL is normally under `build-local/plugins/native_demo/Release/`.
Single-configuration Linux/macOS builds normally place the library directly
under `build-local/plugins/native_demo/`. Package the result as:

```text
mods/native_demo/
  mod.json
  native_demo.dll       Windows build
  native_demo.so        Linux build
  native_demo.dylib     macOS build
```

Ship the libraries for the platforms/architectures you support; the manifest
maps each platform to its filename. Match the game's architecture and ensure
the plugin's runtime dependencies are available. Distribute unpackable packages;
the loader does not load ZIPs directly and rejects symlinked package paths.

`native_demo` registers a crystal and `/native_demo:gift`. With cheats enabled,
run that command in a test world, use the crystal, and verify its HUD/environment
callbacks. Its [source](../examples/plugins/native_demo/plugin.cpp) and
[CMake project](../examples/plugins/native_demo/CMakeLists.txt) are the runnable
entry-point template:

- Export exactly `extern "C" MC_PLUGIN_EXPORT const MC_Plugin* MCPlugin_Query(uint32_t abi)`.
- Return null for unsupported ABI versions. The descriptor's ABI, ID and version
  must match the manifest.
- Set every descriptor's `size`, zero-initialize unused fields, and check return
  values. Host calls return zero on success; inspect `last_error` after failure.
- Keep strings UTF-8 and honor borrowed-data lifetimes. Registration copies
  descriptors; callback userdata must stay alive until shutdown. Do not pass STL
  objects, ownership of allocations or exceptions across the ABI.

## Lifecycle, threads and mutation

The order is load → REGISTER → registry freeze → INITIALIZE → WORLD_READY →
gameplay/frame callbacks → WORLD_CLOSE → SHUTDOWN → reverse unload. Register
content, commands and subscriptions during load/REGISTER. A failed package
transaction rolls back its registrations and blocks required dependents.

Ordinary host calls and gameplay/render callbacks are main-thread operations.
Generation, batch, LOD, structure and sampled-spawn callbacks are pure and may
run concurrently, repeatedly and out of order. Resolve content during registration;
workers use immutable userdata, the supplied seed/parameters and coordinate hashes.
Never call main-thread services or issue GPU work from generation callbacks.

World writes, item grants and action requests queue for a safe host boundary.
Acceptance does not promise execution: the actor may leave, a chunk may unload,
or inventory may fill. HUD/environment/close callbacks cannot enqueue world
mutations. PRE cancellation is monotonic; only documented fields are mutable.
See [events and storage](plugins.md#native-sdk-and-events).

Use `config_read/write` for per-plugin configuration and `world_data_read/write`
for plugin-private world JSON (each at most 64 KiB; read buffers need the NUL).
Initial gameplay configuration participates in compatibility. Callback failure
disables the plugin and ends the session without saving current state; a restart
is required. Prior autosaves remain. Native process crashes cannot be recovered
by the callback error path.

## Negotiate extensions

ABI 1 is independent of extension versions. Check host-table size before reading
`query_extension`; the C++ `minecraftc::Host::extension()` helper does this.
Query the version needed and handle null. Do not cast a V1 result to V2/V3.
Check returned table/descriptor sizes before accessing optional fields.

| Extension | Available versions | Reference |
|---|---|---|
| Entities, block shapes, render | 1 | [Extensions](plugin-extensions.md) |
| World generation | 1, 2, 3 | [Generation](plugin-extensions.md#world-generation), [V3](plugins.md#registered-localization-travel-and-presentation) |
| Dimensions | 1, 2, 3 | [Policies](plugins.md#dimension-registration-abi-1-extension-version-1), [sampled spawn](plugins.md#seeded-dimension-spawn-extension-version-2) |
| Behaviors | 1, 2 | [Behavior semantics](plugin-capabilities.md#block-and-item-behavior) |
| Structures, player actions | 1, 2, 3 | [Capabilities](plugin-capabilities.md), [V3](plugins.md#registered-localization-travel-and-presentation) |
| Environment, presentation | 1, 2 | [Presentation](plugin-capabilities.md#environment-and-presentation), [V2](plugins.md#registered-localization-travel-and-presentation) |
| Localization, material sequences | 1 | [Registered resources](plugins.md#registered-localization-travel-and-presentation) |
| Multiplayer | 1 | [Actor-aware host API](plugins.md#lan-multiplayer-and-abi-1-extension) |

Some newer tables have no dedicated C++ helper. Use `Host::extension(name, version)`
and the exact SDK table type. Dimension handles, entity instance IDs and dynamic
block/item IDs are launch/session-local; persist stable keys and author revisions.

## Examples by feature

| Example | What to learn |
|---|---|
| [example_content](../examples/plugins/example_content) | Data materials, blocks/items, recipe |
| [native_demo](../examples/plugins/native_demo) | C ABI, events, HUD, commands and per-actor cooldown |
| [entity_demo](../examples/plugins/entity_demo/README.md) | Custom mob decisions, model, spawn egg and override |
| [terrain_demo](../examples/plugins/terrain_demo/README.md) | Column/voxel generation and modifiers |
| [dimension_demo](../examples/plugins/dimension_demo/README.md) | Dimension policy, generation and host travel |
| [worldgen_v2_demo](../examples/plugins/worldgen_v2_demo/README.md) | Batch generation and multilayer LOD |
| [worldgen_policy_v2](../examples/plugins/worldgen_policy_v2/README.md) | Seeded spawn and accepted structures |
| [block_shapes_data](../examples/plugins/block_shapes_data/README.md), [block_shapes_demo](../examples/plugins/block_shapes_demo/README.md) | JSON/native shape families and boxes |
| [capability_demo](../examples/plugins/capability_demo/README.md), [capability_native](../examples/plugins/capability_native/README.md) | Behavior, structures, actions, environment and presentation |
| [render_demo](../examples/plugins/render_demo/README.md) | Host-owned render resources, shaders and stages |

## Compatibility, testing and distribution

Bump the package version for published changes. Update generator, modifier,
structure, dimension or entity-state revisions when their saved semantics change;
update LOD revisions for LOD output changes. Preserve point/batch/region equivalence,
negative coordinates and deterministic scheduling. Do not change builtin numeric
IDs or claim official reserved content. See [generation contracts](plugin-extensions.md#world-generation).

Only the LAN host runs gameplay callbacks. Use the callback's actor/dimension;
guest IDs expire on departure. Guest commands require `MC_COMMAND_ALLOW_GUEST`.
For native cross-platform LAN, `network_compatibility` is an author promise of
equivalent behavior, not a bypass of world-save fingerprints. Change it when
behavior changes. Test host/guest actions and mismatches with matching builds.

From a configured application source build:

```sh
ctest --test-dir build-local -C Release -R plugin_ --output-on-failure
ctest --test-dir build-local -C Release --output-on-failure
```

Render plugins additionally need actual Vulkan execution. Configure
`-DMINECRAFTC_VULKAN_TESTS=ON`, build, then run `ctest --test-dir build-local
-C Release -L vulkan --output-on-failure`. Verify the plugin-specific readback
tests in that configuration. A missing-display skip is not GPU validation.

Before distribution, test a clean install, disable/restart, missing dependencies,
save/reopen and version mismatch. Include manifest, libraries/data, all referenced
assets, dependency requirements and license/provenance notices. Do not include
local config, saves, build trees or generated test evidence. Diagnose failed load
via **Plugins** status and `<user-data>/minecraftc.log`; common causes are ID/path
mismatch, missing platform library, unsupported API/version, undeclared foreign
references, invalid resources or an exception escaping native code.
