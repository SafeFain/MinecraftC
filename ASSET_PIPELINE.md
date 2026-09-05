# Asset pipeline

MinecraftC separates authored, generated, imported, and declarative assets:

- `assets/textures/source/` contains project-authored or adapted source PNGs.
- `assets/textures/generated/` contains reproducible output, the
  block `atlas.png`/`atlas.json` pair, and the separate
  `items_atlas.png`/`items_atlas.json` pair.
- `assets/textures/third_party/` contains imported packs with their licenses.
- `assets/textures/definitions/` contains JSON block, item, texture, style, and
  entity-material definitions. The default generated style is
  `bright-comfortable` (generator version 3); it uses dependency-free
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
  --build-items-atlas --build-entity-atlas --build-entity-skins \
  --seed 213785369 --output assets/textures/generated

# Default-seed CMake target also synchronizes embedded GLB skins
cmake --build build-local --target texture_generator
```

Available build switches are `--generate`, `--validate`, `--build-atlas`,
`--build-items-atlas`, `--build-entity-atlas`, `--build-entity-skins`,
`--build-ios-icon`, `--build-android-icon`, and `--build-desktop-icons`.
Generation and review options include `--seed`, `--output`,
`--candidate-count`, `--contact-sheet`, `--preview`, `--visual-report`,
the platform-icon output options, item/block definition overrides, and
repeatable `--local-seed MATERIAL=SEED`. Operations may be combined. Run
`python3 tools/texture_generator.py --help` for the complete argument list. A
local seed selects one material candidate without perturbing any other material
and is recorded in atlas metadata.

Generation combines toroidal low-frequency fields, material-specific connected
features and sparse accents. Absolute thresholds keep middle tones dominant.
Soils, turf, stone layers, grains, wood fibers, end grain, foliage and ores use
separate structures; manufactured surfaces use explicit semantic drawings.
No enlarged low-resolution noise or aliased 15-pixel domains are used.

Validation rejects non-16x16 files, undeclared palette colors, non-binary alpha,
opaque black outlines and ore outside 2-4 connected clusters. Toroidal seam
variation must remain within 2.60 times interior variation; grass sides check
horizontal wrapping and an upper turf cap. Large calm planes and limited palette
occupancy are allowed. Frequency and flat-region statistics are diagnostic
report fields rather than instructions to add noise. PNG and atlas output use
nearest-neighbor sampling with no resampling.

For visual review, generate eight deterministic candidates per material:

```bash
python3 tools/texture_generator.py --generate --validate --build-atlas \
  --seed 213785369 --candidate-count 8 --contact-sheet \
  --output assets/textures/generated
```

`contact_sheet.png` shows the first candidate in an eight-column grid; each
cell contains a native tile, a 3x enlargement and a 4x4 repeat. Further candidates
use numbered sheets. Companion JSON lists pages, candidate and local seeds.
These development files are not
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
validated for seamless repetition. Tool sprites use disjoint grip, connector and working-head masks; highlights
are clipped to their semantic part. Every icon is 16x16 RGBA, uses
only alpha 0 or 255, has no antialiasing, avoids broad pure-black outlines, and
is packed without resampling. Metadata and runtime use nearest filtering.

`block_item_icon` samples logical top, front and side materials from `blocks.json` and
composes an isometric inventory cube without changing block tiles, seamless
validation, or the existing block atlas format.

Selection order is an authored PNG in `source/items/`, the automatic icon, a
PNG in `legacy/items/`, then the missing-resource/existing runtime icon.
`items_atlas.json` records source kind, generator category, tile index, grid
dimensions, and this priority. Items absent from the atlas retain the old path.

## Entity materials

`--build-entity-atlas` deterministically creates eleven 16x16 wrapping material
swatches for ten passive/hostile mobs and the item fallback. Unlike the
legacy portrait sheet, these tiles contain hide, fleece, feather, bone, skin,
robe, or carapace patterns that remain coherent on every cuboid model part. The
4x4 atlas and its metadata are written to `generated/entity_atlas.png` and
`generated/entity_atlas.json` with nearest filtering.

`--build-entity-skins` creates eleven original 64x64 runtime skins for ten mobs
and the player under `generated/entity_skins/` plus
`generated/entity_skins.json`. Each 4x4 skin
contains named 16x16 regions for all six head faces, all six body faces,
primary and secondary limbs, detail, and fallback material. Head and body
backgrounds come from continuous cube-space fields so adjacent faces remain
coherent; eyes, mouths, muzzles, beaks, and other identifying features are
drawn as explicit overlays. Output remains nearest-filtered pixel art.

## Entity models and actions

`tools/generate_entity_models.py` version 5 deterministically emits the ten runtime
GLBs and their versioned `.anim.json` action graphs. GLBs hold geometry,
skins, embedded copies of the generated 64x64 entity skins, per-face UVs, and
keyframes; action graphs hold runtime layers,
masks, transitions, priorities, queues, and gameplay event times. Regenerate
with the `texture_generator` CMake target, which also updates embedded skins;
verify with `python3 tests/test_entity_models.py`.

## Add a block and material

1. Append the serialized `BlockId` before `COUNT`; never reorder existing IDs.
2. Add the logical texture name and palette/generation rule to
   `tools/texture_generator.py`, or place an authored PNG in `source/` and
   record its provenance in `ASSET_SOURCES.md`.
3. Reference the logical texture from `definitions/blocks.json`. Use `all`, or
   `top`/`bottom`/`side` face keys, followed by optional
   `front`/`back`/`left`/`right` overrides. Blocks without orientation state
   use world -Z as their front.
4. Add the corresponding item icon reference to `definitions/items.json`.
5. Regenerate, validate, and build the atlas. Do not add UV coordinates to C++.
6. Add the semantic C++ material enum/name mapping only when the new block must
   coexist with the legacy procedural compatibility atlas.
7. Run the Release build, full CTest suite, `git diff --check`, and Vulkan smoke
   test.

For third-party art, place files under `third_party/<pack>/`, retain the
upstream license, and add it to the third-party table in `README.md`. Never copy assets from
Minecraft or another game without a compatible license.
# Entity model generation

Run `python3 tools/generate_entity_models.py --output assets/models/entities`
to reproduce all ten runtime GLBs. `python3 tests/test_entity_models.py`
performs byte-for-byte regeneration plus the skin, animation, vertex semantic,
embedded PNG, and nearest-sampler contract checks.

## Generator v3: clean natural pixel art

The `bright-comfortable` style keeps its identifier and advances the generator
revision to 3. Definition format versions and serialized game IDs are unchanged.
Absolute thresholds from `style.json` preserve middle-color planes; material
features supply sparse connected accents instead of rank-equalized noise.
Periodic tiles may be translated as a whole to put the repeat cut at ordinary
variation, without copying borders or changing the torus topology. The seam
validator retains its 2.60 ratio limit. Palette membership, alpha, ore separation
and deterministic output are checked; minimum color occupancy and forced
fragmentation are no longer aesthetic requirements.

Functional blocks have dedicated front, top, side and bottom art. Tree species
have separate end grain. Crossed plants use PNG top-left rows, including upright
inventory sprites. Existing bed geometry selects linen face tiles. Food shapes,
material-clipped tool parts, species markings, skin-colored hands and player
hair are original hand-programmed pixel designs. No image service or external
texture pack is required. Dark volcanic materials keep their dark anchors.

Generate runtime resources and synchronized GLB skins together:

```bash
cmake --build build-local --target texture_generator
python3 tools/texture_generator.py --preview --candidate-count 1 \
  --output build-local/texture-review/current
python3 tools/texture_review.py --before /path/to/previous/generated \
  --output build-local/texture-review
```

The review tool writes native/4x/3x3 repeat comparisons, per-face functional
sheets, all item/entity sheets, and quantitative lightness, neighbor-difference
and dark-pixel statistics. Previews do not change runtime resources. Approximated
lighting previews supplement actual Vulkan checks; they do not simulate the
complete renderer. No global exposure or lighting change is part of v3.

### Leaf cutout minification

Leaf art uses connected gaps (roughly 15–19% at the default seed) and layered
leaf clusters. Runtime tile-local mip generation preserves source cutout
coverage for the six leaf materials at 8x8, 4x4 and 2x2. It retains the lowest
averaged-alpha texels as gaps, within one texel of source coverage, instead of
averaging every small hole above the 0.1 cutoff. Color downsampling still uses
uncorrected alpha-weighted levels; correction does not feed back or cross tile
borders. The terminal 1x1 mip remains averaged for distant canopies and the
opaque-leaf setting. When transparency is disabled, the shader fills gaps
with that species average at 0.55 linear-light intensity, separating shaded
interior foliage from the visible leaf clusters without adding noise.
Transparent-mode sampling and scene lighting are unchanged. `asset_definition_tests` checks the actual runtime mip data.

The follow-up art pass restores modest grass/stone/wood/leaf complexity after
the initial v3 simplification. In-world visual acceptance is performed by the
user; automated validation views generated material sheets only.
