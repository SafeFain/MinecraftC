# Seeded spawn and structure V2 fixture

Native ABI 1 example, loaded by `plugin_seeded_spawn_and_structure_v2`.
Queries dimensions/structures extension version 2 while retaining their V1 tables.
The seed selects a spawn on Y=100 although the highest island is Y=240.
Accepted structures have two independently ordered instances per cell, signed
relative Y, AIR overwrites, and an exact zero loot seed.

Callbacks receive the already-derived dimension generation seed and actual
parameters. They must remain pure, concurrent and independent of load order.
This is an SDK regression fixture, not the official Heaven provider.

From a configured MinecraftC source build (with `BUILD_TESTING=ON`):

```sh
cmake --build build-local --config Release --target plugin_worldgen_policy_v2
ctest --test-dir build-local -C Release -R plugin_seeded_spawn_and_structure_v2 --output-on-failure
```

The integration test stages its own fixture manifest. This directory is not a
ready-to-install standalone package. For a first plugin, use the
[native demo](../native_demo/README.md) and [developer guide](../../../docs/plugin-development.md).
