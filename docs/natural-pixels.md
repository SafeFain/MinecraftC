# 自然像素 / Natural Pixels

Natural Pixels is an original 16×16 block and item texture package inspired by
general modern voxel pixel-art conventions. It is bundled on Linux, Windows,
macOS, Android and iOS and is **disabled by default**.

Open **Plugins**, enable **自然像素 / Natural Pixels**, and restart the game.
Disable and restart to restore the default textures. Safe mode disables it.
This visual-only choice does not change world requirements or LAN compatibility;
each player may select their own textures. Heaven remains independently optional.

## Contents

All current logical block materials and item icons are covered, including Nether,
wood variants, doors, crops, workstations, fluids and enabled Heaven content.
Block icons sample the package's own top/front/side images. Existing material
variants and animation counts/rates are preserved, with paired normal/property
maps and the host's tile-safe linear-light mip filtering.

There are 297 runtime material replacements and 433 icons. The shared authoring
catalog additionally retains an unused copper-ore tile; it is generated for
comparisons but excluded from runtime replacements because no BlockTexture or
registered block currently refers to it.

Items using the sprite atlas share the selected images between inventory, hand
and dropped forms. Cuboid tools retain their models/animations and derive material
swatches from the selected item images. Custom model geometry/embedded skins,
entity skins, interface art and sky effects remain provided by their usual owners.

## Authoring and reproduction

```sh
python3 tools/natural_pixels.py
python3 tools/natural_pixels.py --review build-local/natural-pixels-review
python3 tools/natural_pixels.py --output build-local/natural-pixels-regenerated
python3 tests/test_natural_pixels.py
```

The first command updates only this package. The default texture generator never
exports into this package. `authoring.json` snapshots palettes, patterns, item
templates, normal/property profiles and material sequence choices; update this
file and the pattern drawing code together for deliberate art changes. Shared
helpers provide deterministic seed domains, PNG encoding, semantic masks,
physical maps and isometric block icons.

Review output contains all material comparisons, enlarged/native tiles, 3×3
repeats, icon boards and five mip levels. It is development output, not shipped
runtime content. `resources.sha256.json` inventories package bytes for mobile
Title Storage materialization and installation/release checks.
Tile PNGs use stored DEFLATE so zlib and zlib-ng reproduce identical resource
bytes. Review-only large PNGs may use normal compression.

The package's `mod.json` uses `package_format: 1`, `visual_only: true`,
`enabled_by_default: false`, and `after: ["official_heaven"]`. It has no native
module or required Heaven dependency. PNG paths are package-relative. Existing
plugin ordering decides overlapping replacements; later visual overrides win,
and a failed registration rolls back the complete package.

All new pixel artwork and authoring palettes are CC0-1.0. No official Minecraft
images or third-party resource-pack assets are included. See the package license
and [asset sources](../ASSET_SOURCES.md).
