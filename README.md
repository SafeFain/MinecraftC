# MinecraftC

A C++17 voxel sandbox built with SDL3 and Vulkan for Linux, Windows, macOS,
Android and iOS (MoltenVK on Apple platforms).

- Seeded infinite terrain, biomes, caves and structures, with asynchronous
  streaming, greedy meshing and independent distant-terrain LOD.
- Creative, Survival and Spectator modes; crafting, farming, fishing, combat,
  villagers, weather and persistent worlds.
- LAN cooperative play for 2–8 players, configurable controls and ten UI languages.
- Data and native plugins, including the default-enabled optional Heaven dimension.

## Quick start

Install CMake 3.16+, a C++17 compiler and Vulkan development/runtime support.
CMake fetches SDL 3.4.10; the option below also fetches GLM 1.0.1.
Linux needs X11/Wayland development headers. Apple builds need MoltenVK.
See [platform build instructions](docs/building.md) for prerequisites.

```sh
cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release -DMINECRAFTC_FETCH_DEPENDENCIES=ON
cmake --build build-local --config Release --parallel 2
```

Run `./build-local/minecraftc` on Linux/macOS. For Visual Studio builds, run
`.\build-local\Release\minecraftc.exe`. Installed builds find assets beside the
executable; see [installation](docs/building.md#install-and-test).
All platforms require Vulkan; startup does not fall back to another renderer.

Move with WASD, look with the mouse, jump with Space, open inventory with E,
and pause with Esc. Bindings are configurable; see [controls](docs/controls.md).

## Documentation

| Topic | Guide |
|---|---|
| Build and install | [Desktop and mobile setup](docs/building.md) |
| Gameplay | [Built-in Nether](docs/nether.md), [Features](docs/project-features.md), [commands and saves](docs/saves-and-commands.md), [game rules](docs/game-rules.md) |
| Multiplayer | [LAN setup and compatibility](docs/lan-multiplayer.md) |
| Plugins | [Install and manage](docs/plugins.md), [develop a plugin](docs/plugin-development.md) |
| Contributing | [Development and validation](docs/development.md), [asset authoring](ASSET_PIPELINE.md) |
| Releases | [Versioning and packaging](docs/releasing.md) |

[All documentation](docs/README.md)

## World compatibility

Opening a world requires save format **v22** and global generation **v21**
(native Nether **v1**, optional Heaven **v9**). Older/future versions remain listed as incompatible. Gameplay
plugin changes require review and explicit local recovery; a full backup is
retained by default, and entering without one permanently discards removed content.
LAN requires matching gameplay plugins. See [compatibility and recovery](docs/saves-and-commands.md).

## Versioning and releases

`VERSION` is the single application version source. Tags matching
`v<VERSION>` publish packages after all platform jobs pass; manual runs build
without publishing. Windows packages are available separately for x86-64 and
ARM64. [Full version rules](docs/releasing.md).

## License

[GPL-3.0-only](LICENSE), with separately licensed dependencies and assets.
See [third-party licenses](docs/licenses.md) and [asset provenance](ASSET_SOURCES.md).
