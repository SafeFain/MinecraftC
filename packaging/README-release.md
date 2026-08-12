# MinecraftC Release Package

This is a portable MinecraftC build. Keep the executable and `assets` directory
together.

MinecraftC is licensed under GPL-3.0-only. The complete terms are in `LICENSE`;
third-party licenses are summarized in `README.md` and retained beside the
corresponding assets and source dependencies.

- Windows: run `minecraftc.exe` or `run-minecraftc.cmd`.
- Linux: run `./run-minecraftc.sh` from a terminal. A desktop entry and hicolor
  application icon are included under `share/`.
- macOS: open `MinecraftC.app` or run `./run-minecraftc.sh`. The application
  bundle includes the Apache-2.0-licensed MoltenVK runtime and defaults to Vulkan
  with automatic OpenGL fallback.
- Android: install `MinecraftC-<version>-android-arm64-unsigned.apk` after signing it
  with a trusted publisher or local developer key. It requires Android 10,
  arm64, and OpenGL ES 3.0.
- iOS: the simulator ZIP runs only in an arm64 iOS Simulator. The device
  unsigned IPA targets iOS 14+ arm64 and must be signed and provisioned before
  installation; it is Vulkan-only and statically links MoltenVK. Simulator
  binaries cannot be converted into device applications by renaming or signing
  them, even though both builds use arm64.
- Print build information without opening a window with `--version`.

All supported platforms default to Vulkan. Linux, Windows, macOS, and Android
fall back to OpenGL when Vulkan initialization is unavailable and accept
`--renderer=opengl`; iOS has no OpenGL backend or fallback.

Worlds and settings use the platform user-data directory documented in
`README.md`. If startup fails, inspect `minecraftc.log` in that directory.
Unsigned macOS downloads may require Control-clicking the executable and
choosing Open the first time.
The Android release APK is intentionally unsigned and cannot be installed until
it has been signed.
The iOS device IPA is intentionally unsigned and cannot be installed until it
has been signed with a compatible certificate and provisioning profile.
