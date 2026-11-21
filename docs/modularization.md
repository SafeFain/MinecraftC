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

Start with `pixel_readback_tests` and `model_animation_tests`. Move the
implicit Heaven simulation linkage toward explicit consumers. Record which
tests need normal simulation, which need the Heaven query variant, and which
use frozen or purpose-built fixtures. Make this distinction in the target
declarations and migrate in small groups.

The first batch is complete when a fresh build of just those two executables
passes their CTest entries, and their generated build graph has no path to
`minecraftc_simulation_test`, Heaven, SDL, or Vulkan. Verify representative
Heaven and core simulation tests use their intended variants, then run full
CTest. Preserve fixture definitions and registration of every existing test.

After configuring a fresh Release build directory, run the focused checks:

```bash
cmake -S . -B build-local/modularization-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build-local/modularization-tests --config Release \
  --target pixel_readback_tests model_animation_tests
ctest --test-dir build-local/modularization-tests -C Release --output-on-failure \
  -R '^(pixel_readback_negative_controls|model_animation_sampling_and_pose_math)$'
```

### 2. Make the application own UI backend creation

Pass the existing `IUIRenderBackend` into `UIRenderer` at startup and during
graphics recovery. Keep the Vulkan factory declaration with the adapter and
expose the backend contract at the layer shared by the two owners. Keep atlas
borrowing, localization rebinding, and destruction order explicit in both paths.

Complete this batch when `minecraftc_ui` has no path to
`minecraftc_render_vulkan`, the module guard rejects reintroducing the
dependency, and a real Vulkan smoke exercises UI startup and graphics
reinitialization with the relevant UI/readback regressions passing.

### 3. Enforce targeted header boundaries

Keep source ownership and layer checks in CTest. Extend the manifest and checker
with header ownership and allowed consumers for the boundaries touched in each
batch. Start with the UI backend and content mutation/publication headers.
Re-use the include scanner to measure propagation and check cycles. Exercise
the boundary checker with small fixtures that introduce a forbidden include,
an upward link, and duplicate production ownership.

Complete this batch when legal contract consumers pass, each injected violation
fails, and both existing API-boundary and module-boundary checks pass. Apply
ownership rules as headers acquire clear owners and account for platform and
test-only variants in their respective build configurations.

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
