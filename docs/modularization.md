# Modularization direction

The recommended first step is explicit CMake libraries with narrow interfaces.
C++20 named modules are a later, separate experiment. This document is advice;
the CI repair does not reorganize runtime code or adopt language features.

## Current constraints

- The project already isolates SDL, native APIs and Vulkan implementations and has
  application/session/presenter contracts. Keep these boundaries and their guards.
- Root CMake still discovers the application through a broad source glob, exposes
  the SDK through directory-wide includes and attaches assets/JSON/content libraries
  with directory-wide linkage. Large tests repeat implementation lists, including
  Block, Item, persistence, model and plugin code. Editing one header can therefore
  recompile the same implementation for many executables.
- Directory names are not yet independent library boundaries. World.cpp uses content
  queries, renderer contracts, survival and persistence; EntityManager uses survival,
  persistence and dimension rules. WorldGenerator also consults plugin content.
  Splitting each directory directly would preserve cycles rather than resolve them.

## Suggested sequence

1. **Measure and make dependencies explicit.** Record per-target source counts,
   repeated compilations, cold/warm build time and incremental time for a shared
   header change on representative CI runners. Introduce a shared INTERFACE build
   policy for C++20 and appropriate warnings; replace directory-wide include/link
   propagation with target-scoped dependencies. Keep version data from VERSION.
2. **Reuse compiled implementation before changing ownership.** Extract model
   loading/animation, platform-neutral utilities and small gameplay rules into
   STATIC targets. Make the application and relevant tests link the same targets.
   Preserve separate targets for intentionally different definitions, test stubs,
   platform options and frozen generation baselines; do not share objects merely
   because filenames match. Retain renderer/model/UI header API guards.
3. **Separate content contracts from registration.** Move the immutable registry
   view/types needed by generation, items and meshes behind a small content contract
   target. Keep loading, mutation, built-in provider registration and SDK bridging
   in the plugin runtime. Reuse ContentQueries and existing capability interfaces;
   preserve one registry owner, freeze ordering and ABI 1. Keep C SDK headers and
   plugin binaries independent of engine-internal C++ representation.
4. **Untangle simulation incrementally.** Initially keep coupled world/game/entity
   runtime in one simulation library. Extract deterministic generation and pure
   rules through read-only inputs; keep rendering resource/upload operations on the
   render thread and persistence/platform services behind existing contracts.
   GameSession remains the composition owner. Move UI, presentation and application
   composition into targets above simulation; avoid service locators and a new
   general event bus. Split the large CMake file only after targets own their sources.
5. **Enforce and measure each step.** Extend dependency guards to reject forbidden
   includes/links without weakening API boundaries. Each production source should
   compile once per intentional configuration/definition variant. Compare the same
   cold/warm/incremental workloads; retain full CTest, deterministic generation and
   Vulkan readbacks. Preserve save/network/generation versions and serialized IDs.

## C++20 named modules

CMake's named-module support starts at 3.28. Its current supported generator list
includes Ninja, Ninja Multi-Config and supported Visual Studio generators; Xcode
is not listed. This repository retains CMake 3.16, Android CMake 3.22.1 and an iOS
Xcode build, so a project-wide `export module` migration would also require a
separate toolchain/generator plan. Merely selecting C++20 does not satisfy that.
See the [official CMake module support matrix](https://cmake.org/cmake/help/latest/manual/cmake-cxxmodules.7.html)
and [3.28 release notes](https://cmake.org/cmake/help/latest/release/3.28.html).

After the library boundaries are stable, an optional private, platform-neutral
utility can be trialed in a dedicated supported-toolchain CI lane. Verify import
ordering, incremental rebuilds, package installation and cache behavior; keep BMI
artifacts scoped to the compiler/platform/configuration. Avoid an initial conversion
of plugin SDKs, third-party headers, persisted public types or `import std`.
Advance only if measured maintenance/build gains justify retaining support for all
five platform families. No module adoption is part of this repair.

## Remote CI incident behind this review

[Run 43901084120](https://github.com/SafeFain/MinecraftC/actions/runs/43901084120)
for e6c92cd failed before compilation on Windows x64: sccache's GHA storage returned
HTTP 503 ServerBusy with an egress-limit message. The iOS job never acquired a
hosted runner; GitHub reported macOS ARM64 capacity constraints. Linux, macOS,
Android and Windows ARM64 succeeded. These failures do not establish a C++20 code
regression. Optional cache initialization now falls back to local storage and then
a direct compiler; compiler and test failures remain fatal. Runner capacity still
requires a later retry/availability and is not corrected by gameplay source changes.

The local fallback clears both GHA settings: in the pinned
[sccache 0.19.0 configuration](https://github.com/mozilla/sccache/blob/v0.19.0/src/config.rs),
a nonempty SCCACHE_GHA_VERSION enables GHA storage even if SCCACHE_GHA_ENABLED is
false. Setting just the boolean would therefore repeat the original failure.
