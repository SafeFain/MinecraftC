# glTF entity model and animation engine

MinecraftC's glTF entity model and animation engine was merged into `main` on
2026-07-29.

## History

- `672af93`: preserve and optimize cloud rendering.
- `f6d2e0b`: add the glTF loader, animation system, GPU model renderer,
  registry, tests, generator, and eight runtime GLBs.
- `37b9acc`: merge the feature line into `main`.

## Delivered behavior

- Validated glTF 2.0/GLB loading through vendored cgltf 1.15, including skins,
  node hierarchy, embedded PNG images, supported interpolation modes, strict
  accessor validation, and a 64-joint limit.
- Main-thread Vulkan model resources, integer joint attributes, GPU linear
  blend skinning, opaque/masked/blended passes, nearest sRGB textures,
  environment lighting, fog, and gamma handling.
- Shared per-type entity assets and stable per-ID animation instances with
  idle/walk selection, 0.15-second locomotion blending, hurt priority, and a
  render-only death presentation lasting exactly 1.0 second.
- Cow, pig, sheep, chicken, zombie, skeleton, spider, and Blastling render from
  original deterministic multi-part GLBs. Dropped items, arrows, and primed TNT
  retain the named compatibility cube path and legacy entity atlas.
- Save format version 8, gameplay AI, physics, spawning, loot timing, and world
  generation remain unchanged.

## Assets and regeneration

`tools/generate_entity_models.py` version 1 with seed `0x4D43474C` reproduces
all eight CC0-1.0 GLBs byte-for-byte. Assets use Y-up meters, local forward
`-Z`, feet/base at `Y=0`, embedded original pixel textures, genuine skins, and
the required `idle`, `walk`, `hurt`, and `death` clips. Details are in
`assets/models/entities/README.md`.

## Validation evidence

- Post-merge Release configuration and full build passed.
- CTest passed 23/23 after the merge.
- `git diff --check` passed.
- Installation contained exactly eight entity GLBs and both model shaders.
- A six-second xvfb Vulkan startup reached normal initialization and presented
  the first frame without validation errors.
- Project loader tests load all eight runtime GLBs; deterministic asset tests
  regenerate and compare them byte-for-byte.

Khronos glTF Validator was unavailable and was not run. Manual in-world visual
inspection of all eight spawned mobs was not run. Staged missing-asset startup
attempts were blocked by transient GLFW/xvfb initialization; missing, corrupt,
and over-limit failures remain covered by registry and loader tests.

The temporary `codex/gltf-entity-model-engine` branch and
`/tmp/minecraftc-gltf-model-engine` worktree were removed after integration.
