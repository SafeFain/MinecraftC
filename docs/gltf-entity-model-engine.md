# glTF entity model and animation engine

## Runtime contracts

- Validated glTF 2.0/GLB loading through vendored cgltf 1.15: skins, node hierarchy,
  embedded PNGs, supported interpolation, accessor validation and a 64-joint limit.
- Main-thread Vulkan model resources, integer joint attributes, linear blend
  skinning, opaque/masked/blended passes, nearest sRGB textures, lighting and fog.
- Shared per-type assets with per-instance layered animation graphs; missing or
  invalid graphs use the builtin fallback. See [animation behavior](entity-animation-engine.md).
- Generator v5 supplies eleven original mob GLBs, including villager, zombie
  villager and iron golem. Player and held-item assets have separate ownership.
  Dropped items use their block/tool/icon rendering paths; arrows and primed TNT
  retain their entity rendering paths.
- Animation mixer/transition state is transient. Current world persistence is v21;
  plugin entity types persist stable keys and revisioned instance state.

## Assets and regeneration

See [entity model authoring](../assets/models/entities/README.md) for coordinates,
clips and deterministic generation; [player assets](../assets/models/player/README.md)
and [asset pipeline](../ASSET_PIPELINE.md) cover related resources.

```sh
python3 tools/generate_entity_models.py --output assets/models/entities
python3 tests/test_entity_models.py
```

## Validation

Loader fixtures cover malformed accessors, buffers, skinning and animations;
asset tests regenerate deterministic models. Renderer changes additionally need
real Vulkan execution. See [development checks](development.md) and
[plugin model contracts](plugin-extensions.md#entities). The [original integration record](history/gltf-integration.md) preserves its
dated commits, test totals and validation limits.
