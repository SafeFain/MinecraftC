# Worldgen V2 native example

This C++17 plugin registers `worldgen_v2_demo:islands` and an optional
`worldgen_v2_demo:markers` modifier through `Host::worldgenV2()`.
It demonstrates five separated terrain layers, seed variation, a bulk volume
callback, fine-cell crystal silhouettes with surface replacement, and a
cross-boundary modifier with an independent LOD revision.

From a configured application source build with `BUILD_TESTING=ON`:

```sh
cmake --build build-local --config Release --target plugin_worldgen_v2_demo
ctest --test-dir build-local -C Release -R plugin_worldgen_v2_batch_and_lod --output-on-failure
```

CMake builds `plugin_worldgen_v2_demo` alongside the other native fixtures.
For a local package, copy the module into a mods/worldgen_v2_demo directory
and add `mod.json` with id `worldgen_v2_demo`, version `1.0.0`, api `1`, and a
native platform mapping to the module filename. Select generator key
`worldgen_v2_demo:islands`, revision `1`, parameters `{}` in an Overworld world;
optionally select modifier `worldgen_v2_demo:markers`, revision `1`.

The point callback and bulk callback describe identical exact blocks. LOD terrain
uses disjoint material spans; a separate decoration callback retains narrow
crystals in cells up to eight blocks wide. At size one, the resulting column
matches exact generation. The host applies the modifier and structures afterward.

This is an SDK demonstration, not the official Heaven provider. Native callback
contract and cache revision rules are documented in
[plugin extensions](../../../docs/plugin-extensions.md).
