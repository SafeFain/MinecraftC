# MinecraftC

MinecraftC is a C++17 voxel sandbox built with SDL3 and Vulkan. It
features deterministic infinite worlds, asynchronous chunk streaming, Creative,
Survival, and Spectator modes, dynamic lighting and weather, and English and
nine other localized interfaces.

## Versioning and releases

The root `VERSION` file is the single version source. It uses
`X.Y.Z-alpha|beta|rc|release`, with an optional `.N` after alpha, beta, or rc
(for example `1.4.0-beta.2`). The core version can stay at `1.4.0` across any
number of commits; increase the channel iteration only when you want to identify
a new prerelease. Iterations run from 1 to 999. Existing unnumbered versions
remain supported; `release` does not take an iteration.

| VERSION | Runtime display | Release tag | GitHub release type |
|---|---|---|---|
| `1.4.0-beta.1` | `Beta.1-1.4.0` | `v1.4.0-beta.1` | Prerelease |
| `1.4.0-beta.2` | `Beta.2-1.4.0` | `v1.4.0-beta.2` | Prerelease |
| `1.4.0-rc.1` | `RC.1-1.4.0` | `v1.4.0-rc.1` | Prerelease |
| `1.4.0-release` | `Release-1.4.0` | `v1.4.0-release` | Release |

CMake, Android/iOS/macOS metadata, runtime output, package names, and release CI
all derive from this file. Branch pushes, pull requests, and manual workflow runs
build and test; desktop CI continues to run CTest. A `v*` tag push publishes all
platform packages only if the tag exactly matches `v<VERSION>` and every platform
job passes. Manual runs do not publish, including runs on a tag. Package filenames include the full version
(for example `MinecraftC-1.4.0-beta.2-linux-x86_64.tar.gz`).

Android versionCode and Apple bundle build numbers use
`(major * 10000 + minor * 100 + patch) * 10000 + channel * 1000 + iteration`,
where alpha/beta/rc/release are 1/2/3/4 and an omitted iteration is 0. This keeps
iterations and subsequent stages increasing and upgrades over the former codes.
Major is limited to 20, minor/patch to 99, and the final code must be at most
Android's 2100000000 limit. The Apple short version remains `X.Y.Z`.

## Highlights

- Deterministic terrain, biomes, caves, ores, vegetation, and trees across
  Y=-64..319.
- 30 biomes, 15 blended macro terrain archetypes, drainage-basin rivers, seven
  vegetation and tree forms, fluids, farming, fire, TNT, and moving voxel clouds
  with progressively coarser clouds beyond the selected exact-cloud range,
  extending out to 4096 blocks independently of terrain LOD.
- Distance-prioritized generation, greedy meshing, ambient occlusion, dual-channel
  lighting, configurable cascaded shadows, transparent materials, and persistent
  spawn caches.
- Distant-terrain LOD outside the full chunk radius, with 32-4096 chunk distance,
  four load presets, four precision presets, and progressively refined caches.
- Crafting, furnaces, containers, Java 1.9-style charged melee combat, hunger,
  fast regeneration, armor/shields, weather, commands, and persistent
  players, entities, and worlds.
- Classic fishing with craftable durable rods, visible bobbers and lines, manual
  bite/reel timing, fish/junk/open-water treasure, and edible/cookable cod and salmon.
- JSON-driven block, 286-item, and entity atlases with a deterministic 16x16
  texture pipeline.
- Keyboard and mouse, controller, and native multi-touch input.
- Ten localized interfaces and an About screen listing third-party repositories
  and licenses, and linking to the project's source
  repository.

## Build and Run

MinecraftC requires CMake 3.16 or newer and a C++17 compiler. CMake fetches the
pinned SDL 3.4.10 release by default. Add
`-DMINECRAFTC_FETCH_DEPENDENCIES=ON` to fetch GLM 1.0.1 as well, or use
`-DMINECRAFTC_USE_SYSTEM_SDL3=ON` for a compatible system SDL3.

### Linux

Install CMake, Git, GLM and Vulkan development packages, plus the X11
and Wayland development headers. Use the command for your distribution:

```bash
# Debian / Ubuntu (APT)
sudo apt install build-essential cmake git libglm-dev libvulkan-dev mesa-vulkan-drivers \
  xorg-dev libwayland-dev libxkbcommon-dev wayland-protocols \
  extra-cmake-modules pkg-config

# Fedora (DNF)
sudo dnf install gcc-c++ cmake git glm-devel vulkan-headers \
  vulkan-loader-devel libX11-devel libXcursor-devel libXi-devel libXrandr-devel \
  libXext-devel libXfixes-devel wayland-devel libxkbcommon-devel \
  wayland-protocols-devel extra-cmake-modules pkgconf-pkg-config

# Arch Linux (Pacman)
sudo pacman -S --needed base-devel cmake git glm vulkan-headers \
  vulkan-icd-loader libx11 libxcursor libxi libxrandr libxext libxfixes wayland \
  libxkbcommon wayland-protocols extra-cmake-modules pkgconf
```

Then build from the repository root:

```bash
cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release
cmake --build build-local -j2
./build-local/minecraftc
```

All supported platforms use Vulkan exclusively. `--renderer=vulkan` remains a
compatibility alias; Vulkan initialization failures are reported without a
fallback renderer.

### Windows

Install Visual Studio 2022 or newer with Desktop development with C++, CMake,
Git, and the LunarG Vulkan SDK 1.4.350.0 or newer. In PowerShell:

```powershell
cmake -S . -B build-local -DMINECRAFTC_FETCH_DEPENDENCIES=ON
cmake --build build-local --config Release --parallel 2
cmake --install build-local --config Release --prefix install-local
.\install-local\bin\minecraftc.exe
```

### macOS

macOS 11 or newer requires Xcode Command Line Tools, CMake, Git, GLM, and the
official MoltenVK 1.4.1 `MoltenVK-macos.tar` archive:

```bash
brew install cmake git glm
cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release \
  -DMINECRAFTC_MOLTENVK_ROOT=/path/to/MoltenVK
cmake --build build-local -j2
./build-local/minecraftc
```

### Android

Android builds target arm64 devices running Android 10 (API 29) or newer and
require Vulkan 1.0. The pinned toolchain uses SDK
Platform 35, Build Tools 35.0.0, NDK 28.2.13676358, CMake 3.22.1, and JDK 17.

```bash
gradle -p android assembleDebug
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

See [android/README.md](android/README.md) for release packaging and signing.

### iOS

iOS 14 or newer uses the Vulkan gameplay renderer exclusively through statically
linked MoltenVK 1.4.1. Building requires Xcode, CMake 3.28 or newer, and the
official `MoltenVK-all.tar` archive.

```bash
cmake -S . -B build-ios-simulator -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
  -DMINECRAFTC_FETCH_DEPENDENCIES=ON \
  -DMINECRAFTC_MOLTENVK_ROOT=/path/to/MoltenVK
cmake --build build-ios-simulator --config Release --parallel 2
```

See [ios/README.md](ios/README.md) for device builds, signing, and installation.

## Controls

| Action | Default input |
| --- | --- |
| Move / look | `WASD` / mouse |
| Jump / toggle Creative flight | `Space` / double-tap `Space` |
| Sneak, descend, or dive | `Shift` |
| Sprint | `Ctrl` |
| Attack or break / use or place | Left / right mouse button |
| Select hotbar slot | `1`-`9` or mouse wheel |
| Inventory / chat / direct command | `E` / `T` / `/` |
| Pick targeted block / swap offhand / drop | Middle mouse / `F` / `Q` (`Ctrl+Q` drops the stack) |
| Change perspective / toggle fullscreen | `F5` / `F11` |
| Complete command / previous completion | `Tab` / `Shift+Tab` |
| Pause or back | `Esc` |

Bindings are configurable under Settings > Controls. Controllers use an
Xbox-style layout by default. Native touch controls are available on Linux,
Windows, Android, and iOS and can be adjusted under Settings > Touch Controls.
Inventory screens support Java-style left/right stack handling, Shift-click
transfer, double-click gathering, left/right drag distribution, hovered-slot
`1`-`9`/`F` swaps, and `Q`/`Ctrl+Q` dropping. Creative inventory also supports
middle-click cloning and middle-button drag filling.

## Saves and Commands

| Platform | Default save directory |
| --- | --- |
| Windows | `%APPDATA%\MinecraftC\saves` |
| macOS | `~/Library/Application Support/MinecraftC/saves` |
| Linux | `$XDG_DATA_HOME/minecraftc/saves`, or `~/.local/share/minecraftc/saves` |
| Android | Application-private data directory |
| iOS | Application-private preference directory |

Desktop builds prefer a legacy `saves/` directory in the launch directory when
one exists. Save format v14 uses little-endian 16-bit block IDs and can read v2-v13 desktop
metadata. The current world generation version is v18 (Heaven v9). Generation v11 adds
mountain emerald ore, staffed plains/desert villages, seven villager
workstations, dynamic bed/workstation village claims, infection, spawn eggs,
and fixed five-level profession trading. Generation v12 makes both physical
village variants substantially more common while retaining their biome,
spacing, terrain-fit, and deterministic placement checks. Generation v13 seals
the wall-to-roof courses of village houses and traveler huts, keeps hut
decorations outside the wall, and closes the igloo's diagonal lower shell.
Only generation v18 worlds can load. All previous and future generation versions
remain on disk and are shown as incompatible; there is no automatic migration.
Generation v14 adds deterministic
Verdant Grotto, Dripstone Karst, Crystal Hollow, Volcanic Depths, and neutral
transition cave ecology with larger chambers and rifts. Generation v15 adds
24 collectable natural materials and plants across all 30 surface biomes,
including forest litter/ferns/mushrooms, dry vegetation, sediment, colored
rock layers, frozen ground/blue ice, volcanic ash and coral rock. Seed layout,
terrain heights and Heaven v8 remain unchanged. Generation v16 adds 28 more
natural blocks: mountain rocks, warm-climate soils, salt crust, biome vegetation,
cave moss, gypsum, quartz/amethyst, sulfur rock and a softly glowing cave fungus.
Small rubble heaps and crystal groups use deterministic world-coordinate anchors;
all new blocks support collection, placement and the Creative catalog.

Generation v17 retains the v16 Overworld output and expands Heaven to v9:
moonstone/skystone terrain, aether moss, glimmer silt, star crystal ore,
clustered flowers/ferns/berries, sealed shallow pools and hanging island vines.
Skyroot planks, three brick materials and star lamps provide exclusive building
choices; shards, berries and roasted foods support collection and crafting.
Five altitude layers, eight biomes and existing shrine transport remain.
Block storage, overrides and derived caches now preserve IDs above 255.

Generation v18 adds six Overworld structures: desert temples, jungle ruins,
swamp huts, mountain watchtowers, stone circles, and abandoned farmsteads.
All use existing materials and seeded layout variants, provide accessible
supplies, and support `/locate structure` (see [structure details](docs/project-features.md#overworld-structures-generation-v18)).
Terrain heights, cave-carving, seed layout 5, Heaven v9, save format v16 and
application VERSION are unchanged. New reservations may alter nearby trees
and structure spacing. v17 worlds remain listed but cannot load or migrate.

Each world saves its day/night duration, defaulting to 1200 seconds for a full
day and night. Set it with `/gamerule DayNightDuration 1200` (positive integer
seconds); older saves also default to 1200. This is no longer a client setting.

Worlds with cheats enabled support `/gamemode`, `/tp`, `/time`, `/weather`,
`/give <item_name> [1..64]`, `/gamerule <rule> [<value>]`, `/help gamerule [<rule>]`,
`/locate biome <biome>`, and `/locate structure <structure>`. Structure locate
supports all fourteen Overworld structures in the Overworld plus Heaven's
`xiguang_ruin`, `star_crystal_geode`, `cloudspire_tower`, and `skyway_shrine`.
Skyway Shrines link all five altitude bands, while the Heaven-only Starstep
Scepter provides a reusable 96-block safe-surface jump. Command arguments
support Tab/Shift+Tab completion; touch mode shows a virtual Tab while the
command input is open.
GameRule commands cover all 59 Java 1.21.11 rules plus `DayNightDuration`, accepting
new standard names and classic aliases. Rules persist per world in save v16 (introduced in v15); missing
or partial mechanics are stated in help and feedback. See [the rule reference](docs/game-rules.md).

Run `./build-local/minecraftc --version` to print the version without opening a
window.

## Development

```bash
ctest --test-dir build-local --output-on-failure
git diff --check
# seed originX originZ pixelSize blockStep outputPrefix
./build-local/terrain_preview 1234567890 -2048 -2048 512 8 terrain-preview
./build-local/terrain_benchmark 1592615476 9
```

For GI frame-time measurements and fixed-exposure visual comparisons, see
[GI benchmark and validation](docs/gi-performance.md).

Regenerate Vulkan shaders after editing their GLSL sources:

```bash
python3 tools/vulkan_shaders.py --root . --generate
python3 tools/vulkan_shaders.py --root . --check
```

Texture definitions live in `assets/textures/definitions/`. See
[ASSET_PIPELINE.md](ASSET_PIPELINE.md) for atlas generation and material
authoring. Entity model sources and regeneration instructions are documented in
[assets/models/entities/README.md](assets/models/entities/README.md).

## License

MinecraftC is licensed under the [GNU GPL v3.0 only](LICENSE), except for
third-party components and assets that retain their own licenses.

| Component or asset | License | Location |
| --- | --- | --- |
| SDL 3.4.10 | Zlib | CMake FetchContent build directory |
| GLM 1.0.1 | MIT | System package or CMake FetchContent build directory |
| MoltenVK 1.4.1 | Apache-2.0 | macOS bundles and `licenses/` |
| Vulkan Memory Allocator 3.3.0 | MIT | `external/VulkanMemoryAllocator/` |
| FastNoiseLite pinned snapshot | MIT | `external/FastNoiseLite/` |
| cgltf 1.15 | MIT | `external/cgltf/` |
| nlohmann/json 3.12.0 | MIT | `external/nlohmann/` |
| stb_image 2.30 | MIT or public domain (MIT used) | `external/stb/stb_image.h` |
| stb_truetype 1.26 | MIT | `external/stb/stb_truetype.h` |
| Noto Sans CJK SC Regular | SIL OFL 1.1 | `assets/fonts/noto/` |
| Noto Naskh Arabic Regular | SIL OFL 1.1 | `assets/fonts/noto/` |

Vendored dependency directories retain their upstream, checksum, and license
records in an `UPSTREAM.md` or dependency README; CMake records pins for fetched
dependencies. See [ASSET_SOURCES.md](ASSET_SOURCES.md) and
[assets/textures/LICENSE.md](assets/textures/LICENSE.md) for asset provenance.

### Crafted building materials

Craft stone bricks and polished granite/basalt/limestone/tuff from 2×2 base
materials (four outputs). Smelt cobblestone into stone, stone bricks into cracked
stone bricks, and sandstone into smooth sandstone. Combine stone bricks with
moss for mossy bricks; two vertical stone bricks make two chiseled bricks.
Four sand make sandstone; four sandstone make four cut sandstone.

Split clay into four clay balls, smelt them into bricks, then combine four bricks
into a brick block. Four clay balls also restore one clay block. Poppies,
dandelions, blue orchids and coal yield red, yellow, blue and black dye;
smelting cactus yields green dye. Combine white wool with dye to color it.
One bone makes three bone meal, which bleaches colored wool back to white.
Bone meal currently serves as a crafting material. All new items are available
in the creative catalog; for example, `/give stone_bricks 64` requires cheats.

### Game plugins

Official builtin plugins and user data/native packages can extend blocks, items, recipes,
interaction, environment colors and HUD. Enable them through the main menu’s Plugins page
and restart. See [plugin installation and SDK](docs/plugins.md) for platform support,
examples and world compatibility. `--safe-mode` starts with optional plugins disabled.
