# Native block shape example

Build with `cmake -S . -B build && cmake --build build`. Copy `mod.json` and
`block_shapes_demo.so`/`.dll`/`.dylib` into the user-data
`mods/block_shapes_demo/` directory, then restart.

Registers a slab, stair, manually/villager-openable door, and arrow-sensitive
seven-tick button plus a fixed half-size box model through `minecraftc.block_shapes` version 1. Each family has
one creative inventory item and shared drops for all states. The slab merges
into builtin stone. Native loading is desktop-only; see the portable
[JSON package](../block_shapes_data/README.md) for mobile use.
