# Development

Follow the architecture boundaries and checks below, together with the
[task workflow](task-workflow.md). See [building](building.md)
for platform prerequisites; run commands from the repository root.

## Source map

| Area | Entry points |
|---|---|
| Application/session | `src/app/`, `src/main.cpp` |
| Terrain, streaming, meshing | `src/world/`, `src/threading/` |
| Gameplay and persistence | `src/game/`, `src/player/`, `src/entity/` |
| Plugins and public SDK | `src/plugins/`, `sdk/minecraftc/`, [authoring guide](plugin-development.md) |
| Renderer | `src/renderer/`, `src/renderer/backend/vulkan/` |
| Platform adapters | `src/platform/sdl/`, `src/platform/native/` |
| UI and localization | `src/ui/`, `assets/lang/` |
| Assets and tools | [asset pipeline](../ASSET_PIPELINE.md), `tools/` |

CMake discovers new source files after reconfiguration. Test targets have explicit
source lists. Keep SDL, native OS and Vulkan APIs inside their existing adapters;
public gameplay/UI/render contracts stay backend-neutral. GPU resources belong
to the main/render thread. Preserve serialized IDs and generation determinism.

## Validation and tools

```bash
ctest --test-dir build-local --output-on-failure
git diff --check
# seed originX originZ pixelSize blockStep outputPrefix
./build-local/terrain_preview 1234567890 -2048 -2048 512 8 terrain-preview
./build-local/terrain_benchmark 1592615476 9
```

The default CTest suite runs CPU and LAN regressions; LAN tests require local
loopback/multicast socket access. Enable the optional Vulkan readback suite with:

```bash
cmake -S . -B build-local -DMINECRAFTC_VULKAN_TESTS=ON
cmake --build build-local -j2
ctest --test-dir build-local -L vulkan --output-on-failure
```

These tests check GI and model fog/UV sampling on a real Vulkan device. They use
an existing display, or Xvfb on headless Linux. Set `MINECRAFTC_TEST_XVFB=1` to
force Xvfb when the inherited display is unavailable. A missing display and
missing Xvfb produce an explicit skip; scene failures and Vulkan validation
errors fail the test. Installed Khronos validation layers are enabled on Linux.
The registered scenes run serially with bounded timeouts. Set
`-DMINECRAFTC_VULKAN_TESTS=OFF` to return to the default suite.

For GI frame-time measurements and fixed-exposure visual comparisons, see
[GI benchmark and validation](gi-performance.md).

Regenerate Vulkan shaders after editing their GLSL sources:

```bash
python3 tools/vulkan_shaders.py --root . --generate
python3 tools/vulkan_shaders.py --root . --check
```

Texture definitions live in `assets/textures/definitions/`. See
[ASSET_PIPELINE.md](../ASSET_PIPELINE.md) for atlas generation and material
authoring. Entity model sources and regeneration instructions are documented in
[assets/models/entities/README.md](../assets/models/entities/README.md).

Use `ctest --test-dir build-local -C Release --output-on-failure` with Visual Studio.
A normal code change requires affected builds and the complete CTest suite;
renderer/UI changes additionally require real Vulkan startup where a display is
available. Generation changes require seed variation, determinism, negative
coordinates and region/singleton equivalence. Report warnings and unrun checks.
Version and publishing rules live in [releasing.md](releasing.md).
