# Native block shape example

From the MinecraftC repository root:

```sh
cmake -S examples/plugins/block_shapes_demo -B build-local/plugins/block_shapes_demo
cmake --build build-local/plugins/block_shapes_demo --config Release
```

Copy `mod.json`, the platform module and referenced package assets into
`<user-data>/mods/block_shapes_demo/`. Enable in **Plugins** and restart.
Visual Studio normally places the module under `Release/`.
See [plugin development](../../../docs/plugin-development.md).

Registers a slab, stair, manually/villager-openable door, and arrow-sensitive
seven-tick button plus a fixed half-size box model through `minecraftc.block_shapes` version 1. Each family has
one creative inventory item and shared drops for all states. The slab merges
into builtin stone. Native loading is desktop-only; see the portable
[JSON package](../block_shapes_data/README.md) for mobile use.
