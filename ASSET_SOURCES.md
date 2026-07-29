# Asset source record

Use one section per original or generated asset family.

## Template

- Logical names:
- Local paths:
- Creator/tool:
- Creation date:
- Seed and generator version:
- Source or reference URLs:
- License:
- Modifications:

## Procedural voxel textures

- Logical names: every entry in `assets/textures/definitions/textures.json`,
  covering all registered block faces plus the future copper ore material
- Local paths: `assets/textures/generated/`
- Creator/tool: `tools/texture_generator.py`
- Seed and generator version: atlas metadata records both; format version 1
- License: original project-generated assets

## Procedural entity materials

- Logical names: cow, pig, sheep, chicken, zombie, skeleton, spider, blastling, item
- Local paths: `assets/textures/generated/entities/`, `assets/textures/generated/entity_atlas.png`
- Creator/tool: `tools/texture_generator.py --build-entity-atlas`
- Creation date: 2026-07-27
- Seed and generator version: atlas metadata records seed; format version 1
- License: original project-generated assets
# Entity GLBs

The files under `assets/models/entities/` are original, deterministic
MinecraftC assets. Generator version, seed, coordinate contract, and CC0-1.0
license are documented in that directory's README. They contain no copied
Minecraft/Mojang model or texture data.
