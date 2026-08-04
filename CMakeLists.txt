cmake_minimum_required(VERSION 3.16)
project(MinecraftC VERSION 1.1.3 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
option(MINECRAFTC_FETCH_DEPENDENCIES
       "Use a pinned source build of GLM instead of the system package" OFF)
option(MINECRAFTC_USE_SYSTEM_SDL3
       "Use a system SDL3 3.4.10+ package instead of the pinned source build" OFF)
option(MINECRAFTC_ENABLE_SANITIZERS
       "Enable AddressSanitizer and UndefinedBehaviorSanitizer in Debug builds" OFF)
option(MINECRAFTC_PORTABLE_LAYOUT
       "Install a self-contained release layout rooted at the install prefix" OFF)
option(MINECRAFTC_STATIC_MSVC_RUNTIME
       "Use the static MSVC runtime for portable Windows packages" OFF)
if(MSVC AND MINECRAFTC_STATIC_MSVC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ── Dependencies ───────────────────────────────────────────────────────

if(MINECRAFTC_FETCH_DEPENDENCIES)
    include(FetchContent)
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(glm)
else()
    find_package(glm REQUIRED)
endif()
if(MINECRAFTC_USE_SYSTEM_SDL3)
    find_package(SDL3 3.4.10 CONFIG REQUIRED)
    set(MINECRAFTC_SDL_TARGET SDL3::SDL3)
else()
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
    set(SDL_AUDIO ON CACHE BOOL "" FORCE)
    set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
    set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
    set(SDL_GPU OFF CACHE BOOL "" FORCE)
    set(SDL_HAPTIC ON CACHE BOOL "" FORCE)
    set(SDL_HIDAPI ON CACHE BOOL "" FORCE)
    set(SDL_JOYSTICK ON CACHE BOOL "" FORCE)
    set(SDL_POWER OFF CACHE BOOL "" FORCE)
    set(SDL_RENDER OFF CACHE BOOL "" FORCE)
    set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
    set(SDL_TRAY OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.10
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3)
    set(MINECRAFTC_SDL_TARGET SDL3::SDL3-static)
endif()
find_package(OpenGL REQUIRED)

add_library(minecraftc_assets STATIC src/core/AssetStore.cpp)
target_include_directories(minecraftc_assets PUBLIC src)
target_link_libraries(minecraftc_assets PUBLIC ${MINECRAFTC_SDL_TARGET})
link_libraries(minecraftc_assets)

# ── GLAD (minimal OpenGL loader) ───────────────────────────────────────

add_library(glad STATIC
    external/glad/src/glad.c
)
# Compile glad as C++ (uses reinterpret_cast, extern "C")
set_source_files_properties(external/glad/src/glad.c PROPERTIES LANGUAGE CXX)
target_include_directories(glad PUBLIC external/glad/include)

# ── Main executable ────────────────────────────────────────────────────

file(GLOB_RECURSE SOURCES
    src/*.cpp
    src/*.h
)
list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/core/Platform.cpp")
list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/core/AssetStore.cpp")

add_library(minecraftc_platform STATIC src/core/Platform.cpp)
target_include_directories(minecraftc_platform PUBLIC src)
target_compile_definitions(minecraftc_platform PRIVATE
    MINECRAFTC_INSTALL_DATADIR="${CMAKE_INSTALL_DATADIR}")
if(WIN32)
    target_link_libraries(minecraftc_platform PUBLIC shell32 ole32)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(minecraftc_platform PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(minecraftc_platform PRIVATE /W4 /permissive- /utf-8)
endif()

add_executable(minecraftc ${SOURCES})
target_include_directories(minecraftc PRIVATE
    src
    external/glad/include
    external/FastNoiseLite
    external/cgltf
    external/stb
    external
)
target_compile_definitions(minecraftc PRIVATE
    MINECRAFTC_VERSION_STRING="${PROJECT_VERSION}")

# ── Debug / logging compile-time gating ─────────────────────────────────
# LOG_COMPILE_LEVEL strips log macros below the given level:
#   0=Trace  1=Debug  2=Info  3=Warn  4=Error  5=Fatal
target_compile_definitions(minecraftc PRIVATE
    $<$<CONFIG:Debug>:LOG_COMPILE_LEVEL=0>
    $<$<CONFIG:Release>:LOG_COMPILE_LEVEL=2>
)
target_link_libraries(minecraftc PRIVATE
    ${MINECRAFTC_SDL_TARGET}
    glm::glm
    OpenGL::GL
    glad
    minecraftc_platform
    ${CMAKE_DL_LIBS}
)

# ── Compiler warnings ──────────────────────────────────────────────────

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(minecraftc PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(minecraftc PRIVATE /W4 /permissive- /utf-8)
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT MINECRAFTC_IPO_SUPPORTED OUTPUT MINECRAFTC_IPO_ERROR)
if(MINECRAFTC_IPO_SUPPORTED)
    set_property(TARGET minecraftc PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

if(MINECRAFTC_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(minecraftc PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined -g -O0>)
    target_link_options(minecraftc PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>)
    if(UNIX AND NOT APPLE)
        target_link_options(minecraftc PRIVATE $<$<CONFIG:Debug>:-rdynamic>)
    endif()
endif()

# ── Install ────────────────────────────────────────────────────────────

if(MINECRAFTC_PORTABLE_LAYOUT)
    install(TARGETS minecraftc RUNTIME DESTINATION .)
    install(DIRECTORY assets/ DESTINATION assets)
    install(FILES README.md THIRD_PARTY_LICENSES.md packaging/README-release.md
            DESTINATION .)
    if(WIN32)
        install(FILES packaging/run-minecraftc.cmd DESTINATION .)
    else()
        install(PROGRAMS packaging/run-minecraftc.sh DESTINATION .)
    endif()
else()
    install(TARGETS minecraftc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY assets/ DESTINATION ${CMAKE_INSTALL_DATADIR}/minecraftc/assets)
endif()

# ── Deterministic world-generation tests ───────────────────────────────────
include(CTest)
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME release_version_cli COMMAND minecraftc --version)
    set_tests_properties(release_version_cli PROPERTIES
        PASS_REGULAR_EXPRESSION "MinecraftC Release-1.1.3")
    add_test(NAME texture_generator_determinism_tiling_and_atlas
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/test_texture_generator.py)

    add_executable(model_asset_tests
        tests/ModelAssetTests.cpp
        src/model/ModelAnimation.cpp)
    target_include_directories(model_asset_tests PRIVATE src)
    target_link_libraries(model_asset_tests PRIVATE glm::glm)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(model_asset_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME model_asset_layout_and_weights COMMAND model_asset_tests)

    add_executable(model_animation_tests
        tests/ModelAnimationTests.cpp
        src/model/ModelAnimation.cpp)
    target_include_directories(model_animation_tests PRIVATE src)
    target_link_libraries(model_animation_tests PRIVATE glm::glm)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(model_animation_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME model_animation_sampling_and_pose_math COMMAND model_animation_tests)

    add_executable(animation_graph_tests
        tests/AnimationGraphTests.cpp
        src/model/AnimationGraph.cpp
        src/model/AnimationMixer.cpp
        src/model/ModelAnimation.cpp)
    target_include_directories(animation_graph_tests PRIVATE src external)
    target_link_libraries(animation_graph_tests PRIVATE glm::glm)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(animation_graph_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(animation_graph_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_test(NAME animation_graph_layers_queues_masks_and_events
        COMMAND animation_graph_tests)

    add_executable(gltf_loader_tests
        tests/GltfLoaderTests.cpp
        src/model/GltfLoader.cpp
        src/renderer/StbImageImpl.cpp)
    target_include_directories(gltf_loader_tests PRIVATE src external/cgltf external/stb)
    target_compile_definitions(gltf_loader_tests PRIVATE
        MINECRAFTC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(gltf_loader_tests PRIVATE glm::glm)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(gltf_loader_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(gltf_loader_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_test(NAME gltf_loader_validation_and_conversion COMMAND gltf_loader_tests)

    add_executable(platform_tests tests/PlatformTests.cpp)
    target_include_directories(platform_tests PRIVATE src)
    target_link_libraries(platform_tests PRIVATE minecraftc_platform)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(platform_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(platform_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_test(NAME platform_paths_and_atomic_replace COMMAND platform_tests)

    add_executable(asset_definition_tests
        tests/AssetDefinitionTests.cpp
        src/world/Block.cpp
        src/game/Item.cpp)
    target_include_directories(asset_definition_tests PRIVATE src)
    target_link_libraries(asset_definition_tests PRIVATE glm::glm)
    target_compile_definitions(asset_definition_tests PRIVATE
        MINECRAFTC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(asset_definition_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME asset_json_block_item_and_atlas_mapping
             COMMAND asset_definition_tests)

    add_executable(cavegen_tests
        tests/CaveGeneratorTests.cpp
        src/world/CaveGenerator.cpp
        src/world/Noise.cpp
    )
    target_include_directories(cavegen_tests PRIVATE src external/FastNoiseLite)
    target_link_libraries(cavegen_tests PRIVATE glm::glm)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(cavegen_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME cavegen_determinism_and_boundaries COMMAND cavegen_tests)

    add_executable(worldgen_tests
        tests/WorldGenerationTests.cpp
        src/world/HeightPipeline.cpp
        src/world/TreeGenerator.cpp
        src/world/WorldGenerator.cpp
        src/world/RegionGenerator.cpp
        src/world/CaveGenerator.cpp
        src/world/OreGenerator.cpp
        src/world/Chunk.cpp
        src/world/Noise.cpp
        src/world/BiomeMap.cpp
        src/world/Block.cpp
    )
    target_include_directories(worldgen_tests PRIVATE
        src external/FastNoiseLite external/glad/include)
    target_link_libraries(worldgen_tests PRIVATE glm::glm glad)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(worldgen_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME worldgen_determinism_boundaries_and_distribution COMMAND worldgen_tests)

    add_executable(rendering_logic_tests
        tests/RenderingLogicTests.cpp
        src/renderer/CameraEffects.cpp
    )
    target_include_directories(rendering_logic_tests PRIVATE src)
    target_link_libraries(rendering_logic_tests PRIVATE glm::glm)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(rendering_logic_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME rendering_day_cycle_and_environment COMMAND rendering_logic_tests)

    add_executable(player_physics_tests
        tests/PlayerPhysicsTests.cpp
        src/world/Block.cpp
    )
    target_include_directories(player_physics_tests PRIVATE src)
    target_link_libraries(player_physics_tests PRIVATE glm::glm)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(player_physics_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME player_ledge_and_auto_jump_logic COMMAND player_physics_tests)

    add_executable(inventory_tests
        tests/InventoryTests.cpp
        src/game/Item.cpp
        src/game/InventoryModel.cpp
        src/world/Block.cpp
    )
    target_include_directories(inventory_tests PRIVATE src)
    target_link_libraries(inventory_tests PRIVATE glm::glm)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(inventory_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME survival_item_registry_and_inventory COMMAND inventory_tests)

    add_executable(save_store_tests
        tests/SaveStoreTests.cpp
        src/game/SaveStore.cpp
        src/game/Item.cpp
        src/game/InventoryModel.cpp
        src/world/Block.cpp
    )
    target_include_directories(save_store_tests PRIVATE src external/FastNoiseLite)
    target_link_libraries(save_store_tests PRIVATE glm::glm minecraftc_platform)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(save_store_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME survival_save_roundtrip_and_corruption COMMAND save_store_tests)

    add_executable(survival_rules_tests
        tests/SurvivalRulesTests.cpp
        src/game/SurvivalRules.cpp
        src/game/Item.cpp
        src/world/Block.cpp
    )
    target_include_directories(survival_rules_tests PRIVATE src)
    target_link_libraries(survival_rules_tests PRIVATE glm::glm)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(survival_rules_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME survival_mining_crafting_and_smelting COMMAND survival_rules_tests)

    add_executable(survival_stats_tests
        tests/SurvivalStatsTests.cpp
        src/game/SurvivalStats.cpp
        src/game/Item.cpp
        src/world/Block.cpp
    )
    target_include_directories(survival_stats_tests PRIVATE src)
    target_link_libraries(survival_stats_tests PRIVATE glm::glm)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(survival_stats_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME survival_health_hunger_and_starvation COMMAND survival_stats_tests)

    add_executable(world_catalog_tests
        tests/WorldCatalogTests.cpp
        src/game/WorldCatalog.cpp
        src/game/SaveStore.cpp
        src/game/Item.cpp
        src/game/InventoryModel.cpp
        src/world/Block.cpp
    )
    target_include_directories(world_catalog_tests PRIVATE src external/FastNoiseLite)
    target_link_libraries(world_catalog_tests PRIVATE glm::glm minecraftc_platform)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(world_catalog_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME survival_multiple_world_catalog COMMAND world_catalog_tests)

    add_executable(command_tests tests/CommandTests.cpp)
    target_include_directories(command_tests PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(command_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME cheat_gamemode_command_parsing COMMAND command_tests)

    add_executable(weather_logic_tests
        tests/WeatherTests.cpp
        src/game/Weather.cpp
        src/world/BiomeMap.cpp)
    target_include_directories(weather_logic_tests PRIVATE src)
    target_link_libraries(weather_logic_tests PRIVATE glm::glm)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(weather_logic_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME weather_cycle_precipitation_and_commands COMMAND weather_logic_tests)

    add_executable(entity_logic_tests tests/EntityLogicTests.cpp)
    target_include_directories(entity_logic_tests PRIVATE src)
    target_link_libraries(entity_logic_tests PRIVATE glm::glm)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(entity_logic_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME survival_entity_spawning_despawn_and_sweep COMMAND entity_logic_tests)

    add_executable(entity_model_registry_tests
        tests/EntityModelRegistryTests.cpp
        src/entity/EntityModelRegistry.cpp
        src/model/AnimationGraph.cpp
        src/model/AnimationMixer.cpp
        src/model/ModelAnimation.cpp
        src/model/GltfLoader.cpp
        src/model/ModelRenderer.cpp
        src/core/GamepadManager.cpp
        src/core/Window.cpp
        src/debug/Log.cpp
        src/renderer/Shader.cpp
        src/renderer/StbImageImpl.cpp)
    target_include_directories(entity_model_registry_tests PRIVATE
        src external external/cgltf external/stb external/glad/include)
    target_compile_definitions(entity_model_registry_tests PRIVATE
        MINECRAFTC_VERSION_STRING="${PROJECT_VERSION}")
    target_link_libraries(entity_model_registry_tests PRIVATE
        glm::glm glad ${MINECRAFTC_SDL_TARGET} OpenGL::GL minecraftc_platform)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(entity_model_registry_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(entity_model_registry_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_test(NAME entity_model_registry_sharing_and_fallback
             COMMAND entity_model_registry_tests)

    add_custom_target(entity_model_assets
        COMMAND ${CMAKE_COMMAND} -E env
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_entity_models.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME entity_model_asset_contract
        COMMAND ${CMAKE_COMMAND} -E env
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_entity_models.py)

    add_executable(lighting_logic_tests tests/LightingLogicTests.cpp src/world/Block.cpp)
    target_include_directories(lighting_logic_tests PRIVATE src)
    target_link_libraries(lighting_logic_tests PRIVATE glm::glm)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(lighting_logic_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME rendering_cross_chunk_block_light COMMAND lighting_logic_tests)

    add_executable(client_input_tests
        tests/ClientInputTests.cpp
        src/core/Input.cpp
        src/core/GamepadManager.cpp
        src/core/InputCodes.cpp
        src/core/TextEditBuffer.cpp
        src/debug/Log.cpp
        src/ui/TouchControls.cpp
        src/game/ClientSettings.cpp
        src/game/Utf8.cpp
        src/game/Language.cpp
        src/game/InventoryInteraction.cpp
        src/game/Item.cpp
        src/world/Block.cpp)
    target_include_directories(client_input_tests PRIVATE src external/glad/include)
    target_link_libraries(client_input_tests PRIVATE
        glm::glm ${MINECRAFTC_SDL_TARGET} minecraftc_platform)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(client_input_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME client_settings_bindings_and_inventory_gestures COMMAND client_input_tests)

    add_executable(localization_tests
        tests/LocalizationTests.cpp
        src/core/Input.cpp
        src/core/InputCodes.cpp
        src/game/Item.cpp
        src/game/Language.cpp
        src/game/Localization.cpp
        src/game/Utf8.cpp
        src/ui/StbTruetypeImpl.cpp
        src/world/Block.cpp)
    target_include_directories(localization_tests PRIVATE
        src external external/stb)
    target_compile_definitions(localization_tests PRIVATE
        MINECRAFTC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(localization_tests PRIVATE glm::glm ${MINECRAFTC_SDL_TARGET})
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(localization_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(localization_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_test(NAME localization_resources_utf8_and_cjk_font
             COMMAND localization_tests)

    add_executable(survival_integration_tests
        tests/SurvivalIntegrationTests.cpp
        src/game/SurvivalRules.cpp
        src/game/SurvivalSession.cpp
        src/game/SurvivalBlockLogic.cpp
        src/game/SaveStore.cpp
        src/game/InventoryModel.cpp
        src/game/Item.cpp
        src/world/Block.cpp
        src/world/BlockEntityLogic.cpp)
    target_include_directories(survival_integration_tests PRIVATE src external/FastNoiseLite)
    target_link_libraries(survival_integration_tests PRIVATE glm::glm minecraftc_platform)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(survival_integration_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME survival_progression_persistence_and_recovery COMMAND survival_integration_tests)
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(texture_generator
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/texture_generator.py
                --generate --validate --build-atlas --build-items-atlas
                --build-entity-atlas --build-entity-skins
                --output ${CMAKE_SOURCE_DIR}/assets/textures/generated
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating and validating deterministic voxel textures")
endif()
