# Render Demo

From the MinecraftC repository root:

```sh
cmake -S examples/plugins/render_demo -B build-local/plugins/render_demo
cmake --build build-local/plugins/render_demo --config Release
```

Copy `mod.json`, the platform module and referenced package assets into
`<user-data>/mods/render_demo/`. Enable in **Plugins** and restart.
Visual Studio normally places the module under `Release/`.
See [plugin development](../../../docs/plugin-development.md).

The SPIR-V binaries are generated from the included original GLSL using `glslc`.
The post pass replaces the default final color pass with a simple color grade.

It also uploads a procedural texture, updates a frame uniform buffer, renders to
a host-owned target and samples that target in a corner overlay. All GLSL,
SPIR-V and example code are original project assets under the project license.
