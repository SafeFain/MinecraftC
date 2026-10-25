# Content packages

A package contains definitions, translations, assets and optional C++. The same
source can be published to `mods/<id>/` or explicitly compiled into the application.
Existing Heaven and legacy packages retain their loading behavior.

## Create and publish

From a checkout, using your Python 3 executable:

```sh
python3 tools/content_package.py create content/my_expansion --id my_expansion --type data
python3 tools/content_package.py build content/my_expansion --output dist
```

Use `--type native` to include `code/plugin.cpp` and a CMake project. `build` then
configures CMake, builds the desktop module and copies it to `dist/my_expansion/`.
Options include `--config Debug` and `--sdk /path/to/sdk`. CMake and a C++17 compiler
must be available. Build caches are in `.content-package-build/` beside the output
directory, outside the source. Public commands refuse to overwrite existing source
or output directories; choose a new output parent for another build.

Portable releases include the tool at `sdk/tools/content_package.py` and the shared
CMake helper at `sdk/cmake/ContentPackage.cmake`. The installed tool finds the
adjacent SDK; the checkout tool uses the repository SDK. Python dependencies are
standard-library only.
The creator writes LF text and adds a development `.gitattributes` that preserves
resource bytes across checkouts. Runtime files are copied without rewriting them.

Copy the built directory into user-data `mods/`, enable the package and restart.
Use a new disposable world during development. Changing gameplay package bytes
changes existing strict world compatibility requirements.

## Source layout

```text
my_expansion/
  mod.json
  materials/blue_brick.json
  blocks/blue_brick.json
  items/blue_brick.json
  items/crystal.json
  recipes/blue_brick.json
  lang/en_us.json
  lang/zh_cn.json
  textures/                 optional PNGs
  models/                   optional glTF/GLB and animation files
  audio/                    optional WAVs
  shaders/                  optional SPIR-V
  code/plugin.cpp           optional C++
  CMakeLists.txt            native build
  LICENSE.md               code/asset licenses and provenance
```

The manifest explicitly selects the new format:

```json
{"id":"my_expansion","name":"My Expansion","version":"1.0.0","api":1,"package_format":1}
```

Do not combine `package_format` with `data`. Native manifests additionally use the
existing platform-library map under `native`; the scaffold supplies it. Dependencies,
versions, visual-only restrictions, configuration and enable switches work as before.
Android/iOS support external data packages and compiled-in code, not external native
libraries.

## Definitions and references

Each JSON file contains one object using the corresponding existing
[`content.json` fields](plugins.md#data-content), without an enclosing array. Example:

```json
{"key":"my_expansion:blue_brick","name":"Blue Brick",
 "name_key":"my_expansion:block.blue_brick",
 "materials":"my_expansion:blue_brick","hardness":1.5,
 "drop":"my_expansion:blue_brick"}
```

Supported definition directories are `materials`, `blocks`, `block_families`,
`block_geometry`, `items`, `dimensions`, `recipes`, `entities`, `generators`,
`block_behaviors`, `item_behaviors`, `loot_tables`, `structures`, `player_actions`,
`environments`, `particles`, `held_models`, `playlists`, `render_resources`,
`render_passes` and `item_icons`. Subdirectories are supported; only `.json` files are definitions.
Within each category files sort by normalized package-relative path. Render
resources sort by explicit dependencies, retaining path order among independent nodes.

Keys are explicit identities, independent of filenames. Geometry/block behavior
files use `block`; item behavior/held model files use `item`, as before.
Icon override files also use `item`, paired with `image`; materials may include
an optional `sequence` object (see [data content](plugins.md#data-content)). All resource
paths are relative to the **package root**, not the definition file. Cross-package
references require dependencies. Definition discovery allows at most 4096 files,
128 MiB total JSON and 16 MiB per file, in addition to existing capability limits.
Links and parent/absolute resource paths are rejected.

Optional `name_key` binds named content to its package translation. If `lang/`
exists, `en_us.json` is required. Dictionaries use namespaced keys and existing
language fallback. A block and its item may share the same name binding. Missing
English text or conflicting bindings reject the package.

## Data and code lifecycle

New-format packages execute one rollback transaction:

1. Discover and parse all definitions.
2. Register materials, blocks, shape families/geometry, items, dimensions and languages.
3. Execute optional code `load`, then REGISTER listeners.
4. Register remaining data, behaviors and presentation/render bindings.
5. Validate the complete package and freeze before initialization/play.

C++ can resolve data-defined blocks/items/dimensions in `load` and register custom
generators or actions referenced by later data. A dimension may name a generator
registered later. A data item's `placed_block` must resolve in step 2, so it cannot
refer to a block created by code in step 3: define the base block in JSON instead.
Data and code cannot register the same definition twice. Any failure rolls back the
package and blocks required dependents. Errors include package, source file, content
and field context where available. Legacy single-data-file packages retain their
original code-before-data lifecycle.

## Compile into the application

Explicitly list source roots at configuration:

```sh
cmake -S . -B build-local -DMINECRAFTC_CONTENT_PACKAGES=/absolute/path/to/my_expansion
```

The value is a CMake semicolon-separated list. The manifest controls default enable
state; use `enabled_by_default: false` for an opt-in built-in package. Registration
generates unique code query symbols, reads the same manifest and creates a clean
runtime resource inventory. For compiled-in code the generated behavior revision
includes the package's code-tree and CMake input hashes, so code-only changes also
change save/LAN requirements. Text build inputs normalize CRLF/LF before hashing;
runtime resource fingerprints remain byte-exact. Desktop development uses an explicit staging mapping;
installed applications use `plugins/<id>` under their normal asset root. iOS embeds
the staged files. Virtual Title Storage reuses the bounded materialization cache.

Android Gradle takes `-PminecraftcContentPackages=/absolute/path/to/package`, also
semicolon-separated, and optional `-PminecraftcPython=/path/to/python3`. These feed
CMake registration and prepare matching packaged assets. Platform/device validation
still depends on the available build environment.

The shared function is `minecraftc_add_content_package(target ROOT <source> MODE
MODULE|BUILTIN)`. Standalone packages use MODULE; the host list uses BUILTIN. Both
compile `code/*.cpp` and `code/*.c` recursively against the public SDK only. User
mods are never automatically compiled, and cannot claim a compiled-in package ID.

## Publication and compatibility

Publication includes the manifest, known definition directories, resource trees,
translations, current platform module and root `LICENSE[.md|.txt]`/
`NOTICE[.md|.txt]`. Source, CMake files, README and caches are excluded. Keep asset
provenance with resources or in LICENSE/NOTICE.

The existing complete-package fingerprint algorithm remains in use. Built-ins add
their resource inventory and behavior revision; desktop and materialized resources
retain identical files. Dynamic and compiled-in packages do not promise interchangeable
world or LAN fingerprints. There is no hot reload, save-format change or generation change.

The complete [`package_demo`](../examples/plugins/package_demo) defines blue bricks,
a crystal, recipes and translations. `/package_demo:gift` queues four bricks;
using the crystal queues one. Brick placement remains normal. Its original procedural
color, code and translations are documented in LICENSE.md.
