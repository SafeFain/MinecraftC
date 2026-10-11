# Terrain Demo

Build with `cmake -S . -B /tmp/terrain_demo` and `cmake --build /tmp/terrain_demo`.
Copy the module and all non-source package files here into `<user-data>/mods/terrain_demo/`.
See [the plugin SDK documentation](../../../docs/plugins.md) for extension contracts.

Select Seeded Plateau in the create-world page. Parameters such as
`{ "base_height": 48 }` control the base height; enable markers for cross-boundary
wood pillars. Column and voxel callbacks share a pure seed/coordinate hash.
Original project code under the project license.
