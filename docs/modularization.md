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

The save layer exposes separate contracts for world metadata, compatibility
reports, and chunk load data. Catalog and menu headers use the compatibility
and generation configuration data. Chunk streaming and persistence consume
the chunk records. `SaveStore` still owns encoding, I/O, and recovery.
The generation configuration and its fingerprint are independent of the
plugin extension registry.

| Include measurement | Before | After |
| --- | ---: | ---: |
| Production translation units that reach `SaveStore.h` | 40 | 18 |
| Project headers in the `WorldCatalog.h` closure | 24 | 3 |
| Project headers in the `WorldPersistence.h` closure | 31 | 24 |
| Project headers in the `ChunkStreamer.h` closure | 31 | 25 |
| Project headers in the `LodSubscriptions.h` closure | 24 | 2 |

`minecraftc_gameplay_rules` owns the item, block, inventory, recipe, and
container rules that the transaction implementation needs. In the Windows
Ninja build, `inventory_transaction_tests` went from 313 to 277 object nodes
and no longer pulls in simulation, terrain generation, or the thread pool.
The existing SDL asset dependency still supplies block definition reads.

`ReplicaLodSubscriptions` owns client-side revisions, pending requests,
and cancellation state. `GameSession` owns stream lifetime, dimension and
epoch validation, transport, and application of the accepted columns.
The LOD packet codec stays in the network layer. The focused subscription test
now builds one object, down from four, and covers host and replica behavior,
including backpressure, stale results, cancellation, and request budgets.
In matched Windows x64 MSVC/Ninja Release builds, a clean subscription test
build went from four compiler and four linker or archiver invocations to one
of each. Both no-op builds ran zero. Changing `LodTerrainSystem.h` went from
one compile and one link to zero. In the clean runs, Ninja recorded 85.34 s
versus 11.00 s summed across compiler invocations and 11.75 s versus 3.19 s
across link and archive invocations. The timings are single runs taken while
the host also built other targets.

Use the save round-trip and recovery tests, the authoritative inventory test,
and the LAN replica, authority, and LOD subscription tests to validate these
interfaces. Generation stays on version 23 with the original fingerprint
calculation, and the existing seed and region/singleton checks cover both
positive and negative coordinates.

## Compatibility and release gates

- Keep C++20, CMake 3.28 or newer, current mobile and desktop build paths, plugin ABI 1,
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

## C++20 named-module pilot

On native Windows x64, configure a separate MSVC/Ninja build with
`-DMINECRAFTC_EXPERIMENTAL_CXX_MODULES=ON` alongside the usual dependency
options. The pilot uses named interfaces for `WorldGenContext` and
`RuntimeClock` in selected production translation units. Their headers remain
the definition source and are available to the rest of the project. The
default build uses the headers on every platform.

Run CTest with `BUILD_TESTING=ON` to check linking and passing values between
module and header callers. For incremental validation, check that Ninja
records the original headers as dependencies of the module interface objects.
Compare clean, no-op and incremental builds in separate directories with the
same compiler, generator, parallelism and cache settings.

See [development and validation](development.md).
