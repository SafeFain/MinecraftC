# glTF entity model and animation engine

## Runtime contracts

- Validated glTF 2.0/GLB loading through unchanged vendored cgltf 1.15: external,
  embedded and base64 buffers/PNG/JPEG images, sparse accessors, normalized UVs and
  weights, node hierarchy and at most 256 joints per skin (four weights per vertex).
- Images, textures and samplers remain independent. All core wrap/filter modes are
  supported; requested mips use alpha-weighted linear-light filtering. Unspecified
  filters retain nearest sampling for existing pixel models. Only base-color
  TEXCOORD_0 materials are rendered; full PBR, transformed UVs, morph targets and
  compressed meshes are outside this subset. Required extensions are rejected.
- Each selected-scene node/mesh/skin binding draws separately while sharing its
  immutable geometry. Skin matrices are joint-global times inverse-bind; rigid
  geometry uses its node-global transform. Bounds include selected bind-pose geometry.
- Main-thread Vulkan resources use generation-checked handles and renderer-scoped
  sharing. Clearing resources waits for outstanding GPU frames. Queuing snapshots
  pose data before returning; command recording never dereferences the caller's instance.
- Opaque/masked bindings use indexed instancing. Blend retains stable far-to-near
  submission order, merging only consecutive compatible draws. Environment,
  instance and actual-length joint data use separate readonly storage buffers,
  paged within device range limits and rotated with frame fences.
- Shared per-type assets with per-instance layered animation graphs; missing or
  invalid graphs use the builtin fallback. See [animation behavior](entity-animation-engine.md).
- Generator v5 supplies eleven original mob GLBs, including villager, zombie
  villager and iron golem. Player and held-item assets have separate ownership.
  Dropped items use their block/tool/icon rendering paths; arrows and primed TNT
  retain their entity rendering paths.
- Animation mixer/transition state is transient. Current world persistence is v21;
  plugin entity types persist stable keys and revisioned instance state.
- Local pose workspaces are reused, node globals compose in precomputed topology
  order, and revision caching avoids duplicate evaluation. Game events advance in
  simulation independently of visibility. Instances retire with entity lifetime;
  hiding an entity does not reset its animation. Player aim/hand overlays apply
  before global and palette composition. Plugin scene replacement uses the same snapshots.

## Assets and regeneration

See [entity model authoring](../assets/models/entities/README.md) for coordinates,
clips and deterministic generation; [player assets](../assets/models/player/README.md)
and [asset pipeline](../ASSET_PIPELINE.md) cover related resources.

```sh
python3 tools/generate_entity_models.py --output assets/models/entities
python3 tests/test_entity_models.py
```

The application supplies one `ModelAssetCache` to plugin registration and renderer
composition. It shares immutable assets and failed-load diagnostics across consumers
and survives GPU recreation. Plugin entity/held registrations resolve from their
package roots and retain validated assets; held registrations also publish path
aliases into this cache. Plugin held
meshes use selected-scene bindings, composed bind poses, sampler mips and prepared
bounds; procedural tool meshes keep their existing path. An explicit `AssetSource` identifies its namespace/root and optional
virtual reader. Dependencies resolve relative to the model and must stay inside
that root, including decoded URIs and filesystem symlinks. This supports packaged
Title Storage and plugin assets without putting platform APIs in model code.

Plugin entity registration rejects malformed, unsupported or over-budget models
with the resource diagnostic and rolls back that plugin's registration. Successful
`EntityContent` holds the immutable asset directly; registries prefer it even when
using an independent cache. Omitting a model retains the template fallback.

`LoadLimits` bounds each load before allocation. Default limits are:

| Resource | Limit |
|---|---|
| One file/data URI; cumulative encoded input | 32 MiB; 96 MiB |
| Live parser / decoder allocations | 64 MiB / 128 MiB |
| Cumulative decoded RGBA / converted output / texture relations including mips | 128 MiB / 256 MiB / 256 MiB |
| Image dimension / pixels | 4096 per axis / 16,777,216 |
| Primitives / nodes / skins / animations | 4096 / 8192 / 256 / 256 |
| Materials / images / textures / samplers | 1024 / 128 / 256 / 256 |
| Buffers / buffer views / accessors | 256 / 32768 / 32768 |
| Vertices per primitive / total | 1,048,576 / 2,097,152 |
| Indices per primitive / total | 3,145,728 / 6,291,456 |
| Animation channels / total keyframes | 8192 / 2,097,152 |
| Node draw bindings / referenced vertices / JSON nesting | 16384 / 8,388,608 / 64 |

PNG/JPEG headers are inspected before decoding; the decoder also has actual
allocation hooks, including temporary buffers and conservative realloc overlap.
The parser uses budgeted allocation hooks without modifying vendored cgltf.
Conversion counts charge repeated primitive/accessor/channel references separately,
and texture budgeting includes each image/sampler relation and requested mip chain.
Decoded output and live decoder memory are separate budgets, so a small encoded
image cannot evade the decode limit. Limits are ceilings, not an assurance that
every combination at the maximum fits all other budgets; they apply per asset
load, not to the aggregate application cache or number of runtime instances.

Filesystem and Title Storage reads check file size before allocating. A virtual
reader now receives `(relativePath, maxBytes)` and must enforce that limit before
allocating its result. Tests can pass smaller `LoadLimits` directly to `loadGltf`;
production caches use the defaults. Diagnostics retain the source and resource path.

## Validation

Loader fixtures cover malformed accessors, buffers, skinning and animations;
asset tests regenerate deterministic models. Renderer changes additionally need
real Vulkan execution. See [development checks](development.md) and
[plugin model contracts](plugin-extensions.md#entities). The [original integration record](history/gltf-integration.md) preserves its
dated commits, test totals and validation limits.

## Performance validation

The existing terrain-plus-128-model demo is retained. Model-only measurements
isolate animation/submission costs from terrain GPU time:

```sh
python3 tools/model_benchmark.py cmake-build-windows/Release/minecraftc.exe --output build-local/model-benchmark
```

The runner alternates instanced and single-instance recording with identical new
shaders, 128/512 models, one/four primitives, and three repeats per case. Each run
warms up for 600 frames and records 1800 frames at the default 1280x720. Logs and
JSON include device, load/animation/queue times, model draws, palette/instance
bytes, and frame P50/P95. A repeat-median regression over 5% fails the comparison;
opaque model draws must decrease by at least 90%. No fixed FPS is promised across GPUs.

`--benchmark-models=128|512`, `--benchmark-model-parts=1|4`,
`--benchmark-model-only` and `--benchmark-single-instance` are diagnostic demo
options and require `--benchmark-frames=N`. The native model readback test also
checks 256-joint skinning, snapshot lifetime, multi-primitive palette sharing,
forced 16 KiB pages, stable Blend rendering and stale handles after resource clear.

Use `--counts 512 --parts 1 --reverse-order` on the Python runner to repeat an
isolated case with instancing first. Keep the original logs when investigating
run-order or timing variability; do not discard slow runs.

Windows Release measurements on Intel Iris Plus Graphics (2026-10-23,
1280x720, 2x MSAA, 600 warmup/1800 samples) produced these repeat medians.
Values show single-instance recording followed by instancing on the same new
pipeline; they are not a claim about other devices or the previous renderer.

| Instances / primitives | Model draws | P50 ms | P95 ms | Palette bytes/frame |
|---|---|---|---|---|
| 128 / 1 | 128 → 4 | 2.050 → 1.948 | 2.959 → 2.824 | 57,344 |
| 512 / 1 | 512 → 4 | 5.063 → 5.079 | 6.909 → 7.069 | 229,376 |
| 128 / 4 | 512 → 16 | 4.736 → 3.537 | 6.712 → 4.867 | 57,344 |
| 512 / 4 | 2048 → 16 | 6.989 → 6.479 | 9.910 → 8.677 | 229,376 |

Each case has three repeats. The 512/1 case initially exceeded the 5% P95
threshold, then passed three reverse-order repeats; its table values include
all six repeats. Animation CPU also varied during that comparison. The combined
P50/P95 changes are below 5%; the isolated initial failure remains in local
validation evidence. Palette upload does not multiply with primitive count.
