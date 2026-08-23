# Asset pipeline

MinecraftC separates authored, generated, imported, and declarative assets:

- `assets/textures/source/` contains project-authored or adapted source PNGs.
- `assets/textures/generated/` contains reproducible output, the unchanged
  block `atlas.png`/`atlas.json` pair, and the separate
  `items_atlas.png`/`items_atlas.json` pair.
- `assets/textures/third_party/` contains imported packs with their licenses.
- `assets/textures/definitions/` contains JSON block, item, texture, style, and
  entity-material definitions. The default generated style is
  `bright-comfortable` (generator version 2); it uses dependency-free
  OKLab/OKLCH role palettes and keeps the runtime tile contracts unchanged.

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
  --build-items-atlas --build-entity-atlas \
  --seed 213785369 --output assets/textures/generated

# Equivalent default-seed CMake target
cmake --build build-local --target texture_generator
```

Available switches are `--generate`, `--validate`, `--build-atlas`,
`--build-items-atlas`, `--build-entity-atlas`, `--seed`,
`--output`, `--candidate-count`, `--contact-sheet`, `--preview`,
`--visual-report`, and repeatable
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
part of `atlas.png` or `atlas.json`. `--preview` additionally emits
`block_preview.png` (tile, repeat, grass/structure board, and noon/dusk/cave
lighting samples), `items_contact_sheet.png`, `entity_contact_sheet.png`,
`entity_semantic_preview.png`, and `visual_report.json`. The report contains
OKLab lightness/chroma percentiles, role palettes, binary-alpha coverage,
edge/seam/periodicity metrics, and family structure-correlation summaries;
CMake exposes the same operation as
`cmake --build build-local --target texture_preview`.

## Item icons

`definitions/item_icons.json` (version 2) declares `block_texture`, `item_sprite`, and
`block_item_icon`, plus logical names, templates, and palettes. Concrete C++
`ItemId` values are not embedded in Python. Initial templates are sword,
pickaxe, axe, shovel, hoe, stick, ingot, gem, coal, and torch; shared materials
are wood, stone, copper, iron, and gold.

The definition covers every registered non-empty item. Additional silhouettes
handle fibers, feathers, bones, arrows, crops, food, bows, shields, armor, and
flint. A C++ regression test derives logical names from the live item registry
and rejects any registered item missing from `items_atlas.json`.

Item sprites have transparent backgrounds and, unlike block textures, are not
validated for seamless repetition. Tool sprites use discrete outline, handle,
working-part, and top-left highlight layers. Every icon is 16x16 RGBA, uses
only alpha 0 or 255, has no antialiasing, avoids broad pure-black outlines, and
is packed without resampling. Metadata and runtime use nearest filtering.

`block_item_icon` samples logical top and side materials from `blocks.json` and
composes an isometric inventory cube without changing block tiles, seamless
validation, or the existing block atlas format.

Selection order is an authored PNG in `source/items/`, the automatic icon, a
PNG in `legacy/items/`, then the missing-resource/existing runtime icon.
`items_atlas.json` records source kind, generator category, tile index, grid
dimensions, and this priority. Items absent from the atlas retain the old path.

## Entity materials

`--build-entity-atlas` deterministically creates nine 16x16 wrapping material
swatches for the passive animals, hostile mobs, and item fallback. Unlike the
legacy portrait sheet, these tiles contain hide, fleece, feather, bone, skin,
or carapace patterns that remain coherent on every cuboid model part. The 3x3
atlas and its metadata are written to `generated/entity_atlas.png` and
`generated/entity_atlas.json` with nearest filtering.

`--build-entity-skins` creates nine original 64x64 runtime skins under
`generated/entity_skins/` plus `generated/entity_skins.json`. Each 4x4 skin
contains named 16x16 regions for all six head faces, all six body faces,
primary and secondary limbs, detail, and fallback material. Head and body
backgrounds come from continuous cube-space fields so adjacent faces remain
coherent; eyes, mouths, muzzles, beaks, and other identifying features are
drawn as explicit overlays. Output remains nearest-filtered pixel art.

## Entity models and actions

`tools/generate_entity_models.py` deterministically emits the eight runtime
GLBs and their versioned `.anim.json` action graphs. GLBs hold geometry,
skins, embedded copies of the generated 64x64 entity skins, per-face UVs, and
keyframes; action graphs hold runtime layers,
masks, transitions, priorities, queues, and gameplay event times. Regenerate
and verify them with `python3 tests/test_entity_models.py`.

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
upstream license, and add it to the third-party table in `README.md`. Never copy assets from
Minecraft or another game without a compatible license.
# Entity model generation

Run `python3 tools/generate_entity_models.py --output assets/models/entities`
to reproduce all eight runtime GLBs. `python3 tests/test_entity_models.py`
performs byte-for-byte regeneration plus the skin, animation, vertex semantic,
embedded PNG, and nearest-sampler contract checks.
