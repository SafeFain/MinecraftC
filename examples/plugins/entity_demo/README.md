# Entity Demo

From the MinecraftC repository root:

```sh
cmake -S examples/plugins/entity_demo -B build-local/plugins/entity_demo
cmake --build build-local/plugins/entity_demo --config Release
```

Copy `mod.json`, the platform module and referenced package assets into
`<user-data>/mods/entity_demo/`. Enable in **Plugins** and restart.
Visual Studio normally places the module under `Release/`.
See [plugin development](../../../docs/plugin-development.md).

The example registers a circling mob with persistent JSON, a custom spawn egg
and an explicit builtin pig health override. Original project code under the project license.
