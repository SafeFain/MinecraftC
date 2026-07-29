# glTF Entity Model Engine Design

## Goal

Replace the hard-coded cuboid animal renderer with a reusable glTF 2.0 model,
animation, and GPU-skinning subsystem. Migrate all eight current mobs while
preserving Minecraft-style low-poly visuals, and establish interfaces reusable
by future boat, held-item, and player models.

The first release covers cow, pig, sheep, chicken, zombie, skeleton, spider,
and blastling models with `idle`, `walk`, `hurt`, and `death` animation clips.
It reserves an `attack` animation state without requiring that clip in the
initial assets. Dropped items, arrows, and primed TNT remain on the existing
simple renderer through a compatibility path.

## Chosen Approach

Vendor a pinned version of `cgltf` under `external/cgltf/`, including its
upstream source and license record. Use it only to parse glTF 2.0 and GLB data.
MinecraftC owns validation, conversion to runtime data, image decoding, OpenGL
resources, animation sampling, GPU skinning, materials, and rendering.

This is preferred to TinyGLTF because MinecraftC already has image and OpenGL
infrastructure and does not need another broad C++ asset stack. Assimp is not
used because support for formats other than glTF would add build and packaging
cost without serving the requested workflow.

The initial design selected Blockbench as the authoring tool and GLB as the
checked-in runtime format. Blockbench expressions would need to be baked to
keyframes during export because glTF clips carry keyframes rather than a MoLang
runtime.

Implementation outcome: the checked-in runtime format remains GLB, but the
eight shipped original assets are generated deterministically by
`tools/generate_entity_models.py` rather than exported from Blockbench. This
improves byte-for-byte reproducibility while preserving the approved coordinate,
skin, animation, texture, provenance, and licensing contracts.

## Architecture

Add a focused `src/model/` subsystem:

- `GltfLoader` parses `.gltf` and `.glb`, resolves accessors, validates the
  supported subset, and produces CPU-only project data. It performs no OpenGL
  operations.
- `ModelAsset` is immutable shared CPU data: primitives, materials, node tree,
  skins, inverse bind matrices, animation clips, and local bounds.
- `ModelInstance` is lightweight per-entity state: current and target clips,
  playback times, transition progress, evaluated node transforms, and joint
  matrices. Instances share `ModelAsset` geometry and textures.
- `ModelRenderer` owns VAOs, VBOs, EBOs, textures, and model shaders on the main
  render thread. It uploads a model once and draws many instances.
- `EntityModelRegistry` maps each `EntityType` to an asset path, render scale,
  animation fallbacks, and model-specific render settings.
- `Renderer` owns and coordinates `ModelRenderer`, supplying camera and current
  environment data. The hard-coded `renderEntityPart()` animal path is removed
  after migration; a simple-model compatibility entry remains for items,
  arrows, and primed TNT.

The data flow is:

`GLB -> GltfLoader -> shared ModelAsset -> EntityModelRegistry -> ModelInstance evaluation -> ModelRenderer`

Animal simulation, collision, spawning, combat, and loot remain in the entity
system. Model code must not become a dependency of gameplay decisions.

## Supported glTF Subset

The loader supports:

- glTF 2.0 `.gltf` and binary `.glb` files;
- node hierarchies and node translation, rotation, scale, or matrix transforms;
- indexed and non-indexed triangle primitives;
- 16-bit and 32-bit indices;
- `POSITION`, `NORMAL`, `TEXCOORD_0`, `JOINTS_0`, and `WEIGHTS_0` attributes;
- skins with inverse bind matrices and at most four joint influences per vertex;
- animation channels targeting node translation, rotation, and scale;
- `STEP`, `LINEAR`, and `CUBICSPLINE` interpolation;
- base-color textures and factors, double-sided materials, and `OPAQUE`,
  `MASK`, and `BLEND` alpha modes.

The initial implementation deliberately excludes morph targets, Draco,
meshopt, cameras, lights, additional UV sets, and advanced PBR extensions.
Unsupported required extensions fail that model with a precise diagnostic.
Optional unsupported data is ignored only when the glTF specification permits
it, with a one-time warning when useful.

Each skin is limited to 64 joints. Assets above the limit fail explicitly with
their path and joint count; joint arrays are never truncated. Accessors are
checked for component type, shape, range, stride, buffer bounds, and element
count before conversion. Joint indices are range-checked and weights are
normalized, with a deterministic fallback to the first joint for zero-weight
vertices.

## Asset Contract

Runtime animal assets live below `assets/models/entities/`, one GLB per entity.
The GLB embeds its mesh, skeleton, animations, and textures. CMake install and
package rules copy this directory through `RuntimePaths`; no consumer uses a
working-directory-relative path.

All assets use Y-up coordinates and meters, with the entity foot position at
the local origin and its visual forward axis documented in the asset README.
The registry applies any final orientation correction and render scale so that
art dimensions remain independent from collision dimensions.

Required clip names are `idle`, `walk`, `hurt`, and `death`. `attack` is a
reserved optional name. Missing clips use these fallbacks:

- `walk` falls back to `idle`;
- `hurt` and `death` fall back to `idle`;
- absent `attack` returns to the current locomotion clip.

Missing clips produce one warning per loaded asset, not one message per frame.
Textures use nearest-neighbor sampling and pixel-art-safe wrapping. Assets,
textures, `cgltf`, and any adapted reference work must have provenance and
license information recorded in the repository.

## Animation and Entity Integration

The base state is `walk` when horizontal speed exceeds a small configured
threshold and `idle` otherwise. Walk playback speed scales within a bounded
range based on horizontal movement speed. Base-state changes cross-fade over a
short fixed duration.

`hurt` is a higher-priority one-shot overlay triggered by the existing hurt
timer. It returns smoothly to locomotion after completion. `death` is the
highest-priority one-shot. An entity that reaches zero health enters a
1.0-second render-only death presentation before its visual instance
disappears. The death clip plays once; if it finishes before 1.0 seconds, its
last pose is held for the remainder. Loot and gameplay death handling keep
their existing timing. The presentation duration is a named model-system
constant and is tested.

An `attack` state and trigger API are defined now but do not change current
combat behavior until gameplay explicitly calls them.

Per-entity animation instances are keyed by stable entity ID and are not
serialized. They are created lazily, reconstructed after loading, and removed
when the entity unloads, its death presentation ends, or the world closes.
This avoids changing save format version 8.

For each frame, animation evaluation:

1. selects the base clip and any priority one-shot;
2. samples translation, rotation, and scale channels according to glTF rules;
3. uses spherical interpolation for rotations and the specified interpolation
   for other values, including proper cubic-spline tangents;
4. composes the node hierarchy from roots to children;
5. calculates joint global transforms multiplied by inverse bind matrices;
6. submits primitives with their material and skin to the renderer.

## Rendering

The model vertex layout contains position, normal, primary UV, four joint
indices, and four joint weights. The vertex shader performs linear blend
skinning with a uniform array of up to 64 joint matrices. Rigid primitives use
an identity skin path and share the rest of the shader pipeline.

The fragment path supports base-color texture/factor, alpha cutoff, current
environment lighting, distance fog, framebuffer-sRGB/manual-gamma distinction,
and entity tint. Existing hurt, burning, and TNT flash effects remain render
tints rather than animation clips.

Opaque and masked primitives render before blended primitives. Blended entity
primitives are sorted by entity distance; triangle-level sorting is outside
scope. Back-face culling follows the material's double-sided flag.

Model bounds are calculated during loading and transformed per instance for
distance and coarse frustum culling. Rendering remains relative to the player
render origin to preserve distant-world precision. Assets and GPU buffers are
shared. The first implementation draws per primitive and leaves batching or
instancing for measured performance work rather than adding it speculatively.

## Failure Handling

A missing or invalid model logs one structured error containing its resolved
path and reason, then uses a built-in magenta placeholder cube. Failure of one
asset cannot prevent the client from starting or other assets from loading.

Invalid required animation data fails the affected model instead of producing
partially undefined poses. A valid model with a missing named gameplay clip
uses the documented clip fallback. GPU upload errors follow existing renderer
error reporting and remain on the main thread.

## Migration

Migrate all eight mobs to registry entries and GLB assets. Remove their
hard-coded cuboid layouts and animal use of atlas slots from
`EntityManager::render()`. Keep entity colors only where they are meaningful as
effect tints or placeholder fallback data.

Dropped items, arrows, and primed TNT retain the existing cube renderer and
legacy entity atlas in this phase. Their calls pass through an explicitly named
compatibility API so future boat and item model work can migrate them without
changing the model engine. Do not delete the legacy atlas until its final
consumer is migrated.

## Validation

CPU regression tests cover accessor validation and conversion, node hierarchy
composition, joint matrix calculation, clip time boundaries, looping,
`STEP`/`LINEAR`/`CUBICSPLINE` sampling, quaternion interpolation, cross-fades,
state priority, and missing-clip/model fallbacks.

Small project-owned GLB fixtures exercise rigid and skinned paths. During
development, Khronos `Simple Skin` and `Rigged Simple` samples provide
independent compatibility checks. All checked-in GLBs must pass the Khronos
glTF Validator and a project asset test that checks the supported subset,
coordinate contract, bone limit, and required animation names.

Normal project validation includes configuring when CMake discovery changes,
building affected targets, the complete CTest suite, `git diff --check`, and a
real OpenGL startup smoke test. The smoke test must exercise all eight mobs and
visually check orientation, nearest-filtered textures, locomotion transitions,
hurt, death hold, fog/lighting, and absence of OpenGL errors. Missing, corrupt,
and over-64-joint assets are separately tested for graceful fallback.

## Completion Criteria

- All eight existing mobs render from shared GLB assets with no hard-coded
  body-part geometry in `EntityManager`.
- `idle`, `walk`, `hurt`, and `death` states work with documented fallbacks and
  smooth locomotion transitions.
- Both rigid node animation and GPU linear-blend skinning work through the same
  reusable model subsystem.
- Gameplay physics, AI, loot, persistence, and world generation behavior are
  unchanged except for the intentional one-second visual death presentation.
- Invalid assets fall back without preventing startup.
- Install/package output contains every required model and license file.
- Automated tests and the required OpenGL smoke test pass, with any warnings
  reported separately.
