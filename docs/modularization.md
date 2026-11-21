# MinecraftC modularization roadmap

The first CMake split landed in `058e204`. The next phase makes focused tests
build independently, moves backend creation to application composition, and
narrows the simulation interfaces that propagate rebuilds. Each batch should
build and pass its own checks before the next one begins.

## Baseline on 2026-11-21

- The Windows x64 Ninja production manifest classifies 30 CMake targets. The
  boundary guard verifies one production owner for each of 160 source files.
  The quoted-include scanner reports 706 cross-directory edges and zero
  file-level cycles across 401 files. Directory edges measure source coupling.
  The CMake manifest measures target linkage.
- `minecraftc_simulation_common` and `minecraftc_simulation` own 29 and 20
  sources respectively. Their consumers handle gameplay, terrain, saves,
  rendering data, and plugins. The include scanner reaches `Item.h` from
  93 translation units, `ContentQueries.h` from 65, `SaveStore.h` from 40,
  and `World.h` from 31. These are static propagation counts.
- `pixel_readback_tests` and `model_animation_tests` both link
  `minecraftc_simulation_test` in the generated Ninja graph. The blanket
  linkage in the test setup also propagates the Heaven query compile
  definition. `redstone_ore_tests` already links terrain independently.
- `minecraftc_ui` links `minecraftc_render_vulkan` because `UIRenderer`
  invokes `createVulkanUIBackend`. The UI already owns an
  `IUIRenderBackend`, and application startup and graphics recovery are the
  two production call sites that install it.

See [production targets](../cmake/MinecraftCTargets.cmake),
[test targets](../cmake/MinecraftCTests.cmake), and
[boundary checks](../tools/check_module_boundaries.py).

## Implementation order

### 1. Give focused tests explicit owners and dependencies

Test executables now declare their implementation libraries alongside their
sources. The Heaven query variant is limited to `worldgen_tests`,
`world_orchestration_tests`, `session_flow_tests`, and
`vulkan_decoration_smoke`. Tests exercising official Heaven content through
the content fixture link the provider separately. The cave test retains
its decorator-free compilation, and the frozen Heaven references stay in
their original test executables.

On Windows x64 Ninja, the Heaven query variant went from 58 executable
consumers to 4. The pixel readback test's build graph went from 340 object
nodes to 1, and the model animation test's went from 341 to 3. Both graphs
are free of simulation, Heaven, SDL, and Vulkan dependencies.

Run the isolation checks in a fresh Release directory:

```bash
cmake -S . -B build-local/modularization-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build-local/modularization-tests --config Release \
  --target pixel_readback_tests model_animation_tests
ctest --test-dir build-local/modularization-tests -C Release --output-on-failure \
  -R '^(pixel_readback_negative_controls|model_animation_sampling_and_pose_math)$'
```

With Ninja, inspect the closure for each executable with `ninja -t graph`.
Run full CTest from the complete desktop build to cover the normal simulation,
Heaven query and frozen-reference paths.

### 2. Make the application own UI backend creation

`Application` creates the Vulkan UI backend at startup and after a graphics
reset. `UIRenderer` owns the injected backend and binds the current localization
when it installs a replacement. Recovery releases the old backend before
rebuilding renderer resources, then passes the rebuilt block atlas to the new
backend as a borrowed texture. The contract lives in `renderer/UIRenderBackend.h`.

The module guard rejects direct and transitive paths from `minecraftc_ui` to
`minecraftc_render_vulkan` and keeps the factory header at the application
composition point and in the adapter. Inspect the generated closure with
`ninja -t graph minecraftc_ui` from a configured Ninja build. With
`MINECRAFTC_VULKAN_TESTS=ON`, `plugin_vulkan_heaven_off_readback` compares a
block icon and localized tooltip before and after rebuilding real Vulkan
resources. Hiding the icon and switching languages provide negative controls.

### 3. Enforce targeted header boundaries

Production targets list the owned headers in
[MinecraftCTargets.cmake](../cmake/MinecraftCTargets.cmake).
[MinecraftCBoundaries.cmake](../cmake/MinecraftCBoundaries.cmake) generates the
module and header manifests. The header manifest records each protected
header's owner and allowed direct includers. Add optional test consumers by
path, since the checker scans both production and test source trees in every
desktop test configuration.

| Header | Owner | Direct production includers | Production translation units reached |
| --- | --- | ---: | ---: |
| `renderer/UIRenderBackend.h` | `minecraftc_render_data` | 2 | 16 |
| `renderer/backend/vulkan/VulkanUIRenderer.h` | `minecraftc_render_vulkan` | 2 | 2 |
| `plugins/ContentMutation.h` | `minecraftc_content_registry` | 9 | 8 |
| `plugins/AtlasPublication.h` | `minecraftc_atlas_bindings` | 3 | 3 |

These counts held across the boundary change. The include graph remains
acyclic. Use `tools/check_module_dependencies.py --report --check-cycles` to
review propagation before extending the manifest.

`module_dependency_boundaries` checks the generated manifests against the
source tree. `module_boundary_guard_mutations` verifies legal consumers and
rejects forbidden includes, upward links, duplicate ownership, and a transitive
UI link to Vulkan. `api_dependency_boundaries` continues to enforce the
platform and graphics API rules.

### 4. Narrow simulation and session interfaces

Start by tracing the symbols used from `SaveStore.h` and moving data-only
dependencies onto a small existing or dedicated contract where that reduces the
build graph. The store continues to own encoding, I/O, and recovery. Next,
identify inventory rules or deterministic generation inputs that can be linked
by focused tests without the world runtime. Extract a target only when its
inputs, source owner, and link direction are clear.

Then trace LAN responsibilities across the `GameSessionLan*` implementations.
Separate replication state or translation where it can be exercised through a
focused test. Keep session lifecycle and gameplay authority in `GameSession`
and packet encoding in `minecraftc_network`.

Each extraction needs a smaller dependency closure for a named consumer and
passing focused tests. Use save round trips and recovery, generation seed and
region/singleton equivalence, or LAN authority and replication as applicable.
Check negative coordinates for generation work, and compare seeded output
against the existing generation version before accepting a generation split.

## Compatibility and release gates

- Keep C++20, CMake 3.16, current mobile and desktop build paths, plugin ABI 1,
  serialized IDs, and current save, LAN, and world-generation semantics.
  Keep SDL/native/Vulkan APIs confined to their adapters and GPU resource
  ownership on the render thread.
- For code batches, configure, build affected targets, run full desktop CTest,
  and run `git diff --check`. UI and renderer batches also need a real Vulkan
  startup and relevant readbacks. Check index-layer handoff and shader/vertex
  layout where affected. For target graph changes, use the existing platform
  matrix to validate Windows x64/ARM64, Linux, macOS, Android, and iOS before
  claiming cross-platform acceptance. Record unavailable checks by platform.
- Compare dependency graphs, compile counts, clean and no-op builds, and one
  representative header edit under the same compiler, generator, configuration,
  parallelism, and cache settings. Record compile and link time separately.
  The first split reduced Windows x64 cold build time from 2,094 s to 1,107 s,
  while the `ModelAnimation.h` incremental case went from 201 s to 374 s.
  Its final log reported MSVC /LTCG restarts. Repeat the measurement before
  attributing the difference or changing IPO policy.
- Treat C++20 named modules as a separate toolchain experiment after these
  boundaries settle. Any proposal needs its own CMake and platform support
  decision and an incremental-build comparison.

See [development and validation](development.md).
