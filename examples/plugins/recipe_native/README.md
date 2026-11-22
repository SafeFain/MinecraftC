# Native Kiln Recipe Demo

Use `tools/content_package.py build` on this directory for a desktop module or
add its source root to `MINECRAFTC_CONTENT_PACKAGES` for a compiled-in example.
The C code uses only the public SDK. It registers one recipe before registering
the group it references so both definitions are resolved together.

Use this package in a separate test profile from the
[data example](../recipe_demo/README.md). Both use the same package ID and
gameplay definitions, with distinct native and data compatibility requirements.
