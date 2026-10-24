# Package Demo

This self-contained package defines blue bricks, a crystal, a recipe and English/
Chinese names. The C++ source resolves those data definitions during load.
`/package_demo:gift` queues four bricks; using the crystal queues one brick.
Normal brick placement is unchanged.

From the repository root:

```sh
python3 tools/content_package.py build examples/plugins/package_demo --output build-local/package-demo-dist
```

Copy `build-local/package-demo-dist/package_demo/` into your user-data `mods/`.
Enable/restart, create a disposable world with cheats, then use
`/give package_demo:crystal 1` and `/package_demo:gift`. Verify placement, crystal
use, crafting and save/reopen. An existing output is intentionally not overwritten.

To compile the same package into the application, explicitly configure
`-DMINECRAFTC_CONTENT_PACKAGES=/absolute/path/to/examples/plugins/package_demo`.
Do not also install the external version: compiled-in IDs are reserved.
See [content packages](../../../docs/content-packages.md) for lifecycle, directory,
platform and distribution rules. The package's license and provenance are in LICENSE.md.
