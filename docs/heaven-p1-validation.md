# Heaven P1 implementation and validation

This is a dated acceptance record, not a current platform certification. See
[current status](heaven-plugin-remaining-work.md) for later compatibility changes.

Status: P1 implementation and Windows acceptance completed (2026-10-18); other native platforms/devices remain pending.
Baseline: `988fbd4fbacfaadeb8d8c07449566244e5c42f78`.
Local raw logs are retained under
`docs/tasks/evidence/heaven-remaining-p1/validation-20261018/`.

## Independent references

`tests/reference/heaven_v9/manifest.json` pins original source bytes. The compiled
frozen generator, Skyway policy, LOD, environment, particles and loot never call
the migrated provider. Existing V2 adapter tests remain useful contract tests and
are not the independent migration evidence.

Verified comparisons cover three seeds (123, 987654321, UINT64_MAX), negative
coordinates, spawn and four landmark neighborhoods, all five logical island slots,
full singleton voxels, reversed 3x3 region targets, reverse concurrent completion,
point columns and LOD sizes 1/2/4/8/16/32. The particle/environment fixture compares
101 environment samples and 450 frames per seed, emission/lifetime, phase, angular
motion, per-axis drift, shapes and old shader colors. It also compares generated
chest metadata/seeds and every inventory slot against original loot code.

The original reference exposed unsupported plants following structural AIR
writes and coarse LOD ordering of hanging vines versus center terrain. Fixes
preserve v9 output; expected values were not changed to accept migrated output.

## Performance

Same Windows machine, Release, no concurrent build; one warmup plus five rounds,
alternating reference/provider order. The workloads are 36 singleton columns,
four 3x3 regions, 1280 coarse LOD cells across five precisions, fresh-seed
construction plus the first chunk, and a nine-chunk CPU loading workload using
the same mesher and neighbor/light inputs for both implementations. Initial-chunk timing measures generation
startup, not total interactive world-loading time or GPU uploads.

Final memory-recording run (`p1-benchmark-final-acceptance.log`):

| Workload | Frozen median ms | Provider median ms | Ratio |
|---|---:|---:|---:|
| Singleton generation | 23.4905 | 23.8869 | 1.01687 |
| Region generation | 28.0453 | 29.2382 | 1.04253 |
| Coarse LOD | 10.1184 | 7.8648 | 0.77728 |
| Fresh first chunk | 0.7795 | 0.8433 | 1.08185 |
| Nine chunks + CPU meshes | 67.9042 | 67.0781 | 0.98783 |

Peak working set of the combined benchmark process: 19,210,240 bytes (18.32 MiB).
This includes both implementations and retained thread-local buffers, not the
full game's memory. The deterministic workload checksum is 5166219. Earlier
five-round runs also met the 10% gate. Initial
migration ratios exceeded the gate and were not accepted as complete.

Optimizations retain all callback validation and transaction boundaries: cull
non-intersecting declared footprints before building, validate namespaces once
per distinct block kind, transfer contiguous voxel rows, memoize pure columns,
reuse bounded unpublished storage, and publish fully validated staging without
revalidating/copying an intermediate buffer. Retained storage is bounded per
thread (four provider chunks, one scratch chunk, 1024 column slots and at most
nine generic staged chunks). Full coordinate/seed keys resolve cache collisions.

## Ownership and compatibility audit

| Location | Remaining responsibility |
|---|---|
| `src/plugins/official_heaven/` | Provider-owned v9 generation, structures, spawn, LOD, ambient and sky policy, package definitions |
| `OfficialHeavenContent.h`, `OfficialHeavenActions.h`, `HeavenSkywayPolicy.h`, Heaven registration in `BuiltinPlugins.cpp` | Compiled official plugin registration, semantics and pure action strategy |
| `BuiltinOwnership.h`, `ContentTypes.h`/`ContentQueries.h`/`ContentMutation.h`, `PluginManager.cpp`, `PluginExtensions.cpp` | Reserved serialized slots and trusted ownership checks; optional registries supply actual content |
| `Block.h`, `Item.h`, `Structure.h`, architectural table holes | Stable numeric IDs and historical enum/state layout |
| `SaveStore.*`, legacy field mappings in `GameSession.cpp`, `dimensionDirectory` | Existing v21 prefix/Heaven directory and legacy state mirrors, with keyed runtime dimension state |
| Generation/cache version constants in `WorldGenerator.h` | Historical v9/v20 cache identification |
| `MINECRAFTC_HEAVEN_TEST_QUERIES` declarations and guarded structure adapter | Test-only historical query surface; bodies reside in test adapters |
| `tests/reference/heaven_v9/` | Immutable original acceptance reference |

Production terrain/LOD/biome queries and ambient context use registered providers.
Skyway actions use logical generation layers and selected provider keys. The
presenter and shaders use registered parameters rather than Heaven selectors.
The sky uniform is 368 bytes; the post push constant remains 128 bytes. Shader
source/include fingerprints and checked-in SPIR-V have been verified.

Enabling Khronos validation exposed generic renderer contracts omitted by earlier
smokes: presentation semaphores now belong to swapchain images, sampled shadow
images receive an initial clear/layout transition even without casters, wire
push constants cover both shader stages, and MRT independent blending is
explicitly enabled when supported (base rendering remains available otherwise).
The repaired basic/textured/held/dropped/material smokes and official Heaven
day/night/grade/three-shape readback passed with validation; the final full suite
passed 84/84 after these repairs.

Heaven v9, global v20, save v21 and existing extension layouts remain unchanged.
An independent source-declaration audit preserved all 81 existing SDK structs
across plugin.h/extensions.h/capabilities.h. New capabilities use independently
queried versions. Official package revision
is `heaven-v9-content2-p1`; resources and behavior descriptions enter save/LAN
identity. Incompatible worlds remain listed and refused without migration.

## Native integration evidence

The final Windows Release configure/ALL_BUILD and explicit held/decor targets
passed. Full CTest passed **84/84**, including six real Vulkan validation-layer
readbacks, in **619.17 seconds** (`p1-final-complete-ctest.log`). No tests skipped.
The first full run failed15/84; all failures were repaired and the complete suite
was rerun. Earlier overlapping build/test execution caused a locked executable
link failure; the successful final build and test phases ran separately.

The complete suite covers native third-party dimension,
namespace rejection and late rollback across all registries, material sequences,
version negotiation, safe mode, Title Storage resource materialization with the
same desktop fingerprint, intact-cache timestamp preservation and damaged-cache
repair. Ten-language names/travel, English fallback, Arabic shaping, historical
aliases, disabled content and package glyph coverage passed. Recipe/import tools
passed 10/10 in their focused run and passed again inside the complete suite.

Final installed Windows basic/textured demos presented12 frames each. Actual
first/third-person Starstep, dropped models and16 Heaven materials over120 frames
passed geometry/index-layer/upload handoff checks. Khronos validation reported
no errors. Day/night, registered grading, all three ambient particle shapes,
ten-language UI, disabled-provider UI and resize/suspend/resume passed GPU
readbacks. Device: Intel Iris Plus Graphics.

Warnings: retained MSVC numeric-conversion, narrowing, shadowing and CRT warnings;
Vulkan validation emits unused fragment-output warnings for reduced attachment
sets. Negative package fixtures intentionally log rejected registrations. None
of these warnings is claimed fixed. Other GPUs and non-Windows native/device
runs remain unexecuted.

## Platform matrix

| Platform | Configuration/package checks | Actual execution |
|---|---|---|
| Windows x64 | Source inventory, directory/ZIP/TAR checker tests and final installed/actual ZIP313 resources passed | Release build,84/84 CTest and real Vulkan with validation passed |
| Linux x64 | Existing TAR packaging now checks the same resource manifest before upload | Native build/device pending |
| macOS universal | Existing app/TAR packaging checks the same resource manifest | Native build/MoltenVK/device pending |
| Android arm64 | Static provider; packaged Title Storage resources use private cache; APK manifest check added | NDK/SDK build and real device pending |
| iOS arm64 and simulator | Static provider; Title Storage private cache; IPA/simulator ZIP manifest checks added | Xcode build, simulator and real device pending |

Git attributes preserve official package and frozen snapshot bytes across
platform checkouts; final staging is checked against the accepted resource hashes.
CI changes add checks to existing packaging jobs; no new publishing step was added.
