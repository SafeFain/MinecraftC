# Asset pipeline

MinecraftC separates authored, generated, imported, and declarative assets:

- `assets/textures/source/` contains project-authored or adapted source PNGs.
- `assets/textures/generated/` contains reproducible generator output and the
  packed `atlas.png`/`atlas.json` pair.
- `assets/textures/third_party/` contains imported packs with their licenses.
- `assets/textures/definitions/` contains JSON block, item, and texture names.

The client loads `atlas.json`, `blocks.json`, and `items.json` before chunk
meshing. Logical material names are converted to atlas slots from metadata;
block faces come from `blocks.json`. Existing source images and procedural
tiles remain compatibility fallbacks when generated files or definitions are
missing. The runtime atlas still builds per-tile mip levels to prevent bleeding.
Every registered `BlockTexture` now has a generated PNG loaded by its logical
metadata name. Older authored and runtime-procedural tiles are fallback-only.

## Generate and validate

From the repository root:

```bash
python3 tools/texture_generator.py --generate --validate --build-atlas \
  --seed 213785369 --output assets/textures/generated

# Equivalent default-seed CMake target
cmake --build build-local --target texture_generator
```

Available switches are `--generate`, `--validate`, `--build-atlas`, `--seed`,
`--output`, `--candidate-count`, `--contact-sheet`, and repeatable
`--local-seed MATERIAL=SEED`. Operations may be combined. A local seed selects
one material candidate without perturbing any other material and is recorded
in atlas metadata.

Generation uses three structural levels. Irregular radial fields provide a
soft macro value layout directly on a 16x16 torus; material-specific growers
create meso-scale soil clods, grass tufts/blades, stone flakes/cracks, sand
ripples, wood fibers/rings, leaf clusters/holes, and connected ore deposits;
sparse correlated accents break accidental regular edges. It never enlarges a
low-resolution control image and never copies opposing borders. The previous
5x5 interpolated field plus axis-aligned 3x3 grain and 15x15 aliased sampling
was removed because it caused rectangular chunks, center crossings, shared
camouflage structure, and a visible 15-pixel repeat.

Validation rejects non-16x16 files, excess palette colors, non-binary alpha,
opaque black outlines, and ore outside 2-4 connected clusters. For natural
materials it reports explicit detected/allowed values for long near-color
runs, large or rectangular flat connected areas, center-axis bias, 2/4/8-pixel
autocorrelation, meso-frequency transitions, and toroidal seam discontinuity.
Wood, bark, farmland, and cactus are declared directional exceptions, while
their toroidal seams are still checked. Grass-side validation additionally
requires its green layer in the source image's upper region. PNG output and
the atlas use nearest-neighbor sampling; no resampling is applied.

For visual review, generate eight deterministic candidates per material:

```bash
python3 tools/texture_generator.py --generate --validate --build-atlas \
  --seed 213785369 --candidate-count 8 --contact-sheet \
  --output assets/textures/generated
```

`contact_sheet.png` places one material on each row and candidates in columns;
each cell contains the original tile and an 8x8 repeat. Its companion JSON
lists candidate and selected local seeds. These two development files are not
part of `atlas.png` or `atlas.json`.

## Add a block and material

1. Append the serialized `BlockId` before `COUNT`; never reorder existing IDs.
2. Add the logical texture name and palette/generation rule to
   `tools/texture_generator.py`, or place an authored PNG in `source/` and
   record its provenance in `ASSET_SOURCES.md`.
3. Reference the logical texture from `definitions/blocks.json`. Use `all`, or
   `top`/`bottom`/`side` face keys.
4. Add the corresponding item icon reference to `definitions/items.json`.
5. Regenerate, validate, and build the atlas. Do not add UV coordinates to C++.
6. Add the semantic C++ material enum/name mapping only when the new block must
   coexist with the legacy procedural compatibility atlas.
7. Run the Release build, full CTest suite, `git diff --check`, and OpenGL smoke
   test.

For third-party art, place files under `third_party/<pack>/`, retain the
upstream license, and fill in `THIRD_PARTY_LICENSES.md`. Never copy assets from
Minecraft or another game without a compatible license.
