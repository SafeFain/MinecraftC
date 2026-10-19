# Native plugin example

Registers a crystal item, actor-aware use cooldown, `/native_demo:gift`, HUD text
and an environment callback. It uses only the public SDK headers.

From the MinecraftC repository root:

```sh
cmake -S examples/plugins/native_demo -B build-local/plugins/native_demo
cmake --build build-local/plugins/native_demo --config Release
```

Copy `mod.json` and the resulting `native_demo.dll` / `native_demo.so` /
`native_demo.dylib` into `<user-data>/mods/native_demo/`. Visual Studio puts the
DLL under `Release/`; single-configuration builds normally use the build root.
Enable in **Plugins**, restart, create a disposable world with cheats, then run
`/native_demo:gift` and use the crystal. The command opts into guest use when the
multiplayer extension is available. Only the host executes gameplay callbacks.

See [plugin development](../../../docs/plugin-development.md),
[loader and manifest](../../../docs/plugins.md) and [source](plugin.cpp).
External native plugins require desktop Linux, Windows or macOS; mobile code
providers must be compiled into the application.
