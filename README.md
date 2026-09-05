# MinecraftC

MinecraftC is a C++17 voxel sandbox built with SDL3 and Vulkan. It
features deterministic infinite worlds, asynchronous chunk streaming, Creative,
Survival, and Spectator modes, dynamic lighting and weather, and English and
nine other localized interfaces.

The root `VERSION` file is the single version source. It uses
`X.Y.Z-alpha|beta|rc|release` and drives CMake, Android/iOS metadata, runtime
output, package names, and release CI. A release tag must exactly equal
`v<VERSION>` (for example `v1.2.2-release`). Alpha, beta, and RC tags create
GitHub prereleases; release-channel tags create normal releases.

## Highlights

- Deterministic terrain, biomes, caves, ores, vegetation, and trees across
  Y=-64..319.
- 30 biomes, 15 blended macro terrain archetypes, drainage-basin rivers, seven
  vegetation and tree forms, fluids, farming, fire, TNT, and moving voxel clouds.
- Distance-prioritized generation, greedy meshing, ambient occlusion, dual-channel
  lighting, configurable cascaded shadows, transparent materials, and persistent
  spawn caches.
- Distant-terrain LOD outside the full chunk radius, with 32-4096 chunk distance,
  four load presets, four precision presets, and progressively refined caches.
- Crafting, furnaces, containers, Java 1.9-style charged melee combat, hunger,
  fast regeneration, armor/shields, weather, commands, and persistent
  players, entities, and worlds.
- JSON-driven block, 174-item, and entity atlases with a deterministic 16x16
  texture pipeline.
- Keyboard and mouse, controller, and native multi-touch input.
- Ten localized interfaces and an About screen linking to the project's source
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
one exists. Save format v12 can read v2-v11 desktop saves. The current world
generation version is v13 (with Heaven structures at v7). Generation v11 adds
mountain emerald ore, staffed plains/desert villages, seven villager
workstations, dynamic bed/workstation village claims, infection, spawn eggs,
and fixed five-level profession trading. Generation v12 makes both physical
village variants substantially more common while retaining their biome,
spacing, terrain-fit, and deterministic placement checks. Generation v13 seals
the wall-to-roof courses of village houses and traveler huts, keeps hut
decorations outside the wall, and closes the igloo's diagonal lower shell.
Older generation versions remain on disk and are shown as incompatible rather
than migrated or blended into v13 terrain.

Worlds with cheats enabled support `/gamemode`, `/tp`, `/time`, `/weather`,
`/locate biome <biome>`, and `/locate structure <structure>`. Structure locate
supports the current dimension's Overworld structures plus Heaven's
`xiguang_ruin`, `star_crystal_geode`, and `cloudspire_tower`. Command arguments
support Tab/Shift+Tab completion; touch mode shows a virtual Tab while the
command input is open.
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
