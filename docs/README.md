# Documentation

Start with the [project overview](../README.md). Commands assume a source checkout
and the repository root unless stated otherwise. Packaged SDK/examples can be used
independently; source-code links require the matching checkout.

## Play and install

- [Build and run](building.md): desktop prerequisites, install/test commands and mobile entry points.
- [Controls](controls.md), [gameplay features](project-features.md), [saves and commands](saves-and-commands.md).
- [Game rules](game-rules.md): supported rules and implementation limits.
- [LAN multiplayer](lan-multiplayer.md): setup, authority, compatibility and dated platform evidence.
- [Android](../android/README.md), [iOS](../ios/README.md), [portable packages](../packaging/README-release.md).

## Develop plugins

- [Self-contained content packages](content-packages.md): create/build commands,
  one definition per file, translations, optional C++ and compiled-in delivery.
- [Install/manage plugins and loader contracts](plugins.md).
- [Plugin development](plugin-development.md): first data package, native build, lifecycle,
  extension versions, examples, testing and distribution.
- [Entity, terrain, render and block-shape APIs](plugin-extensions.md).
- [Behavior, structures/loot, actions and presentation APIs](plugin-capabilities.md).
- Public headers: [C ABI](../sdk/minecraftc/plugin.h), [extensions](../sdk/minecraftc/extensions.h),
  [capabilities](../sdk/minecraftc/capabilities.h), [C++ helpers](../sdk/minecraftc/plugin.hpp).

## Develop the application and assets

- [Development](development.md), [versioning/releases](releasing.md).
- [Asset pipeline](../ASSET_PIPELINE.md), [asset sources](../ASSET_SOURCES.md),
  [licenses](licenses.md), [texture provenance](../assets/textures/LICENSE.md).
- [glTF models](gltf-entity-model-engine.md), [animation](entity-animation-engine.md),
  [mob assets](../assets/models/entities/README.md), [player assets](../assets/models/player/README.md),
  [audio](../assets/audio/README.md), [font upstreams](../assets/fonts/noto/UPSTREAM.md).
- [GI measurement](gi-performance.md), [GI smoke](vulkan-gi-smoke.md),
  [cloud LOD smoke](vulkan-cloud-lod-smoke.md).
- [Task workflow](task-workflow.md) governs local development state. Agent context
  and task archives are local, ignored files rather than release documentation.

## Dated evidence and history

- [Heaven current implementation and remaining validation](heaven-plugin-remaining-work.md).
- [P1 acceptance record](heaven-p1-validation.md) retains its measured baseline and platform limits.
- [Original assessment](heaven-plugin-assessment.md) and [migration survey](history/heaven-migration-survey.md)
  are historical research, not current SDK limitations or outstanding implementation plans.
- [Original glTF integration](history/gltf-integration.md) retains its initial implementation/validation record.
- [MoltenVK package provenance](../packaging/MOLTENVK-UPSTREAM.md).

Historical test totals and host-specific tool availability describe their original
runs. Consult the implementation and run the relevant checks for a new change.
