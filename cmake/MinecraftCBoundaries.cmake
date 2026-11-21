# Production targets may only depend on targets from earlier layers.
set(module_layers_0 minecraftc_sdk minecraftc_json minecraftc_engine_policy
    minecraftc_engine_link_options minecraftc_assets minecraftc_platform)
set(module_layers_1 minecraftc_log minecraftc_image_decoder minecraftc_model_animation
    minecraftc_content_registry minecraftc_plugin_runtime minecraftc_atlas_bindings)
set(module_layers_2 minecraftc_core minecraftc_threading minecraftc_animation_graph
    minecraftc_gltf minecraftc_terrain)
set(module_layers_3 minecraftc_simulation_common minecraftc_platform_sdl)
set(module_layers_4 minecraftc_simulation minecraftc_heaven minecraftc_network
    minecraftc_text minecraftc_render_data)
set(module_layers_5 minecraftc_plugins minecraftc_scene minecraftc_render_vulkan)
set(module_layers_6 minecraftc_ui)
set(module_layers_7 minecraftc_app_session)
set(module_layers_8 ${MINECRAFTC_APP_TARGET})

set(module_manifest "")
foreach(layer RANGE 0 8)
    foreach(target IN LISTS module_layers_${layer})
        set(module_layer_${target} ${layer})
    endforeach()
endforeach()
get_property(production_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS production_targets)
    foreach(property IN ITEMS SOURCES LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_property(module_${property} TARGET ${target} PROPERTY ${property})
    endforeach()
    string(APPEND module_manifest
        "${target}\t${module_layer_${target}}\t${module_SOURCES}\t${module_LINK_LIBRARIES};${module_INTERFACE_LINK_LIBRARIES}\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/minecraftc-modules.tsv" "${module_manifest}")

# Paths are repository-relative. A trailing slash permits a directory tree.
# Keep tests explicit even when they are disabled in this configuration.
set(header_manifest "")
function(minecraftc_header_boundary header owner)
    string(APPEND header_manifest "${header}\t${owner}\t${ARGN}\n")
    set(header_manifest "${header_manifest}" PARENT_SCOPE)
endfunction()

minecraftc_header_boundary(src/renderer/UIRenderBackend.h minecraftc_render_data
    src/ui/
    src/renderer/backend/vulkan/)
minecraftc_header_boundary(src/renderer/backend/vulkan/VulkanUIRenderer.h minecraftc_render_vulkan
    src/app/Application.cpp
    src/renderer/backend/vulkan/
    tests/NaturalTextureVulkanSmoke.h
    tests/VulkanLanSmoke.cpp
    tests/VulkanUiSmoke.cpp)
minecraftc_header_boundary(src/plugins/ContentMutation.h minecraftc_content_registry
    src/plugins/BuiltinPlugins.cpp
    src/plugins/ContentRegistry.cpp
    src/plugins/OfficialHeavenContent.h
    src/plugins/PluginCapabilities.cpp
    src/plugins/PluginExtensions.cpp
    src/plugins/PluginManager.cpp
    src/plugins/PluginRenderData.cpp
    src/plugins/official_heaven/HeavenDefinitions.cpp
    src/plugins/official_heaven/HeavenProvider.h
    tests/BlockShapesIntegration.h
    tests/DimensionIntegration.h
    tests/EntityModelRegistryTests.cpp
    tests/GenerationPolicyV2Integration.h
    tests/HeavenContentFixture.h
    tests/HeavenPolicyV2Reference.h
    tests/HeavenRemainingP0Integration.h
    tests/HeldToolVulkanSmoke.cpp
    tests/LocalizationTests.cpp
    tests/NetherGenerationIntegration.h
    tests/NetherSessionIntegration.h
    tests/PluginExtensionIntegration.h
    tests/PluginLanIntegration.h
    tests/PluginSessionIntegration.h
    tests/PluginTests.cpp
    tests/VulkanUiSmoke.cpp
    tests/WorldCatalogTests.cpp
    tests/WorldgenV2Integration.h)
minecraftc_header_boundary(src/plugins/AtlasPublication.h minecraftc_atlas_bindings
    src/plugins/AtlasBindings.cpp
    src/plugins/PluginManager.cpp
    src/renderer/BlockAtlasData.cpp
    tests/AssetDefinitionTests.cpp)
minecraftc_header_boundary(src/world/BlockTextureDefinitions.h minecraftc_simulation_common
    src/world/Block.cpp
    src/renderer/BlockAtlasData.cpp)
minecraftc_header_boundary(src/renderer/backend/vulkan/VulkanRendererInternal.h minecraftc_render_vulkan
    src/renderer/backend/vulkan/)
minecraftc_header_boundary(src/renderer/GameRenderer.h minecraftc_render_data
    src/app/
    src/entity/
    src/player/
    src/renderer/
    src/ui/
    tests/ApplicationFlowTests.cpp
    tests/EntityAiTestRenderer.h)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/minecraftc-module-headers.tsv" "${header_manifest}")
