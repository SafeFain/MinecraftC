# Dimension demo

From the repository root:

```sh
cmake -S examples/plugins/dimension_demo -B build-local/plugins/dimension_demo
cmake --build build-local/plugins/dimension_demo --config Release
```

Copy the module and `mod.json` into `<user-data>/mods/dimension_demo/`,
enable in **Plugins** and restart. See [plugin development](../../../docs/plugin-development.md).
 The dimension uses a seeded plateau generator,
fixed clear weather, no natural mobs, and death/void returns to the Overworld.
With cheats enabled, enter using `/dimension dimension_demo:plateau`.
The dimensions SDK table also supports host-authoritative queued player travel.
