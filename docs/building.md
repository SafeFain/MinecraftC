# Build and run

MinecraftC requires CMake 3.16 or newer and a C++17 compiler. CMake fetches the
pinned SDL 3.4.10 release by default. Add
`-DMINECRAFTC_FETCH_DEPENDENCIES=ON` to fetch GLM 1.0.1 as well, or use
`-DMINECRAFTC_USE_SYSTEM_SDL3=ON` for a compatible system SDL3.

## Linux

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

## Windows

Install Visual Studio 2022 or newer with Desktop development with C++, CMake,
Git, and the LunarG Vulkan SDK 1.4.350.0 or newer for the target architecture.
Windows releases support x86-64 and native ARM64 (Windows 11 on Arm). For ARM64,
install the MSVC ARM64 build tools and the Windows ARM64 Vulkan SDK; its native
libraries and tools are under `Lib` and `Bin`. Both architectures require a
Vulkan-capable graphics driver. In PowerShell, build x64 with:

```powershell
cmake -S . -B build-local -A x64 -DMINECRAFTC_FETCH_DEPENDENCIES=ON
cmake --build build-local --config Release --parallel 2
cmake --install build-local --config Release --prefix install-local
.\install-local\bin\minecraftc.exe
```

On a Windows ARM64 host with the ARM64 SDK selected by `VULKAN_SDK`, use a
separate build directory:

```powershell
cmake -S . -B build-local-arm64 -A ARM64 -DMINECRAFTC_FETCH_DEPENDENCIES=ON
cmake --build build-local-arm64 --config Release --parallel 2
ctest --test-dir build-local-arm64 -C Release --output-on-failure
cmake --install build-local-arm64 --config Release --prefix install-local-arm64
.\install-local-arm64\bin\minecraftc.exe
```

## macOS

macOS 11 or newer requires Xcode Command Line Tools, CMake, Git, GLM, and the
official MoltenVK 1.4.1 `MoltenVK-macos.tar` archive:

```bash
brew install cmake git glm
cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release \
  -DMINECRAFTC_MOLTENVK_ROOT=/path/to/MoltenVK
cmake --build build-local -j2
./build-local/minecraftc
```

## Android

Android builds target arm64 devices running Android 10 (API 29) or newer and
require Vulkan 1.0. The pinned toolchain uses SDK
Platform 35, Build Tools 35.0.0, NDK 28.2.13676358, CMake 3.22.1, and JDK 17.

```bash
gradle -p android assembleDebug
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

See [android/README.md](../android/README.md) for release packaging and signing.

## iOS

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

See [ios/README.md](../ios/README.md) for device builds, signing, and installation.

## Install and test

Run commands from the repository root. Use `--config Release` / `-C Release` with
multi-configuration generators (Visual Studio or Xcode).

```sh
cmake --install build-local --prefix install-local
ctest --test-dir build-local --output-on-failure
```

For Visual Studio:

```powershell
cmake --install build-local --config Release --prefix install-local
ctest --test-dir build-local -C Release --output-on-failure
```

Build options and dependency pins are defined in [CMakeLists.txt](../CMakeLists.txt).
See [development](development.md) for optional Vulkan checks and
[release rules](releasing.md) for CI packaging.

## CI compilation cache

Linux, Windows, macOS and iOS CI use pinned sccache 0.19.0 with the GitHub Actions
cache backend. Compiler inputs and options determine object reuse; each platform
lane has its own cache namespace. Clean checkouts still configure, link, test and
validate their packages. The first run populates the cache; later runs report hits
and misses in the compiler cache statistics steps, before lengthy tests can let
the cache server idle out. Android retains its Gradle build.

Windows CI uses Ninja so CMake's C/C++ compiler launchers run. Before configuring
from PowerShell, `tools/ci_msvc_environment.ps1 -Architecture x64` (or `arm64` on a
native ARM64 host) selects MSVC and the SDK, requests English compiler output
for header dependency tracking, and exports environment changes to later CI steps.
Visual Studio generators remain supported for normal local builds.

macOS universal compilation enables sccache's per-architecture preprocessing,
whose cache-key correctness fixes require the pinned 0.19.0 release. iOS retains
Xcode and uses its `C_COMPILER_LAUNCHER` build setting; the cache server starts
before Xcode. Index storage, compiler modules and response files are disabled in
that CI lane as required by sccache's Xcode integration. The simulator and device
retain separate build directories and SDK flags, and both build with four workers.

Both Apple CI lanes set `CMAKE_DISABLE_PRECOMPILE_HEADERS=ON`. SDL's per-architecture
PCH include flags fail through sccache in a universal macOS build, and the SDL PCH
generated through Xcode's compiler launcher was rejected in the iOS simulator build in
[run 43606487330](https://github.com/SafeFain/MinecraftC/actions/runs/43606487330).
Ordinary translation units remain cached; simulator and device configurations use
the same policy. Apple cache namespaces start at v2 to exclude artifacts from the
failed PCH-enabled run. Other platforms and normal local builds retain their PCH settings.

The baseline [Actions run 43598121257](https://github.com/SafeFain/MinecraftC/actions/runs/43598121257)
spent 1195 seconds building Linux, 1162/1135 seconds building Windows x64/ARM64,
and 1488 seconds building macOS universal. iOS simulator configuration/build took
410 seconds; device configuration/build/packaging took 413 seconds. macOS resource
verification alone took 465 seconds: compressed tar resources are now hashed in
archive order to avoid repeated decompression when manifest order differs.
New runner timings must be measured after CI execution, including cold/warm cache
state; these historical durations are not promised speedups.

### iOS configuration snapshots and measurements

iOS keeps one macos-15 job, Xcode, four compiler workers, both iPhone/iPad Vulkan
first-frame checks and the unsigned arm64 device IPA. Before compilation,
`tools/ci_ios.py` saves separate simulator/device configuration trees through
Actions cache restore/save. These contain CMake probe results, generated project
files and pinned dependency sources, not final build/archive products. CMake
always runs again; a restored configuration failure discards only that lane's
workspace build tree and retries cold once. Missing caches use the ordinary path.

Keys include runner image, workspace/tool paths, Xcode/Clang/SDK/CMake versions,
configuration scripts and iOS/SDK inputs. There are no broader restore prefixes.
The helper and workflow are key inputs, so changing flags invalidates snapshots.
Configuration snapshots retain the absolute paths CMake expects; an image or path
change causes a miss. Simulator and device compiler results remain in sccache.
The daemon has no idle expiration and each build resets/reports its own counters.

Simulator smoke reuses available devices in the selected runtime, selected by
UDID within each family, and creates a device only when none matches. Each app
is uninstalled before installation. Devices run serially and are shut down on
exit; only newly created devices are deleted. Initialization, Vulkan first frame,
MoltenVK workaround and the 300-second first-frame timeout remain enforced.
Diagnostics are uploaded even after failures under `ios-ci-diagnostics`, outside
the release artifact pattern. Actions Summary reports step durations including
cache transfer, configuration, build, smoke preparation, checks and packaging.

Baseline [run 43723067838](https://github.com/SafeFain/MinecraftC/actions/runs/43723067838)
(2026-11-15) passed: iOS 1543s, simulator configuration 399s, build about 202s,
smoke 469s, device configuration 288s. Native measurements for this
change are pending: compare three cold and three warm runs of the same commit,
including cache transfer. The warm median target is at most 18 minutes; the cold
median must stay within 110% of an equivalent uncached baseline. Validate source
and resource edits, toolchain/configuration misses and both release artifacts.
These targets are not measured speedup claims. Windows portable helper tests do
not replace macOS/iOS builds or simulator execution.
