# Entity, generation and rendering extensions

The loader keeps the original ABI 1 tables and discovers these optional version 1
interfaces through `MC_Host::query_extension`. Include
[`extensions.h`](../sdk/minecraftc/extensions.h), or use the size-checked C++
`Host::entities()`, `Host::worldgen()` and `Host::render()` helpers.
Unsupported names/versions return null. Set every descriptor's `size` and check
return codes. Registrations are copied, transactional and frozen before play.
Native desktop packages can use all interfaces; compiled-in packages use the same
contracts on mobile. Portable data packages support entity definitions, flat
terrain generators and declarative render resources/passes.

## Entities

`minecraftc.entities` exposes type registration, lookup, spawn, snapshot, removal,
movement/state intents and spawn egg registration. New runtime types start at
4096; save and network code resolve namespaced keys. Builtin serialized types stay
0–13: item, cow, pig, sheep, chicken, zombie, skeleton, spider, blastling, arrow,
primed TNT, villager, zombie villager and iron golem. An explicit `target` such as
`minecraftc:entity_2` overrides a builtin mob; two overrides of the same type are
rejected. Items, arrows and TNT cannot be mob templates/override targets.

A definition supplies name, template type, health, collision size, speed, attack
damage, hostile/natural-spawn flags, drop item/count, optional package-relative
GLB/glTF and a state revision. Missing model paths borrow the template's builtin
model and animation graph. Custom models use the existing model loader and sibling
`.anim.json` contract. External glTF buffers must stay below the model directory;
package models load through explicit paths even while Title Storage is active. Host collision, gravity, terrain movement, light/spawn gates,
combat reach/sight/shields and rendering remain in effect. Natural types join the
existing passive/hostile pool and its population caps; a plugin can implement
additional spawn rules through queued `spawn` calls in gameplay callbacks.

A native callback observes spawn, decision, damage and death. Decision output
replaces template AI with horizontal velocity, a physical jump request, an optional
player attack target and optional JSON state. Decisions follow existing near/far
10/2 Hz scheduling. Player 0 is the host; `UINT64_MAX` means no attack. Host combat
rechecks visibility and reach. Damage/death callbacks are observations; spawn and
decision callbacks may replace instance state. Movement components must be finite
and within ±32. JSON state is at most 4096 UTF-8 bytes. State strings supplied in
output must be plugin-owned and remain valid until the next callback; do not return
a pointer into a local string. Snapshot buffers need space for the terminating NUL.

Runtime mutations queue at the existing actor-aware safe boundary. Clients cannot
mutate entities. LAN sends at most 512 nearest visible entities, further bounded by
its one MiB snapshot frame budget when instance JSON is large. Callback entity IDs are session-local and never serialized. Each
instance stores its JSON and `state_version` alongside its stable type key; a changed
state revision refuses load rather than interpreting old state silently. Set
`natural_spawn` false to use only explicit spawns. Custom spawn eggs are separate
`MC_SpawnEggDefinition` registrations, keeping the original ABI 1 item layout intact.

Data content can include:

```json
{
  "entities": [{
    "key": "my_plugin:grazer", "name": "Grazer", "template_type": 1,
    "health": 30, "size": [0.8, 1.2, 0.8], "speed": 1.5,
    "attack_damage": 2, "hostile": false, "natural_spawn": true,
    "state_version": 0, "drop_item": "minecraftc:wheat", "drop_count": 2
  }]
}
```

[`entity_demo`](../examples/plugins/entity_demo) registers a circling cow-like mob,
a real creative-catalog spawn egg and an explicit pig override.

## World generation

`minecraftc.worldgen` registers named generators and ordered modifiers for the
existing Overworld (0) or Heaven (1). The create-world page selects each dimension's
generator, JSON parameters and modifiers. An empty generator key keeps the builtin
generator selected by the world type. These choices are saved with the world and
sent to joining clients; opening a world reuses its choices. Parameters must be a
JSON object of at most 4096 bytes. At most 64 distinct modifiers may be selected.
Changing selection/parameters requires creating a new world.

A native generator supplies pure column and voxel callbacks. Column output is the
surface authority for spawn, height and biome queries; voxel output supplies every
block at Y=-64..319. Height/water may be -65 to represent absence. Biome and block
IDs must be registered/valid. The callback receives seed, dimension and borrowed
immutable JSON parameters. Resolve block IDs during registration and retain them;
generation callbacks have no `MC_PluginContext` and may not call main-thread APIs.
They can execute concurrently, out of order or multiple times. Userdata must remain
immutable while workers run. Use coordinate hashes, never mutable random state.
Keep column output consistent with voxel output, including surface collision.

Modifiers emit at most 4096 absolute block positions for a host-owned 16×16 anchor
grid. Radius is 0–64 horizontally; output Y must be valid. The host calls all nearby
anchors, clips to the requested area and applies selected modifier order followed
by Z/X anchor order and callback output order. Later writes win, including AIR.
This ordering is identical for singleton, region, negative-coordinate and LOD
queries. Modifiers overlay builtin/custom terrain; delayed builtin structure writes
cannot replace decorated voxels. LOD applies exact modifier samples over its normal
approximate builtin terrain. Column height describes the generator before modifiers.

Generator/modifier keys, author revisions, parameter bytes and order contribute to
full cache identities; old base/LOD caches are rebuilt on a mismatch. Bump the
registration's revision whenever its deterministic output changes. Plugin output
has its own saved identity: unchanged builtin generation remains v20/Heaven v9.
Additional dimensions use the [dimension registration extension](plugins.md#dimension-registration-abi-1-extension-version-1). No automatic generation migration is supplied.

A portable flat generator is configured as:

```json
{
  "generators": [{
    "key": "my_plugin:flat", "name": "Flat Plateau", "revision": "1",
    "dimension": 0, "height": 48, "water_level": 47,
    "surface": "minecraftc:grass", "fill": "minecraftc:stone"
  }]
}
```

[`terrain_demo`](../examples/plugins/terrain_demo) implements seeded voxel/column
callbacks, `{ "base_height": 48 }` parameters and markers spanning chunk boundaries.
Data flat definitions use their registered heights/materials; parameters are passed
to native callbacks, and do not alter the data flat algorithm.

## Rendering

`minecraftc.render` registers owned shaders, RGBA textures, indexed triangle meshes,
uniform buffers, offscreen color targets, pipelines and ordered stage callbacks.
Handles are opaque and belong to the registering package. No Vulkan/SDL handles or
command buffers are exposed. The Vulkan adapter creates/releases resources on the
render thread and rebuilds pipeline/descriptor state after presentation changes.
A frame supplies stage, dimensions, time, floating world origin, view-projection,
resource revision and the number of host scene submissions available for redraw.

Stages are sky (0), shadows (1), terrain (2), entities (3), translucent (4) and
post (5). Passes run in descending priority and stable registration order. Each
stage permits one explicit `replace: true` pass; competing replacements reject
registration. Replacement suppresses that stage's builtin submissions while
callbacks can use `scene_draw(index, pipeline)` to redraw borrowed scene geometry.
Terrain retains near/far projection phases; entity geometry preserves current node
poses and skeletal deformation. Cloud, particle and UI systems remain separately
owned; the post stage runs before UI. Shadow draws require shadows to be enabled.

`draw` records a registered mesh/pipeline with column-major transform and RGBA tint.
World transforms use coordinates relative to `frame.origin`. With `clip_space=1`,
the transform acts directly in clip space. The host supplies Vulkan clip correction.
Target 0 means the active scene/presentation target; a registered target is offscreen.
Targets are cleared and rendered in dependency order before the main scene. Cycles
and foreign resource references are rejected. `MC_RENDER_SCENE_COLOR` samples the
resolved linear scene only in a post draw to target 0. The host still presents into
an sRGB swapchain and draws UI afterwards.

Shader SPIR-V uses one `main` vertex/fragment entry point. The interface is:

- Vertex input location 0: `vec3` position; location 1: `vec2` UV.
- Set 0 binding 0: `sampler2D`; binding 1: optional uniform block.
- Push constants: `mat4 mvp` then `vec4 tint` (80 bytes).
- Fragment output location 0: linear RGBA. Additional scene attachments are retained.

Uniform buffers are 1–16384 bytes and double-buffered; `update_buffer` must supply
exactly the registered byte count within a render callback. Textures are RGBA8 sRGB,
maximum 16 MiB payload; targets are RGBA8 UNORM with one sample and no depth attachment.
Disable depth testing for offscreen pipelines. Main scene pipelines use its supported
MSAA count; transparent pipelines blend and do not write depth. Depth-only shadow
variants use the vertex shader. There is no raw API or arbitrary shader interface.

Global registration limits are 256 resources, 128 MiB copied payload plus 64 MiB
color targets; shader binaries are at most 1 MiB and meshes at most 16 MiB vertex
payload/one million indices. Targets are at most 8192×8192 subject to the budget.
Data textures decode at most 2048×2048 and input files are at most 16 MiB. Each stage
accepts at most 512 draws per frame. Invalid callbacks/resource creation fail the
session through the existing restart-required fault path; native process crashes
remain outside that recovery mechanism.

Data arrays `render_resources` and `render_passes` use the same constraints. Resource
kinds are `shader` (`path`), `texture` (PNG `path`), `mesh` (`vertices` as XYZ/UV and
`indices`), `buffer` (float `values`), `target` (`width`, `height`) and `pipeline`
(`vertex_shader`, `fragment_shader`, optional `texture`, `depth_test`, `blend`).
References are already-declared namespaced keys; `minecraftc:scene_color` is the
scene alias. Passes specify `key`, numeric `stage`, `priority`, `replace`, optional
`scene_pipeline` and `draws`. Draws use mesh/pipeline/target/uniform keys, optional
16-float `transform`, four-float `tint` and `clip_space`.

[`render_demo`](../examples/plugins/render_demo) includes original GLSL and SPIR-V,
a color-grade post replacement, procedural texture, uniform updates and an offscreen
sampled overlay. Compile shader edits with `glslc effect.vert -o effect.vert.spv` and
`glslc effect.frag -o effect.frag.spv`. Each example builds independently with CMake
and links only the SDK headers.

## Compatibility and regression checks

Save v20 stores stable entity keys/state and keyed dimension generation/policy state.
Builtin v2–v19 metadata is inspectable, but worlds require v20 to open; refusals never
rewrite files. LAN protocol 5 adds dimension keys to entity/state/terrain replication
and the complete world generation table; older peers are incompatible. Registrations and author behavior identifiers
participate in the existing exact gameplay handshake. Visual-only render packages
can differ across peers, while entity/generator registrations require gameplay scope.
The original ABI 1 table offsets and legacy native-entry support remain unchanged.

CPU tests include `plugin_loader_content_events_and_persistence` and
`plugin_entity_worldgen_extensions`. Configure `MINECRAFTC_VULKAN_TESTS=ON` to include
`plugin_vulkan_extensions_readback`, GI and model/fog readback tests. GPU checks use a
real Vulkan device (software Vulkan is supported) and a display/Xvfb, skip with code
77 only when a display cannot be supplied, and reject installed validation-layer
errors. The render check compares pixels and exercises resize, suspend/resume and
resource destruction; CTest also checks API boundaries and mesh index-layer handoff.
