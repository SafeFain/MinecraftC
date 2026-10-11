# Render Demo

Build with `cmake -S . -B /tmp/render_demo` and `cmake --build /tmp/render_demo`.
Copy the module and all non-source package files here into `<user-data>/mods/render_demo/`.
See [the plugin SDK documentation](../../../docs/plugins.md) for extension contracts.

The SPIR-V binaries are generated from the included original GLSL using `glslc`.
The post pass replaces the default final color pass with a simple color grade.

It also uploads a procedural texture, updates a frame uniform buffer, renders to
a host-owned target and samples that target in a corner overlay. All GLSL,
SPIR-V and example code are original project assets under the project license.
