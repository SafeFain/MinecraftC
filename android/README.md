# Android build

Requirements: Android SDK 35, NDK 28.2.13676358, CMake 3.22.1, JDK 17, and
Gradle 8.9 or newer. The first configuration downloads the pinned SDL 3.4.10
source archive and verifies its SHA-256 checksum.

The launcher icon shares the deterministic grass-block artwork used by iOS.
Regenerate it from the repository root with
`python3 tools/texture_generator.py --build-android-icon`.

Android builds use the Vulkan 1.0 gameplay renderer exclusively. Vulkan is a
required device feature, so devices without Vulkan support cannot install the
application and initialization failures do not use a fallback renderer.

```bash
gradle -p android assembleDebug
gradle -p android assembleRelease
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

The release APK is intentionally unsigned. Sign it outside the repository with
the publisher's keystore before distribution.

The APK includes `LICENSE` in its assets. MinecraftC is distributed under
GPL-3.0-only except for third-party components and assets that identify a
different license; their notices remain beside the corresponding source or
asset and are summarized in the root `README.md`.

## Versioning and releases

The root `VERSION` supplies Gradle's full `versionName`, including an optional
alpha/beta/rc iteration. For example, `1.4.0-beta.1` uses `versionName`
`1.4.0-beta.1` and `versionCode` `104002001`. Increasing the iteration advances
the installation code without changing the core `1.4.0`. See the root
[version rules](../README.md#versioning-and-releases) for channel ordering,
numbering limits and the shared mobile version-code formula.

GitHub Actions builds the same unsigned arm64 release APK on every workflow run.
Only a pushed tag exactly matching `v<VERSION>` publishes it as
`MinecraftC-<version>-android-arm64-unsigned.apk` alongside the other platform
packages, after all platform jobs pass. For example, `v1.4.0-beta.1` publishes
`MinecraftC-1.4.0-beta.1-android-arm64-unsigned.apk` as a GitHub prerelease.
Alpha/beta/rc are prereleases; `release` is a normal release. Branch pushes and
manual workflow runs build without publishing.
