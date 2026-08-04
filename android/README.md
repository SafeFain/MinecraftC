# Android build

Requirements: Android SDK 35, NDK 28.2.13676358, CMake 3.22.1, JDK 17, and
Gradle 8.9 or newer. The first configuration downloads the pinned SDL 3.4.10
source archive and verifies its SHA-256 checksum.

```bash
gradle -p android assembleDebug
gradle -p android assembleRelease
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

The release APK is intentionally unsigned. Sign it outside the repository with
the publisher's keystore before distribution.

GitHub Actions builds the same unsigned arm64 release APK on every workflow run.
Pushed `v*` tags publish it as
`MinecraftC-1.1.3-android-arm64-unsigned.apk` alongside the Linux, Windows, and
macOS packages.
