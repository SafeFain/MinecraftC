# Terrain Demo

From the MinecraftC repository root:

```sh
cmake -S examples/plugins/terrain_demo -B build-local/plugins/terrain_demo
cmake --build build-local/plugins/terrain_demo --config Release
```

Copy `mod.json`, the platform module and referenced package assets into
`<user-data>/mods/terrain_demo/`. Enable in **Plugins** and restart.
Visual Studio normally places the module under `Release/`.
See [plugin development](../../../docs/plugin-development.md).

Select Seeded Plateau in the create-world page. Parameters such as
`{ "base_height": 48 }` control the base height; enable markers for cross-boundary
wood pillars. Column and voxel callbacks share a pure seed/coordinate hash.
Original project code under the project license.
