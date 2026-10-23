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
