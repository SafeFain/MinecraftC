add_library(minecraftc_sdk INTERFACE)
target_include_directories(minecraftc_sdk INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/sdk")

# Explicit source-package roots only; never compile code discovered in user mods.
set(MINECRAFTC_CONTENT_PACKAGES "" CACHE STRING "Semicolon-separated built-in content package source roots")
include("${CMAKE_CURRENT_SOURCE_DIR}/sdk/cmake/ContentPackage.cmake")
foreach(package_root IN LISTS MINECRAFTC_CONTENT_PACKAGES)
    string(SHA256 package_target "${package_root}")
    string(SUBSTRING "${package_target}" 0 12 package_target)
    minecraftc_add_content_package(content_${package_target} ROOT "${package_root}" MODE BUILTIN)
endforeach()
get_property(content_declarations GLOBAL PROPERTY MINECRAFTC_CONTENT_DECLARATIONS)
get_property(content_entries GLOBAL PROPERTY MINECRAFTC_CONTENT_ENTRIES)
get_property(content_libraries GLOBAL PROPERTY MINECRAFTC_CONTENT_LIBRARIES)
get_property(content_roots GLOBAL PROPERTY MINECRAFTC_CONTENT_ROOTS)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ContentPackages.inc" CONTENT
    "${content_declarations}\ninline void appendContentPackages(std::vector<Plugins::BuiltinPluginDescriptor>& entries) {\n(void)entries;\n${content_entries}}\n")
set_property(SOURCE src/plugins/BuiltinPlugins.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    MINECRAFTC_CONTENT_PACKAGES_HEADER=\"${CMAKE_CURRENT_BINARY_DIR}/ContentPackages.inc\")

# Restrict the first-party engine's compile options to its source-owning targets.
add_library(minecraftc_engine_policy INTERFACE)
target_compile_features(minecraftc_engine_policy INTERFACE cxx_std_20)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(minecraftc_engine_policy INTERFACE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(minecraftc_engine_policy INTERFACE /W4 /permissive- /utf-8)
endif()

add_library(minecraftc_assets STATIC
    src/core/AssetStore.cpp
    src/platform/sdl/SdlAssetStore.cpp)
target_include_directories(minecraftc_assets PUBLIC src)
target_link_libraries(minecraftc_assets PRIVATE ${MINECRAFTC_SDL_TARGET})

# Header-only JSON is shared by persistence, network and plugin validation.
add_library(minecraftc_json INTERFACE)
target_include_directories(minecraftc_json INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/external")

set(MINECRAFTC_SDL_BACKEND_SOURCES
    src/platform/sdl/SdlAudioSystem.cpp
    src/platform/sdl/SdlClipboard.cpp
    src/platform/sdl/SdlGamepadManager.cpp
    src/platform/sdl/SdlWindow.cpp)
set(MINECRAFTC_VULKAN_BACKEND_SOURCES
    src/renderer/backend/vulkan/VulkanDiagnostics.cpp
    src/renderer/backend/vulkan/VulkanHelpers.cpp
    src/renderer/backend/vulkan/VulkanModelBackend.cpp
    src/renderer/backend/vulkan/VulkanPipelineFactory.cpp
    src/renderer/backend/vulkan/VulkanPluginBackend.cpp
    src/renderer/backend/vulkan/VulkanRenderer.cpp
    src/renderer/backend/vulkan/VulkanResources.cpp
    src/renderer/backend/vulkan/VulkanUIRenderer.cpp
    src/renderer/backend/vulkan/VulkanUIRenderer.h
    src/renderer/backend/vulkan/VulkanRendererInternal.h
    src/renderer/backend/vulkan/VmaImplementation.cpp)
add_library(minecraftc_platform STATIC
    src/platform/native/NativePlatform.cpp
    src/platform/native/NativeDynamicLibrary.cpp
    src/platform/native/NativeNetworkSocket.cpp
    src/platform/native/NativeLanDiscovery.cpp
    src/platform/native/NativeLanDiscoveryBonjour.cpp
    src/platform/native/NativeLanDiscoveryAndroid.cpp
    src/platform/native/NativeCrashHandler.cpp
    src/platform/sdl/SdlPlatformPaths.cpp)
target_include_directories(minecraftc_platform PUBLIC src)
target_include_directories(minecraftc_platform SYSTEM PRIVATE external/mdns)
target_link_libraries(minecraftc_platform PRIVATE ${CMAKE_DL_LIBS})
target_compile_definitions(minecraftc_platform PRIVATE
    MINECRAFTC_INSTALL_DATADIR="${CMAKE_INSTALL_DATADIR}")
target_link_libraries(minecraftc_platform PRIVATE ${MINECRAFTC_SDL_TARGET})
# Apple provides Bonjour/DNS-SD through the implicitly linked libSystem;
# macOS and iOS SDKs do not provide a separate libdns_sd to link.
if(WIN32)
    target_link_libraries(minecraftc_platform PUBLIC shell32 ole32 ws2_32 iphlpapi)
endif()

# The application and tests link the same implementations. Preserve Release
# IPO and optional Debug sanitizer instrumentation for the extracted sources.
include(CheckIPOSupported)
check_ipo_supported(RESULT MINECRAFTC_IPO_SUPPORTED OUTPUT MINECRAFTC_IPO_ERROR)
find_package(Threads REQUIRED)

add_library(minecraftc_engine_link_options INTERFACE)
if(MINECRAFTC_IPO_SUPPORTED AND NOT MSVC)
    # Consumers can link IPO objects without compiling all of their sources
    # with IPO. Match the options from CMake's compiler module.
    target_link_options(minecraftc_engine_link_options INTERFACE
        "$<$<CONFIG:Release>:${CMAKE_CXX_COMPILE_OPTIONS_IPO};${CMAKE_CXX_LINK_OPTIONS_IPO}>")
endif()
if(MINECRAFTC_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(minecraftc_engine_link_options INTERFACE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>)
endif()

function(minecraftc_add_engine_library target)
    add_library(${target} STATIC ${ARGN})
    target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(${target}
        PRIVATE minecraftc_engine_policy
        PUBLIC minecraftc_engine_link_options)
    if(MINECRAFTC_IPO_SUPPORTED)
        set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
    if(MINECRAFTC_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            "$<$<CONFIG:Debug>:-fsanitize=address,undefined;-g;-O0>")
    endif()
    if(IOS)
        set_property(TARGET ${target} PROPERTY XCODE_ATTRIBUTE_SKIP_INSTALL YES)
    endif()
endfunction()

minecraftc_add_engine_library(minecraftc_log
    src/debug/Log.cpp)
minecraftc_add_engine_library(minecraftc_core
    src/core/Input.cpp
    src/core/InputCodes.cpp
    src/core/TextEditBuffer.cpp
    src/core/Utf8.cpp)
minecraftc_add_engine_library(minecraftc_threading
    src/threading/ThreadPool.cpp)
minecraftc_add_engine_library(minecraftc_image_decoder
    src/renderer/StbImageImpl.cpp)
minecraftc_add_engine_library(minecraftc_model_animation
    src/model/ModelAnimation.cpp)
minecraftc_add_engine_library(minecraftc_animation_graph
    src/model/AnimationGraph.cpp
    src/model/AnimationMixer.cpp)
minecraftc_add_engine_library(minecraftc_gltf
    src/model/GltfLoader.cpp)
minecraftc_add_engine_library(minecraftc_content_registry
    src/plugins/ContentRegistry.cpp
    src/plugins/ContentMutation.h)
minecraftc_add_engine_library(minecraftc_plugin_runtime
    src/plugins/RuntimeFault.cpp)
minecraftc_add_engine_library(minecraftc_atlas_bindings
    src/plugins/AtlasBindings.cpp
    src/plugins/AtlasPublication.h)
target_link_libraries(minecraftc_content_registry PUBLIC minecraftc_sdk glm::glm)
target_link_libraries(minecraftc_plugin_runtime PUBLIC minecraftc_sdk)
target_link_libraries(minecraftc_log PRIVATE minecraftc_platform)
target_link_libraries(minecraftc_core PRIVATE minecraftc_log)
target_link_libraries(minecraftc_threading PRIVATE minecraftc_log Threads::Threads)
target_include_directories(minecraftc_image_decoder PUBLIC external/stb)
target_link_libraries(minecraftc_model_animation PUBLIC glm::glm)
target_link_libraries(minecraftc_animation_graph
    PUBLIC minecraftc_model_animation
    PRIVATE minecraftc_assets minecraftc_json)
target_include_directories(minecraftc_gltf PRIVATE external/cgltf)
target_link_libraries(minecraftc_gltf
    PUBLIC minecraftc_model_animation
    PRIVATE minecraftc_assets minecraftc_image_decoder)

set(MINECRAFTC_TERRAIN_SOURCES
    src/world/BiomeMap.cpp
    src/world/HeightPipeline.cpp
    src/world/Noise.cpp
    src/world/OreGenerator.cpp)
minecraftc_add_engine_library(minecraftc_terrain ${MINECRAFTC_TERRAIN_SOURCES})
target_include_directories(minecraftc_terrain SYSTEM PUBLIC external/FastNoiseLite)
target_link_libraries(minecraftc_terrain PUBLIC glm::glm)

minecraftc_add_engine_library(minecraftc_gameplay_rules
    src/game/InventoryInteraction.cpp
    src/game/InventoryModel.cpp
    src/game/InventoryTransaction.cpp
    src/game/Item.cpp
    src/game/NetherProgression.cpp
    src/game/SurvivalRules.cpp
    src/world/Block.cpp
    src/world/BlockTextureDefinitions.h
    src/world/BlockEntityLogic.cpp)
target_link_libraries(minecraftc_gameplay_rules
    PUBLIC minecraftc_sdk glm::glm
    PRIVATE minecraftc_assets minecraftc_atlas_bindings minecraftc_content_registry)

# Both simulation configurations reuse these sources, which are independent
# of the Heaven-only World and WorldGenerator query declarations.
set(MINECRAFTC_SIMULATION_COMMON_SOURCES
    src/entity/EntityPresentation.cpp
    src/entity/EntityReplication.cpp
    src/entity/GroundNavigation.cpp
    src/game/FishingSystem.cpp
    src/game/SaveStore.cpp
    src/game/SurvivalBlockLogic.cpp
    src/game/SurvivalSession.cpp
    src/game/SurvivalStats.cpp
    src/game/VillagerTrade.cpp
    src/game/Weather.cpp
    src/game/WorldCatalog.cpp
    src/world/CaveGenerator.cpp
    src/world/Chunk.cpp
    src/world/ChunkMesh.cpp
    src/world/ChunkStore.cpp
    src/world/NetherGenerator.cpp
    src/world/NetherPortal.cpp
    src/world/NetherStructures.cpp
    src/world/RegionGenerator.cpp
    src/world/TreeGenerator.cpp
    src/world/WorldPersistence.cpp)
set(MINECRAFTC_SIMULATION_WORLD_SOURCES
    src/entity/EntityBehavior.cpp
    src/entity/EntityExtensions.cpp
    src/entity/EntityManager.cpp
    src/entity/EntityNavigation.cpp
    src/entity/EntityVillageAi.cpp
    src/player/Player.cpp
    src/world/BuiltinPortalGeneration.cpp
    src/world/ChunkMeshPipeline.cpp
    src/world/ChunkStreamer.cpp
    src/world/FluidScheduler.cpp
    src/world/LodTerrainSystem.cpp
    src/world/NetherPortalWorld.cpp
    src/world/PluginGeneration.cpp
    src/world/RedstoneWorld.cpp
    src/world/StructureGenerator.cpp
    src/world/World.cpp
    src/world/WorldGenerator.cpp
    src/world/WorldLighting.cpp
    src/world/WorldReplication.cpp
    src/world/WorldSimulation.cpp)

function(minecraftc_link_simulation_dependencies target)
    target_link_libraries(${target}
        PUBLIC minecraftc_terrain minecraftc_sdk glm::glm
        PRIVATE minecraftc_assets minecraftc_atlas_bindings minecraftc_content_registry
                minecraftc_json minecraftc_log minecraftc_platform
                minecraftc_plugin_runtime minecraftc_threading)
endfunction()
minecraftc_add_engine_library(minecraftc_simulation_common ${MINECRAFTC_SIMULATION_COMMON_SOURCES})
minecraftc_link_simulation_dependencies(minecraftc_simulation_common)
target_link_libraries(minecraftc_simulation_common PUBLIC minecraftc_gameplay_rules)
minecraftc_add_engine_library(minecraftc_simulation
    ${MINECRAFTC_SIMULATION_WORLD_SOURCES})
minecraftc_link_simulation_dependencies(minecraftc_simulation)
target_link_libraries(minecraftc_simulation PUBLIC minecraftc_simulation_common)

set(MINECRAFTC_HEAVEN_SOURCES
    src/plugins/official_heaven/HeavenAmbient.cpp
    src/plugins/official_heaven/HeavenDefinitions.cpp
    src/plugins/official_heaven/HeavenEnvironment.cpp
    src/plugins/official_heaven/HeavenGenerator.cpp
    src/plugins/official_heaven/HeavenProvider.cpp)
minecraftc_add_engine_library(minecraftc_heaven ${MINECRAFTC_HEAVEN_SOURCES})
target_link_libraries(minecraftc_heaven PRIVATE
    minecraftc_content_registry minecraftc_json minecraftc_simulation_common)

set(MINECRAFTC_PLUGIN_SOURCES
    src/plugins/BuiltinPlugins.cpp
    src/plugins/PluginCapabilities.cpp
    src/plugins/PluginCapabilityData.cpp
    src/plugins/PluginExtensions.cpp
    src/plugins/PluginManager.cpp
    src/plugins/PluginRenderData.cpp)
minecraftc_add_engine_library(minecraftc_plugins ${MINECRAFTC_PLUGIN_SOURCES})
target_link_libraries(minecraftc_plugins
    PUBLIC minecraftc_sdk glm::glm
    PRIVATE minecraftc_assets minecraftc_atlas_bindings minecraftc_content_registry
            minecraftc_gltf minecraftc_heaven minecraftc_image_decoder minecraftc_json
            minecraftc_log minecraftc_platform minecraftc_plugin_runtime
            minecraftc_simulation_common ${content_libraries})

set(MINECRAFTC_NETWORK_SOURCES
    src/network/ChunkProtocol.cpp
    src/network/Connection.cpp
    src/network/EntityProtocol.cpp
    src/network/GameplayProtocol.cpp
    src/network/PlayerProfile.cpp
    src/network/Protocol.cpp
    src/network/Session.cpp
    src/network/StateProtocol.cpp)
minecraftc_add_engine_library(minecraftc_network ${MINECRAFTC_NETWORK_SOURCES})
target_link_libraries(minecraftc_network
    PUBLIC minecraftc_sdk glm::glm
    PRIVATE minecraftc_content_registry minecraftc_json minecraftc_log
            minecraftc_platform minecraftc_simulation_common)

set(MINECRAFTC_TEXT_SOURCES
    src/game/ArabicShaper.cpp
    src/game/Language.cpp
    src/game/Localization.cpp
    src/ui/StbTruetypeImpl.cpp)
minecraftc_add_engine_library(minecraftc_text ${MINECRAFTC_TEXT_SOURCES})
target_include_directories(minecraftc_text PRIVATE external/stb)
target_link_libraries(minecraftc_text
    PUBLIC minecraftc_sdk glm::glm
    PRIVATE minecraftc_assets minecraftc_content_registry minecraftc_core
            minecraftc_json minecraftc_simulation_common)

set(MINECRAFTC_RENDER_DATA_SOURCES
    src/entity/EntityModelRegistry.cpp
    src/model/ModelRenderer.cpp
    src/player/PlayerRenderer.cpp
    src/renderer/BlockAtlasData.cpp
    src/renderer/Camera.cpp
    src/renderer/CameraEffects.cpp
    src/renderer/CloudRenderData.cpp
    src/renderer/Frustum.cpp
    src/renderer/GameRenderer.h
    src/renderer/HeldItemMesh.cpp
    src/renderer/HeldItemRenderer.cpp
    src/renderer/HeldToolModel.cpp
    src/renderer/RenderDevice.cpp
    src/renderer/TexturedCubeScene.cpp
    src/renderer/UIRenderBackend.h)
minecraftc_add_engine_library(minecraftc_render_data ${MINECRAFTC_RENDER_DATA_SOURCES})
target_link_libraries(minecraftc_render_data
    PUBLIC minecraftc_sdk glm::glm
    PRIVATE minecraftc_animation_graph minecraftc_assets minecraftc_atlas_bindings
            minecraftc_content_registry minecraftc_gltf minecraftc_image_decoder
            minecraftc_json minecraftc_log minecraftc_simulation_common)

add_library(minecraftc_platform_sdl STATIC ${MINECRAFTC_SDL_BACKEND_SOURCES})
target_link_libraries(minecraftc_platform_sdl PRIVATE
    minecraftc_assets minecraftc_content_registry minecraftc_core minecraftc_log minecraftc_sdk)
target_include_directories(minecraftc_platform_sdl PRIVATE src)
target_compile_definitions(minecraftc_platform_sdl PRIVATE
    MINECRAFTC_VERSION_STRING="${MINECRAFTC_VERSION_DISPLAY}")
target_link_libraries(minecraftc_platform_sdl PRIVATE
    ${MINECRAFTC_SDL_TARGET} glm::glm)
target_link_libraries(minecraftc_platform_sdl PRIVATE
    ${MINECRAFTC_VULKAN_TARGET})
add_library(minecraftc_render_vulkan STATIC
    ${MINECRAFTC_VULKAN_BACKEND_SOURCES})
target_link_libraries(minecraftc_render_vulkan PRIVATE
    minecraftc_assets minecraftc_atlas_bindings minecraftc_content_registry
    minecraftc_core minecraftc_image_decoder minecraftc_log minecraftc_model_animation
    minecraftc_plugin_runtime minecraftc_render_data minecraftc_sdk minecraftc_text)
target_include_directories(minecraftc_render_vulkan PRIVATE src)
target_include_directories(minecraftc_render_vulkan SYSTEM PRIVATE
    external/VulkanMemoryAllocator/include external/stb)
target_compile_definitions(minecraftc_render_vulkan PRIVATE
    MINECRAFTC_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    MINECRAFTC_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    MINECRAFTC_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    VMA_VULKAN_VERSION=1000000)
target_link_libraries(minecraftc_render_vulkan PRIVATE
    glm::glm ${MINECRAFTC_VULKAN_TARGET})
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(minecraftc_render_vulkan PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

set(MINECRAFTC_UI_SOURCES
    src/app/ApplicationInputController.cpp
    src/game/ClientSettings.cpp
    src/ui/ContainerScreen.cpp
    src/ui/Hotbar.cpp
    src/ui/Inventory.cpp
    src/ui/LanMenu.cpp
    src/ui/Menu.cpp
    src/ui/PluginMenu.cpp
    src/ui/SettingsMenu.cpp
    src/ui/SurvivalInventory.cpp
    src/ui/TouchControls.cpp
    src/ui/UIRenderer.cpp
    src/ui/VillagerTradeScreen.cpp)
minecraftc_add_engine_library(minecraftc_ui ${MINECRAFTC_UI_SOURCES})
target_link_libraries(minecraftc_ui
    PUBLIC minecraftc_sdk minecraftc_text glm::glm
    PRIVATE minecraftc_assets minecraftc_content_registry minecraftc_core minecraftc_json
            minecraftc_network minecraftc_platform minecraftc_plugins
            minecraftc_simulation_common)

set(MINECRAFTC_SCENE_SOURCES
    src/entity/EntityPresenter.cpp
    src/player/PlayerVisual.cpp
    src/renderer/ChunkRenderScene.cpp
    src/renderer/ParticleSystem.cpp)
function(minecraftc_add_scene target simulation)
    minecraftc_add_engine_library(${target} ${MINECRAFTC_SCENE_SOURCES})
    target_link_libraries(${target}
        PUBLIC ${simulation} minecraftc_render_data
        PRIVATE minecraftc_content_registry minecraftc_log
                minecraftc_plugin_runtime minecraftc_atlas_bindings)
endfunction()
minecraftc_add_scene(minecraftc_scene minecraftc_simulation)

set(MINECRAFTC_SESSION_SOURCES
    src/app/ApplicationInputRouter.cpp
    src/app/DimensionSimulation.cpp
    src/app/GameFlowController.cpp
    src/app/GameScenePresenter.cpp
    src/app/GameSession.cpp
    src/app/GameSessionClient.cpp
    src/app/GameSessionInventory.cpp
    src/app/GameSessionLan.cpp
    src/app/GameSessionLanEvents.cpp
    src/app/GameSessionLanLod.cpp
    src/app/GameSessionLanSleep.cpp
    src/app/GameSessionNether.cpp
    src/app/GameUiController.cpp)
function(minecraftc_add_session target scene)
    minecraftc_add_engine_library(${target} ${MINECRAFTC_SESSION_SOURCES})
    target_link_libraries(${target}
        PUBLIC ${scene} minecraftc_network minecraftc_ui
        PRIVATE minecraftc_assets minecraftc_content_registry minecraftc_core minecraftc_json
                minecraftc_log minecraftc_plugin_runtime minecraftc_threading)
endfunction()
minecraftc_add_session(minecraftc_app_session minecraftc_scene)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(minecraftc_platform PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(minecraftc_platform PRIVATE /W4 /permissive- /utf-8)
endif()
