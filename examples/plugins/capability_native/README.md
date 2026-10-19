# Native gameplay capability example

From the repository root, build with
`cmake -S examples/plugins/capability_native -B build-local/plugins/capability_native`,
then `cmake --build build-local/plugins/capability_native --config Release`. Copy the package and the platform module
into user-data `mods/capability_native/`; restart and use a fresh world. The root CMake
build also produces `plugin_capability_native` for integration testing.

This example registers an immutable pure terrain/bridge builder, keyed chest loot,
a read-only two-block blink target, a climbing vine, a sky callback, local particles,
a static held model and a dimension playlist. The blink staff wears only after success.
See [the capability guide](../../../docs/plugin-capabilities.md).

The original `staff.glb` and `chime.wav` are identical copies of the assets described in
[the data example](../capability_demo/README.md), licensed GPL-3.0-only with the repository.

See [plugin development](../../../docs/plugin-development.md) for platform output paths and lifecycle rules.
