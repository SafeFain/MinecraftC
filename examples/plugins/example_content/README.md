# Data content example

A portable JSON package registering a blue-brick material, block and placeable
item. One cobblestone crafts four blue bricks. No library or build step is needed.

Copy this directory to `<user-data>/mods/example_content/` with `mod.json` at its
root. Enable in **Plugins**, restart and use a disposable test world. With cheats
enabled, `/give example_content:blue_brick 4` supplies the block. Verify placement,
breaking, the crafting recipe and save/reopen.

When renaming, update the directory, manifest ID and all `example_content:` keys
together. See [plugin development](../../../docs/plugin-development.md) and
[data schema](../../../docs/plugins.md#data-content).
