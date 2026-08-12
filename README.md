# MinecraftC

MinecraftC is a C++17 voxel sandbox built with SDL3, Vulkan, and OpenGL. It
features deterministic infinite worlds, asynchronous chunk streaming, Creative,
Survival, and Spectator modes, dynamic lighting and weather, and English and
Simplified Chinese interfaces.

The project version is defined by the root `VERSION` file and shared by CMake,
Android Gradle, runtime output, and release CI.

## Highlights

- Deterministic terrain, biomes, caves, ores, vegetation, and trees across
  Y=-64..319.
- 20 biomes, seven vegetation and tree forms, fluids, farming, fire, TNT, and
  moving voxel clouds.
- Distance-prioritized generation, greedy meshing, ambient occlusion, dual-channel
  lighting, transparent materials, and persistent spawn caches.
- Crafting, furnaces, containers, combat, weather, commands, and persistent
  players, entities, and worlds.
- JSON-driven block, 126-item, and entity atlases with a deterministic 16x16
  texture pipeline.
- Keyboard and mouse, controller, and native multi-touch input.

## Build and Run

MinecraftC requires CMake 3.16 or newer and a C++17 compiler. CMake fetches the
pinned SDL 3.4.10 release by default. Add
`-DMINECRAFTC_FETCH_DEPENDENCIES=ON` to fetch GLM 1.0.1 as well, or use
`-DMINECRAFTC_USE_SYSTEM_SDL3=ON` for a compatible system SDL3.

### Linux

Install CMake, Git, GLM, OpenGL and Vulkan development packages, plus the X11
and Wayland development headers. On Debian or Ubuntu:

```bash
sudo apt install build-essential cmake git libglm-dev libgl1-mesa-dev libvulkan-dev \
  xorg-dev libwayland-dev libxkbcommon-dev wayland-protocols \
  extra-cmake-modules pkg-config

cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release
cmake --build build-local -j2
./build-local/minecraftc
```

Linux, Windows, Android, and macOS include both gameplay renderers. Vulkan is
the default and falls back to OpenGL when initialization fails. Select OpenGL
explicitly with `--renderer=opengl`.

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
include Vulkan 1.0 with an OpenGL ES 3.0 fallback. The pinned toolchain uses SDK
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
  -DMINECRAFTC_ENABLE_OPENGL=OFF \
  -DMINECRAFTC_ENABLE_VULKAN=ON \
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
| Inventory / command | `E` / `T` |
| Pause or back | `Esc` |

Bindings are configurable under Settings > Controls. Controllers use an
Xbox-style layout by default. Native touch controls are available on Linux,
Windows, and Android and can be adjusted under Settings > Touch Controls.

## Saves and Commands

| Platform | Default save directory |
| --- | --- |
| Windows | `%APPDATA%\MinecraftC\saves` |
| macOS | `~/Library/Application Support/MinecraftC/saves` |
| Linux | `$XDG_DATA_HOME/minecraftc/saves`, or `~/.local/share/minecraftc/saves` |
| Android | Application-private data directory |

Desktop builds prefer a legacy `saves/` directory in the launch directory when
one exists. Save format v8 can read v2-v7 desktop saves. The current world
generation version is v5.

Worlds with cheats enabled support `/gamemode`, `/tp`, `/time`, and `/weather`.
Run `./build-local/minecraftc --version` to print the version without opening a
window.

## Development

```bash
ctest --test-dir build-local --output-on-failure
git diff --check
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
| MoltenVK 1.4.1 | Apache-2.0 | macOS bundles and `licenses/` |
| Vulkan Memory Allocator 3.3.0 | MIT | `external/VulkanMemoryAllocator/` |
| cgltf 1.15 | MIT | `external/cgltf/` |
| nlohmann/json 3.12.0 | MIT | `external/nlohmann/` |
| stb_truetype 1.26 | MIT | `external/stb/stb_truetype.h` |
| Noto Sans CJK SC Regular | SIL OFL 1.1 | `assets/fonts/noto/` |

Upstream sources and checksums are recorded in each dependency's `UPSTREAM.md`.
See [ASSET_SOURCES.md](ASSET_SOURCES.md) and
[assets/textures/LICENSE.md](assets/textures/LICENSE.md) for asset provenance.
