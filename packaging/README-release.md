# MinecraftC Release 1.1.5

This is a portable MinecraftC build. Keep the executable and `assets` directory
together.

MinecraftC is licensed under GPL-3.0-only. The complete terms are in `LICENSE`;
third-party licenses are summarized in `README.md` and retained beside the
corresponding assets and source dependencies.

- Windows: run `minecraftc.exe` or `run-minecraftc.cmd`.
- Linux/macOS: run `./run-minecraftc.sh` from a terminal.
- Android: install `MinecraftC-1.1.5-android-arm64-unsigned.apk` after signing it
  with a trusted publisher or local developer key. It requires Android 10,
  arm64, and OpenGL ES 3.0.
- Print build information without opening a window with `--version`.

Worlds and settings use the platform user-data directory documented in
`README.md`. If startup fails, inspect `minecraftc.log` in that directory.
Unsigned macOS downloads may require Control-clicking the executable and
choosing Open the first time.
The Android release APK is intentionally unsigned and cannot be installed until
it has been signed.
