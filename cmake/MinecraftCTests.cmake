if(ANDROID OR IOS)
    set(BUILD_TESTING OFF CACHE BOOL "Build tests" FORCE)
endif()
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    include(tools/test_policy.cmake)

    # Historical Heaven queries and structure fixtures require a distinct world
    # implementation. Link this variant only where tests exercise those queries.
    minecraftc_add_engine_library(minecraftc_simulation_test
        ${MINECRAFTC_SIMULATION_WORLD_SOURCES}
        tests/HeavenGeneratorQueryAdapters.cpp
        tests/HeavenWorldQueryAdapters.cpp)
    minecraftc_link_simulation_dependencies(minecraftc_simulation_test)
    target_link_libraries(minecraftc_simulation_test PUBLIC minecraftc_simulation_common PRIVATE minecraftc_heaven)
    target_compile_definitions(minecraftc_simulation_test PUBLIC MINECRAFTC_HEAVEN_TEST_QUERIES=1)
    minecraftc_add_scene(minecraftc_scene_test minecraftc_simulation_test)
    minecraftc_add_session(minecraftc_app_session_test minecraftc_scene_test)

    minecraftc_add_test(NAME ci_compiler_cache_fallback
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_ci_sccache.py)
    minecraftc_add_test(NAME ci_ios_configuration_and_smoke_guards
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_ci_ios.py)
    add_executable(pixel_readback_tests tests/PixelReadbackTests.cpp)
    minecraftc_add_test(NAME pixel_readback_negative_controls COMMAND pixel_readback_tests)
    minecraftc_add_test(NAME test_harness_isolation_and_readback_guards
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_harness.py)
    minecraftc_add_test(NAME api_boundary_guard_mutations
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_api_boundaries.py)
    minecraftc_add_test(NAME api_dependency_boundaries
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tools/check_api_boundaries.py
                --root ${CMAKE_SOURCE_DIR})
    minecraftc_add_test(NAME module_dependency_boundaries
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tools/check_module_boundaries.py
                ${CMAKE_SOURCE_DIR}
                "${CMAKE_BINARY_DIR}/minecraftc-modules.tsv")
    minecraftc_add_test(NAME numbered_prerelease_versions
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_project_version.py
                --cmake ${CMAKE_COMMAND})
    foreach(cli_case version help opengl)
        if(cli_case STREQUAL "version")
            set(cli_name release_version_cli)
        elseif(cli_case STREQUAL "help")
            set(cli_name vulkan_renderer_cli_help)
        else()
            set(cli_name removed_opengl_renderer_cli)
        endif()
        minecraftc_add_test(NAME ${cli_name} COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/test_cli.py $<TARGET_FILE:minecraftc>
            ${cli_case} ${MINECRAFTC_VERSION_DISPLAY} ${MINECRAFTC_VERSION})
        set_tests_properties(${cli_name} PROPERTIES TIMEOUT 10)
    endforeach()
    minecraftc_add_test(NAME texture_recipes_sequences_and_workbench
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_texture_recipes.py)
    minecraftc_add_test(NAME official_provider_package_resources
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_official_package.py)
    minecraftc_add_test(NAME texture_generator_determinism_tiling_and_atlas
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/test_texture_generator.py)
    minecraftc_add_test(NAME audio_engine_envelopes_tails_and_mastering
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_audio_assets.py EngineTests)
    minecraftc_add_test(NAME music_assets_determinism_format_and_loop
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/test_audio_assets.py AssetTests)
    minecraftc_add_test(NAME vulkan_shader_assets
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tools/vulkan_shaders.py
                --root ${CMAKE_SOURCE_DIR})

    minecraftc_add_test(NAME vulkan_shader_provenance
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/test_vulkan_shaders.py)

    add_executable(vulkan_index_rebase_tests
        tests/VulkanIndexRebaseTests.cpp)
    target_include_directories(vulkan_index_rebase_tests PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(vulkan_index_rebase_tests PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME vulkan_zero_base_vertex_index_rebasing
        COMMAND vulkan_index_rebase_tests)

    add_executable(model_asset_tests
        tests/ModelAssetTests.cpp)
    target_link_libraries(model_asset_tests PRIVATE
        minecraftc_model_animation)
    target_include_directories(model_asset_tests PRIVATE src)
    target_link_libraries(model_asset_tests PRIVATE glm::glm)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(model_asset_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME model_asset_layout_and_weights COMMAND model_asset_tests)

    add_executable(model_animation_tests
        tests/ModelAnimationTests.cpp)
    target_link_libraries(model_animation_tests PRIVATE
        minecraftc_model_animation)
    target_include_directories(model_animation_tests PRIVATE src)
    target_link_libraries(model_animation_tests PRIVATE glm::glm)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(model_animation_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME model_animation_sampling_and_pose_math COMMAND model_animation_tests)

    add_executable(animation_graph_tests
        tests/AnimationGraphTests.cpp)
    target_link_libraries(animation_graph_tests PRIVATE
        minecraftc_model_animation minecraftc_animation_graph)
    target_link_libraries(animation_graph_tests PRIVATE minecraftc_assets minecraftc_json)
    target_include_directories(animation_graph_tests PRIVATE src external)
    target_link_libraries(animation_graph_tests PRIVATE glm::glm)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(animation_graph_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(animation_graph_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    minecraftc_add_test(NAME animation_graph_layers_queues_masks_and_events
        COMMAND animation_graph_tests)

    add_executable(gltf_loader_tests
        tests/GltfLoaderTests.cpp)
    target_link_libraries(gltf_loader_tests PRIVATE
        minecraftc_image_decoder minecraftc_model_animation minecraftc_gltf)
    target_link_libraries(gltf_loader_tests PRIVATE minecraftc_assets)
    target_include_directories(gltf_loader_tests PRIVATE src external/cgltf external/stb)
    target_compile_definitions(gltf_loader_tests PRIVATE
        MINECRAFTC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(gltf_loader_tests PRIVATE glm::glm)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(gltf_loader_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(gltf_loader_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    minecraftc_add_test(NAME gltf_loader_validation_and_conversion COMMAND gltf_loader_tests)

    add_executable(platform_tests tests/PlatformTests.cpp)
    target_include_directories(platform_tests PRIVATE src)
    target_link_libraries(platform_tests PRIVATE minecraftc_platform)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(platform_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(platform_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    minecraftc_add_test(NAME platform_paths_and_atomic_replace COMMAND platform_tests)

    add_executable(asset_definition_tests tests/AssetDefinitionTests.cpp)
    target_link_libraries(asset_definition_tests PRIVATE
        minecraftc_image_decoder minecraftc_assets minecraftc_json minecraftc_sdk glm::glm
        minecraftc_simulation_common minecraftc_render_data minecraftc_heaven
        minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(asset_definition_tests PRIVATE src external/stb)
    target_compile_definitions(asset_definition_tests PRIVATE
        MINECRAFTC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(asset_definition_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME asset_json_block_item_and_atlas_mapping
             COMMAND asset_definition_tests)

    # Compile the cave generator with the test-only decorator setting.
    add_executable(cavegen_tests
        tests/CaveGeneratorTests.cpp
        src/world/CaveGenerator.cpp)
    target_link_libraries(cavegen_tests PRIVATE
        minecraftc_sdk glm::glm minecraftc_terrain)
    target_include_directories(cavegen_tests PRIVATE src external/FastNoiseLite)
    target_compile_definitions(cavegen_tests PRIVATE
        MINECRAFTC_CAVEGEN_NO_DECORATOR=1)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(cavegen_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME cavegen_determinism_and_boundaries COMMAND cavegen_tests)

    add_executable(worldgen_tests tests/WorldGenerationTests.cpp)
    target_link_libraries(worldgen_tests PRIVATE
        minecraftc_assets minecraftc_json minecraftc_sdk glm::glm minecraftc_simulation_test
        minecraftc_heaven minecraftc_content_registry minecraftc_atlas_bindings)
    target_sources(worldgen_tests PRIVATE tests/reference/heaven_v9/FrozenHeavenV9.cpp)
    target_include_directories(worldgen_tests PRIVATE
        src external/FastNoiseLite)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(worldgen_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME worldgen_determinism_boundaries_and_distribution COMMAND worldgen_tests --overworld-tests)
    minecraftc_add_test(NAME overworld_structure_placement_and_reservations COMMAND worldgen_tests --structure-tests)
    minecraftc_add_test(NAME overworld_ore_hosts_biomes_and_seed_variation COMMAND worldgen_tests --ore-tests)
    minecraftc_add_test(NAME world_mesh_material_layers_and_lighting COMMAND worldgen_tests --mesh-tests)
    minecraftc_add_test(NAME overworld_ecology_and_blueprints COMMAND worldgen_tests --ecology-tests)
    minecraftc_add_test(NAME nether_native_generation_and_portals COMMAND worldgen_tests --nether-tests)
    minecraftc_add_test(NAME heaven_worldgen_determinism_boundaries_and_distribution COMMAND worldgen_tests --heaven-tests)
    minecraftc_add_test(NAME heaven_p1_independent_migration_reference COMMAND worldgen_tests --heaven-migration-tests)
    minecraftc_add_test(NAME village_layout_counts_access_and_reservations COMMAND worldgen_tests --village-layout-tests)

    add_executable(lod_terrain_tests tests/LodTerrainTests.cpp)
    target_link_libraries(lod_terrain_tests PRIVATE
        minecraftc_log minecraftc_threading minecraftc_assets minecraftc_json minecraftc_sdk
        glm::glm minecraftc_platform minecraftc_simulation minecraftc_heaven
        minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(lod_terrain_tests PRIVATE
        src external/FastNoiseLite)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(lod_terrain_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(lod_terrain_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    minecraftc_add_test(NAME lod_terrain_determinism_cache_and_mesh COMMAND lod_terrain_tests)
    minecraftc_add_test(NAME lod_replica_replacement_uploads COMMAND lod_terrain_tests --replica-replacement-tests)

    add_executable(terrain_preview tools/terrain_preview.cpp)
    target_link_libraries(terrain_preview PRIVATE
        minecraftc_assets minecraftc_sdk glm::glm minecraftc_simulation_common
        minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(terrain_preview PRIVATE src external/FastNoiseLite)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(terrain_preview PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_executable(terrain_benchmark tools/terrain_benchmark.cpp)
    target_link_libraries(terrain_benchmark PRIVATE
        minecraftc_assets minecraftc_json minecraftc_sdk glm::glm minecraftc_simulation
        minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(terrain_benchmark PRIVATE
        src external/FastNoiseLite)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(terrain_benchmark PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_executable(rendering_logic_tests tests/RenderingLogicTests.cpp)
    target_link_libraries(rendering_logic_tests PRIVATE
        minecraftc_assets minecraftc_json minecraftc_sdk glm::glm minecraftc_simulation_common
        minecraftc_render_data minecraftc_heaven minecraftc_content_registry
        minecraftc_atlas_bindings)
    target_include_directories(rendering_logic_tests PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(rendering_logic_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME rendering_day_cycle_and_environment COMMAND rendering_logic_tests)

    add_executable(ui_style_tests tests/UiStyleTests.cpp)
    target_link_libraries(ui_style_tests PRIVATE minecraftc_sdk)
    target_include_directories(ui_style_tests PRIVATE src)
    target_link_libraries(ui_style_tests PRIVATE glm::glm)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ui_style_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME modern_ui_geometry_and_layout COMMAND ui_style_tests)

    add_executable(vulkan_ui_smoke EXCLUDE_FROM_ALL tests/VulkanUiSmoke.cpp)
    target_link_libraries(vulkan_ui_smoke PRIVATE
        minecraftc_log minecraftc_core minecraftc_image_decoder minecraftc_model_animation
        minecraftc_animation_graph minecraftc_gltf minecraftc_assets minecraftc_json
        minecraftc_sdk glm::glm ${MINECRAFTC_VULKAN_TARGET} minecraftc_render_vulkan
        minecraftc_platform_sdl minecraftc_platform minecraftc_simulation
        minecraftc_plugins minecraftc_network minecraftc_text minecraftc_render_data
        minecraftc_ui minecraftc_content_registry minecraftc_atlas_bindings
        minecraftc_plugin_runtime)
    target_include_directories(vulkan_ui_smoke PRIVATE src external external/stb external/nlohmann external/cgltf)
    target_include_directories(vulkan_ui_smoke SYSTEM PRIVATE external/VulkanMemoryAllocator/include)
    target_compile_definitions(vulkan_ui_smoke PRIVATE
        MINECRAFTC_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        MINECRAFTC_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        MINECRAFTC_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        VMA_VULKAN_VERSION=1000000
        MINECRAFTC_VERSION_STRING="${MINECRAFTC_VERSION_DISPLAY}")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(vulkan_ui_smoke PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_executable(held_tool_vulkan_smoke EXCLUDE_FROM_ALL tests/HeldToolVulkanSmoke.cpp)
    target_link_libraries(held_tool_vulkan_smoke PRIVATE
        minecraftc_log minecraftc_core minecraftc_image_decoder minecraftc_model_animation
        minecraftc_animation_graph minecraftc_gltf minecraftc_assets minecraftc_json
        minecraftc_sdk glm::glm minecraftc_render_vulkan minecraftc_platform_sdl
        minecraftc_platform minecraftc_simulation_common minecraftc_render_data minecraftc_heaven
        minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(held_tool_vulkan_smoke PRIVATE src external external/stb external/cgltf)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(held_tool_vulkan_smoke PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    # Explicit display-dependent validation; ordinary builds/CTest stay headless.
    add_executable(vulkan_decoration_smoke EXCLUDE_FROM_ALL tests/VulkanDecorationSmoke.cpp)
    target_link_libraries(vulkan_decoration_smoke PRIVATE
        minecraftc_log minecraftc_image_decoder minecraftc_model_animation minecraftc_assets
        minecraftc_json minecraftc_sdk glm::glm minecraftc_render_vulkan minecraftc_platform_sdl
        minecraftc_platform minecraftc_simulation_test minecraftc_render_data minecraftc_heaven
        minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(vulkan_decoration_smoke PRIVATE src external/stb external/FastNoiseLite)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(vulkan_decoration_smoke PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(vulkan_decoration_smoke PRIVATE /W4 /permissive- /utf-8)
    endif()

    # Display-dependent structure scene for manual use.
    add_executable(vulkan_structure_smoke EXCLUDE_FROM_ALL tests/VulkanStructureSmoke.cpp)
    target_link_libraries(vulkan_structure_smoke PRIVATE
        minecraftc_threading minecraftc_model_animation minecraftc_animation_graph
        minecraftc_gltf minecraftc_assets minecraftc_json minecraftc_sdk glm::glm
        ${MINECRAFTC_VULKAN_TARGET} minecraftc_render_vulkan minecraftc_platform_sdl
        minecraftc_platform minecraftc_simulation minecraftc_render_data
        minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(vulkan_structure_smoke PRIVATE src external external/stb external/FastNoiseLite external/cgltf)
    target_include_directories(vulkan_structure_smoke SYSTEM PRIVATE external/VulkanMemoryAllocator/include)
    target_compile_definitions(vulkan_structure_smoke PRIVATE
        MINECRAFTC_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        MINECRAFTC_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        MINECRAFTC_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        VMA_VULKAN_VERSION=1000000)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(vulkan_structure_smoke PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(vulkan_structure_smoke PRIVATE /W4 /permissive- /utf-8)
    endif()

    add_executable(vulkan_cloud_lod_smoke EXCLUDE_FROM_ALL tests/VulkanCloudLodSmoke.cpp)
    target_link_libraries(vulkan_cloud_lod_smoke PRIVATE
        minecraftc_log minecraftc_image_decoder minecraftc_model_animation minecraftc_assets
        minecraftc_sdk glm::glm minecraftc_render_vulkan minecraftc_platform_sdl
        minecraftc_platform minecraftc_simulation_common minecraftc_render_data
        minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(vulkan_cloud_lod_smoke PRIVATE src external/stb)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(vulkan_cloud_lod_smoke PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(vulkan_cloud_lod_smoke PRIVATE /W4 /permissive- /utf-8)
    endif()

    add_executable(vulkan_model_fog_smoke EXCLUDE_FROM_ALL tests/VulkanModelFogSmoke.cpp)
    target_link_libraries(vulkan_model_fog_smoke PRIVATE
        minecraftc_log minecraftc_image_decoder minecraftc_model_animation minecraftc_assets
        minecraftc_sdk glm::glm ${MINECRAFTC_VULKAN_TARGET} minecraftc_render_vulkan
        minecraftc_platform_sdl minecraftc_platform minecraftc_simulation_common
        minecraftc_render_data minecraftc_heaven minecraftc_content_registry
        minecraftc_atlas_bindings)
    target_include_directories(vulkan_model_fog_smoke PRIVATE src external/stb)
    target_include_directories(vulkan_model_fog_smoke SYSTEM PRIVATE external/VulkanMemoryAllocator/include)
    target_compile_definitions(vulkan_model_fog_smoke PRIVATE
        MINECRAFTC_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        MINECRAFTC_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        MINECRAFTC_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        VMA_VULKAN_VERSION=1000000)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(vulkan_model_fog_smoke PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(vulkan_model_fog_smoke PRIVATE /W4 /permissive- /utf-8)
    endif()

    # Display-dependent integration scene: run the host and client separately.
    add_executable(vulkan_lan_smoke EXCLUDE_FROM_ALL tests/VulkanLanSmoke.cpp)
    target_link_libraries(vulkan_lan_smoke PRIVATE
        minecraftc_app_session minecraftc_log minecraftc_core minecraftc_threading
        minecraftc_image_decoder minecraftc_model_animation minecraftc_animation_graph
        minecraftc_gltf minecraftc_assets minecraftc_json minecraftc_sdk glm::glm
        ${MINECRAFTC_VULKAN_TARGET} minecraftc_render_vulkan minecraftc_platform_sdl
        minecraftc_platform ${CMAKE_DL_LIBS} minecraftc_content_registry
        minecraftc_atlas_bindings minecraftc_plugin_runtime)
    target_include_directories(vulkan_lan_smoke PRIVATE
        src external external/stb external/cgltf external/FastNoiseLite)
    target_include_directories(vulkan_lan_smoke SYSTEM PRIVATE external/VulkanMemoryAllocator/include)
    target_compile_definitions(vulkan_lan_smoke PRIVATE
        MINECRAFTC_VERSION_STRING="${MINECRAFTC_VERSION_DISPLAY}"
        MINECRAFTC_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        MINECRAFTC_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        MINECRAFTC_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        VMA_VULKAN_VERSION=1000000)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(vulkan_lan_smoke PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(vulkan_lan_smoke PRIVATE /W4 /permissive- /utf-8)
    endif()

    add_executable(vulkan_gi_smoke EXCLUDE_FROM_ALL tests/VulkanGiSmoke.cpp)
    target_link_libraries(vulkan_gi_smoke PRIVATE
        minecraftc_log minecraftc_image_decoder minecraftc_model_animation minecraftc_assets
        minecraftc_json minecraftc_sdk glm::glm ${MINECRAFTC_VULKAN_TARGET}
        minecraftc_render_vulkan minecraftc_platform_sdl minecraftc_platform
        minecraftc_simulation_common minecraftc_render_data minecraftc_heaven
        minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(vulkan_gi_smoke PRIVATE src external/stb)
    target_include_directories(vulkan_gi_smoke SYSTEM PRIVATE external/VulkanMemoryAllocator/include)
    target_compile_definitions(vulkan_gi_smoke PRIVATE
        MINECRAFTC_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        MINECRAFTC_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        MINECRAFTC_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        VMA_VULKAN_VERSION=1000000)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(vulkan_gi_smoke PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(vulkan_gi_smoke PRIVATE /W4 /permissive- /utf-8)
    endif()

    option(MINECRAFTC_VULKAN_TESTS "Register display-dependent Vulkan readback tests" ON)
    if(MINECRAFTC_VULKAN_TESTS)
        foreach(scene gi model_fog)
            set_property(TARGET vulkan_${scene}_smoke PROPERTY EXCLUDE_FROM_ALL FALSE)
            minecraftc_add_test(NAME vulkan_${scene}_readback COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/run_vulkan_test.py
                $<TARGET_FILE:vulkan_${scene}_smoke> ${CMAKE_SOURCE_DIR}/assets ${scene})
            set_tests_properties(vulkan_${scene}_readback PROPERTIES
                LABELS vulkan SKIP_RETURN_CODE 77 TIMEOUT 240 RESOURCE_LOCK vulkan_device)
        endforeach()
    endif()

    add_executable(vulkan_gi_benchmark EXCLUDE_FROM_ALL tests/VulkanGiBenchmark.cpp)
    target_link_libraries(vulkan_gi_benchmark PRIVATE
        minecraftc_log minecraftc_image_decoder minecraftc_model_animation minecraftc_assets
        minecraftc_sdk glm::glm ${MINECRAFTC_VULKAN_TARGET} minecraftc_render_vulkan
        minecraftc_platform_sdl minecraftc_platform minecraftc_simulation_common
        minecraftc_render_data minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(vulkan_gi_benchmark PRIVATE src external/stb)
    target_include_directories(vulkan_gi_benchmark SYSTEM PRIVATE external/VulkanMemoryAllocator/include)
    target_compile_definitions(vulkan_gi_benchmark PRIVATE
        MINECRAFTC_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        MINECRAFTC_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        MINECRAFTC_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        VMA_VULKAN_VERSION=1000000)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(vulkan_gi_benchmark PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(vulkan_gi_benchmark PRIVATE /W4 /permissive- /utf-8)
    endif()

    add_executable(voxel_gi_tests tests/VoxelGiTests.cpp)
    target_link_libraries(voxel_gi_tests PRIVATE
        minecraftc_assets minecraftc_json minecraftc_sdk glm::glm minecraftc_simulation_common
        minecraftc_heaven minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(voxel_gi_tests PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(voxel_gi_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME voxel_gi_clipmap_and_temporal COMMAND voxel_gi_tests)

    add_executable(player_physics_tests tests/PlayerPhysicsTests.cpp)
    target_link_libraries(player_physics_tests PRIVATE
        minecraftc_assets minecraftc_sdk glm::glm minecraftc_simulation_common
        minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(player_physics_tests PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(player_physics_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME player_ledge_and_auto_jump_logic COMMAND player_physics_tests)

    add_executable(nether_progression_tests tests/NetherProgressionTests.cpp)
    target_link_libraries(nether_progression_tests PRIVATE
        minecraftc_assets minecraftc_sdk glm::glm minecraftc_simulation_common minecraftc_network
        minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(nether_progression_tests PRIVATE src sdk external)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(nether_progression_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME nether_native_brewing_smithing_and_effects COMMAND nether_progression_tests)

    add_executable(inventory_tests tests/InventoryTests.cpp)
    target_link_libraries(inventory_tests PRIVATE
        minecraftc_assets minecraftc_json minecraftc_sdk glm::glm minecraftc_simulation_common
        minecraftc_heaven minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(inventory_tests PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(inventory_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME survival_item_registry_and_inventory COMMAND inventory_tests)

    add_executable(plugin_tests tests/PluginTests.cpp)
    target_link_libraries(plugin_tests PRIVATE
        minecraftc_log minecraftc_image_decoder minecraftc_model_animation minecraftc_gltf
        minecraftc_assets minecraftc_json minecraftc_sdk glm::glm minecraftc_platform
        package_fixture minecraftc_simulation_common minecraftc_plugins minecraftc_render_data
        minecraftc_content_registry minecraftc_atlas_bindings minecraftc_plugin_runtime)
    target_include_directories(plugin_tests PRIVATE src external external/stb external/cgltf)
    target_compile_definitions(plugin_tests PRIVATE MINECRAFTC_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        MINECRAFTC_VERSION_STRING="${MINECRAFTC_VERSION_DISPLAY}")
    minecraftc_add_test(NAME natural_texture_plugin COMMAND plugin_tests --natural-textures)
    minecraftc_add_test(NAME natural_texture_manifest_and_coverage COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tests/test_natural_pixels.py NaturalPixelsTests.test_full_coverage_and_sequences)
    minecraftc_add_test(NAME natural_texture_assets COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tests/test_natural_pixels.py NaturalPixelsTests.test_reproducible_and_publishable)
    minecraftc_add_content_package(package_fixture
        ROOT "${CMAKE_SOURCE_DIR}/examples/plugins/package_demo" MODE BUILTIN)
    minecraftc_add_test(NAME content_package_authoring_loading_and_delivery
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_content_package.py
            --plugin-tests $<TARGET_FILE:plugin_tests>
            --builtin-stage "${CMAKE_BINARY_DIR}/content-assets/plugins/package_demo")
    set_tests_properties(content_package_authoring_loading_and_delivery PROPERTIES TIMEOUT 180)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(plugin_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(plugin_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    if(NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        foreach(extension_demo entity_demo terrain_demo render_demo dimension_demo block_shapes_demo capability_native worldgen_v2_demo worldgen_policy_v2)
            add_library(plugin_${extension_demo} MODULE examples/plugins/${extension_demo}/plugin.cpp)
            target_include_directories(plugin_${extension_demo} PRIVATE sdk)
            if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
                target_compile_options(plugin_${extension_demo} PRIVATE -Wall -Wextra -Wpedantic)
            elseif(MSVC)
                target_compile_options(plugin_${extension_demo} PRIVATE /W4 /permissive- /utf-8)
            endif()
            set_target_properties(plugin_${extension_demo} PROPERTIES PREFIX "" OUTPUT_NAME ${extension_demo})
            if(APPLE)
                set_target_properties(plugin_${extension_demo} PROPERTIES SUFFIX ".dylib")
            endif()
        endforeach()
        add_library(plugin_native_demo MODULE examples/plugins/native_demo/plugin.cpp)
        target_link_libraries(plugin_native_demo PRIVATE minecraftc_sdk)
        add_library(plugin_bad_abi MODULE tests/PluginBadEntry.cpp)
        target_link_libraries(plugin_bad_abi PRIVATE minecraftc_sdk)
        target_compile_definitions(plugin_bad_abi PRIVATE MC_BAD_ABI=1)
        add_library(plugin_bad_entry MODULE tests/PluginBadEntry.cpp)
        target_link_libraries(plugin_bad_entry PRIVATE minecraftc_sdk)
        add_library(plugin_callback_fault MODULE tests/PluginBadEntry.cpp)
        target_link_libraries(plugin_callback_fault PRIVATE minecraftc_sdk)
        target_compile_definitions(plugin_callback_fault PRIVATE MC_CALLBACK_FAULT=1)
        add_dependencies(plugin_tests plugin_native_demo plugin_bad_abi plugin_bad_entry plugin_callback_fault)
        minecraftc_add_test(NAME plugin_loader_content_events_and_persistence COMMAND plugin_tests
            $<TARGET_FILE:plugin_native_demo> $<TARGET_FILE:plugin_bad_abi> $<TARGET_FILE:plugin_bad_entry> $<TARGET_FILE:plugin_callback_fault>)
    else()
        minecraftc_add_test(NAME plugin_loader_content_events_and_persistence COMMAND plugin_tests)
    endif()

    add_executable(save_store_tests tests/SaveStoreTests.cpp)
    target_link_libraries(save_store_tests PRIVATE
        minecraftc_assets minecraftc_json minecraftc_sdk glm::glm minecraftc_platform
        minecraftc_simulation_common minecraftc_heaven minecraftc_content_registry
        minecraftc_atlas_bindings)
    target_include_directories(save_store_tests PRIVATE src external/FastNoiseLite)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(save_store_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME survival_save_roundtrip_and_corruption COMMAND save_store_tests)

    add_executable(fishing_tests tests/FishingTests.cpp)
    target_link_libraries(fishing_tests PRIVATE
        minecraftc_assets minecraftc_sdk glm::glm minecraftc_simulation_common
        minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(fishing_tests PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(fishing_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME classic_fishing_simulation_and_rules COMMAND fishing_tests)

    add_executable(survival_rules_tests tests/SurvivalRulesTests.cpp)
    target_link_libraries(survival_rules_tests PRIVATE
        minecraftc_assets minecraftc_json minecraftc_sdk glm::glm minecraftc_simulation_common
        minecraftc_heaven minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(survival_rules_tests PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(survival_rules_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME survival_mining_crafting_and_smelting COMMAND survival_rules_tests)
    minecraftc_add_test(NAME heaven_p1_independent_content_reference
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_heaven_content_reference.py
                $<TARGET_FILE:survival_rules_tests>)

    add_executable(fluid_logic_tests tests/FluidLogicTests.cpp)
    target_link_libraries(fluid_logic_tests PRIVATE
        minecraftc_assets minecraftc_sdk glm::glm minecraftc_simulation_common
        minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(fluid_logic_tests PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(fluid_logic_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME java_26_2_fluid_logic COMMAND fluid_logic_tests)

    add_executable(survival_stats_tests tests/SurvivalStatsTests.cpp)
    target_link_libraries(survival_stats_tests PRIVATE
        minecraftc_assets minecraftc_sdk glm::glm minecraftc_simulation_common
        minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(survival_stats_tests PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(survival_stats_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME survival_health_hunger_and_starvation COMMAND survival_stats_tests)

    add_executable(combat_rules_tests tests/CombatRulesTests.cpp)
    target_link_libraries(combat_rules_tests PRIVATE
        minecraftc_assets minecraftc_sdk glm::glm minecraftc_simulation_common
        minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(combat_rules_tests PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(combat_rules_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME java_1_9_combat_rules COMMAND combat_rules_tests)

    add_executable(world_catalog_tests tests/WorldCatalogTests.cpp)
    target_link_libraries(world_catalog_tests PRIVATE
        minecraftc_assets minecraftc_json minecraftc_sdk glm::glm minecraftc_platform
        minecraftc_simulation_common minecraftc_heaven minecraftc_content_registry
        minecraftc_atlas_bindings)
    target_include_directories(world_catalog_tests PRIVATE src external/FastNoiseLite)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(world_catalog_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME survival_multiple_world_catalog COMMAND world_catalog_tests)
    minecraftc_add_test(NAME world_recovery_transactions COMMAND world_catalog_tests --recovery)

    add_executable(command_tests tests/CommandTests.cpp)
    target_link_libraries(command_tests PRIVATE
        minecraftc_core minecraftc_assets minecraftc_sdk glm::glm minecraftc_platform
        minecraftc_simulation_common minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(command_tests PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(command_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME cheat_gamemode_command_parsing COMMAND command_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/java-1.21.11-gamerules.tsv)

    add_executable(weather_logic_tests tests/WeatherTests.cpp)
    target_include_directories(weather_logic_tests PRIVATE src)
    target_link_libraries(weather_logic_tests PRIVATE
        glm::glm minecraftc_simulation_common)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(weather_logic_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME weather_cycle_precipitation_and_commands COMMAND weather_logic_tests)

    add_executable(ground_navigation_tests tests/GroundNavigationTests.cpp)
    target_link_libraries(ground_navigation_tests PRIVATE
        minecraftc_assets minecraftc_sdk glm::glm minecraftc_simulation_common
        minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(ground_navigation_tests PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ground_navigation_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME entity_ground_navigation COMMAND ground_navigation_tests)

    add_executable(entity_logic_tests tests/EntityLogicTests.cpp)
    target_link_libraries(entity_logic_tests PRIVATE
        minecraftc_assets minecraftc_sdk glm::glm minecraftc_simulation_common
        minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(entity_logic_tests PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(entity_logic_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME survival_entity_spawning_despawn_and_sweep COMMAND entity_logic_tests)

    add_executable(voxel_raycast_tests tests/VoxelRaycastTests.cpp)
    target_include_directories(voxel_raycast_tests PRIVATE src)
    target_link_libraries(voxel_raycast_tests PRIVATE glm::glm)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(voxel_raycast_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME world_voxel_raycast_distance_and_obstruction COMMAND voxel_raycast_tests)

    add_executable(chunk_index_tests tests/ChunkIndexTests.cpp)
    target_include_directories(chunk_index_tests PRIVATE src)
    target_link_libraries(chunk_index_tests PRIVATE glm::glm)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(chunk_index_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME world_chunk_override_index_decode COMMAND chunk_index_tests)

    add_executable(entity_model_registry_tests tests/EntityModelRegistryTests.cpp)
    target_link_libraries(entity_model_registry_tests PRIVATE
        minecraftc_log minecraftc_image_decoder minecraftc_model_animation
        minecraftc_animation_graph minecraftc_gltf minecraftc_assets minecraftc_json
        minecraftc_sdk glm::glm minecraftc_platform minecraftc_render_data
        minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(entity_model_registry_tests PRIVATE
        src external external/cgltf external/stb)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(entity_model_registry_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(entity_model_registry_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    minecraftc_add_test(NAME entity_model_registry_sharing_and_fallback
             COMMAND entity_model_registry_tests)

    add_custom_target(entity_model_assets
        COMMAND ${CMAKE_COMMAND} -E env
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_entity_models.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    minecraftc_add_test(NAME entity_model_asset_contract
        COMMAND ${CMAKE_COMMAND} -E env
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_entity_models.py)

    add_executable(thread_pool_tests tests/ThreadPoolTests.cpp)
    target_link_libraries(thread_pool_tests PRIVATE
        minecraftc_log minecraftc_threading)
    target_include_directories(thread_pool_tests PRIVATE src)
    target_link_libraries(thread_pool_tests PRIVATE minecraftc_platform Threads::Threads)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(thread_pool_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME thread_pool_priority_idle_and_shutdown COMMAND thread_pool_tests)
    set_tests_properties(thread_pool_priority_idle_and_shutdown PROPERTIES TIMEOUT 15)

    add_executable(lighting_logic_tests tests/LightingLogicTests.cpp)
    target_link_libraries(lighting_logic_tests PRIVATE
        minecraftc_assets minecraftc_sdk glm::glm minecraftc_simulation
        minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(lighting_logic_tests PRIVATE src external/FastNoiseLite)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(lighting_logic_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME rendering_cross_chunk_block_light COMMAND lighting_logic_tests)

    add_executable(client_input_tests tests/ClientInputTests.cpp)
    target_link_libraries(client_input_tests PRIVATE
        minecraftc_log minecraftc_core minecraftc_assets minecraftc_sdk glm::glm
        ${MINECRAFTC_SDL_TARGET} minecraftc_platform minecraftc_simulation_common minecraftc_text
        minecraftc_platform_sdl minecraftc_ui minecraftc_content_registry
        minecraftc_atlas_bindings)
    target_include_directories(client_input_tests PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(client_input_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME client_settings_bindings_and_inventory_gestures COMMAND client_input_tests)
    # Exercise the inherited audio-driver environment used by Linux CI.
    set_tests_properties(client_settings_bindings_and_inventory_gestures PROPERTIES
        ENVIRONMENT "SDL_AUDIODRIVER=dummy")

    add_executable(input_routing_tests tests/InputRoutingTests.cpp)
    target_link_libraries(input_routing_tests PRIVATE
        minecraftc_log minecraftc_core glm::glm ${MINECRAFTC_SDL_TARGET} minecraftc_platform
        minecraftc_text minecraftc_platform_sdl minecraftc_ui)
    target_include_directories(input_routing_tests PRIVATE src)
    target_compile_definitions(input_routing_tests PRIVATE
        MINECRAFTC_VERSION_STRING="${MINECRAFTC_VERSION_DISPLAY}")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(input_routing_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(input_routing_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    minecraftc_add_test(NAME application_input_routing COMMAND input_routing_tests)

    add_executable(session_flow_tests tests/SessionFlowTests.cpp)
    target_link_libraries(session_flow_tests PRIVATE
        minecraftc_log minecraftc_core minecraftc_threading minecraftc_image_decoder
        minecraftc_model_animation minecraftc_animation_graph minecraftc_gltf minecraftc_assets
        minecraftc_json minecraftc_sdk glm::glm ${MINECRAFTC_SDL_TARGET} minecraftc_platform
        minecraftc_app_session_test minecraftc_plugins minecraftc_network minecraftc_render_data
        minecraftc_platform_sdl minecraftc_heaven minecraftc_content_registry
        minecraftc_atlas_bindings minecraftc_plugin_runtime)
    target_include_directories(session_flow_tests PRIVATE
        src external external/FastNoiseLite external/cgltf external/stb)
    target_compile_definitions(session_flow_tests PRIVATE
        MINECRAFTC_VERSION_STRING="${MINECRAFTC_VERSION_DISPLAY}")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(session_flow_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(session_flow_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    minecraftc_add_test(NAME redstone_lan_atomic_motion COMMAND session_flow_tests --redstone-lan-tests)
    set_tests_properties(redstone_lan_atomic_motion PROPERTIES LABELS "fast;integration" TIMEOUT 180)
    minecraftc_add_test(NAME lan_game_replica COMMAND session_flow_tests --lan-replica-tests ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    set_tests_properties(lan_game_replica PROPERTIES TIMEOUT 180 LABELS "slow;integration")
    add_executable(lod_subscription_tests tests/LodSubscriptionTests.cpp)
    target_link_libraries(lod_subscription_tests PRIVATE
        minecraftc_sdk glm::glm minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(lod_subscription_tests PRIVATE src)
    minecraftc_add_test(NAME lan_lod_subscription_replacement COMMAND lod_subscription_tests)
    set_tests_properties(lan_lod_subscription_replacement PROPERTIES TIMEOUT 15)
    minecraftc_add_test(NAME lan_gameplay_authority COMMAND session_flow_tests --lan-authority-tests)
    set_tests_properties(lan_gameplay_authority PROPERTIES TIMEOUT 30)
    if(NOT ANDROID AND NOT IOS)
        add_library(plugin_legacy_fixture MODULE tests/PluginLegacyEntry.cpp)
        target_link_libraries(plugin_legacy_fixture PRIVATE minecraftc_sdk)
        add_library(plugin_lan_fixture MODULE tests/PluginLanEntry.cpp)
        target_link_libraries(plugin_lan_fixture PRIVATE minecraftc_sdk)
        add_dependencies(session_flow_tests plugin_legacy_fixture plugin_lan_fixture)
        minecraftc_add_test(NAME plugin_lan_multiplayer COMMAND session_flow_tests --plugin-lan-tests
            ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE:plugin_legacy_fixture> $<TARGET_FILE:plugin_lan_fixture>)
    endif()
    minecraftc_add_test(NAME lan_multiplayer_simulation COMMAND session_flow_tests --lan-simulation-tests ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    minecraftc_add_test(NAME game_session_flow COMMAND session_flow_tests)
    minecraftc_add_test(NAME experience_integration COMMAND session_flow_tests --experience-tests ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    minecraftc_add_test(NAME enchantment_integration COMMAND session_flow_tests --enchantment-tests ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    set_tests_properties(enchantment_integration PROPERTIES TIMEOUT 120)
    set_tests_properties(experience_integration PROPERTIES TIMEOUT 60)
    target_sources(session_flow_tests PRIVATE
        tests/reference/heaven_v9/FrozenParticleSystem.cpp
        tests/reference/heaven_v9/FrozenHeavenV9.cpp)
    minecraftc_add_test(NAME heaven_p1_independent_particle_reference
        COMMAND session_flow_tests --heaven-particle-migration)
    if(NOT ANDROID AND NOT IOS)
        foreach(p1_fixture_kind RANGE 0 2)
            add_library(plugin_heaven_p1_${p1_fixture_kind} MODULE tests/HeavenP1Fixture.cpp)
            target_include_directories(plugin_heaven_p1_${p1_fixture_kind} PRIVATE sdk)
            target_compile_definitions(plugin_heaven_p1_${p1_fixture_kind} PRIVATE P1_FIXTURE_KIND=${p1_fixture_kind})
            set_target_properties(plugin_heaven_p1_${p1_fixture_kind} PROPERTIES PREFIX "")
            add_dependencies(session_flow_tests plugin_heaven_p1_${p1_fixture_kind})
        endforeach()
        minecraftc_add_test(NAME heaven_p1_native_registration_and_rollback COMMAND session_flow_tests --heaven-p1-tests
            ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE:plugin_heaven_p1_0>
            $<TARGET_FILE:plugin_heaven_p1_1> $<TARGET_FILE:plugin_heaven_p1_2>)
        add_library(plugin_worldgen_v2_bad_registration MODULE examples/plugins/worldgen_v2_demo/plugin.cpp)
        target_include_directories(plugin_worldgen_v2_bad_registration PRIVATE sdk)
        target_compile_definitions(plugin_worldgen_v2_bad_registration PRIVATE MC_WORLDGEN_BAD_REGISTRATION=1)
        add_dependencies(session_flow_tests plugin_worldgen_v2_demo plugin_worldgen_v2_bad_registration)
        minecraftc_add_test(NAME plugin_worldgen_v2_batch_and_lod COMMAND session_flow_tests --worldgen-v2-tests
            ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE:plugin_worldgen_v2_demo> $<TARGET_FILE:plugin_worldgen_v2_bad_registration>)
        set_tests_properties(plugin_worldgen_v2_batch_and_lod PROPERTIES TIMEOUT 180)
        add_library(plugin_policy_v2_bad MODULE examples/plugins/worldgen_policy_v2/plugin.cpp)
        target_include_directories(plugin_policy_v2_bad PRIVATE sdk)
        target_compile_definitions(plugin_policy_v2_bad PRIVATE MC_POLICY_BAD_REGISTRATION=1)
        set_target_properties(plugin_policy_v2_bad PROPERTIES PREFIX "")
        add_dependencies(session_flow_tests plugin_worldgen_policy_v2 plugin_policy_v2_bad)
        minecraftc_add_test(NAME plugin_seeded_spawn_and_structure_v2 COMMAND session_flow_tests --policy-v2-tests
            ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE:plugin_worldgen_policy_v2> $<TARGET_FILE:plugin_policy_v2_bad>)
        set_tests_properties(plugin_seeded_spawn_and_structure_v2 PROPERTIES TIMEOUT 180)
        add_dependencies(session_flow_tests plugin_entity_demo plugin_terrain_demo plugin_dimension_demo)
        add_dependencies(vulkan_ui_smoke plugin_render_demo)
        if(MINECRAFTC_VULKAN_TESTS)
            set_property(TARGET vulkan_ui_smoke PROPERTY EXCLUDE_FROM_ALL FALSE)
            minecraftc_add_test(NAME redstone_vulkan_readback COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/run_vulkan_test.py $<TARGET_FILE:vulkan_ui_smoke>
                ${CMAKE_SOURCE_DIR}/assets redstone)
            set_tests_properties(redstone_vulkan_readback PROPERTIES
                LABELS vulkan SKIP_RETURN_CODE 77 TIMEOUT 240 RESOURCE_LOCK vulkan_device)
            minecraftc_add_test(NAME enchanting_vulkan_readback COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/run_vulkan_test.py $<TARGET_FILE:vulkan_ui_smoke>
                ${CMAKE_SOURCE_DIR}/assets enchanting)
            set_tests_properties(enchanting_vulkan_readback PROPERTIES
                LABELS vulkan SKIP_RETURN_CODE 77 TIMEOUT 240 RESOURCE_LOCK vulkan_device)
            minecraftc_add_test(NAME experience_vulkan_readback COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/run_vulkan_test.py $<TARGET_FILE:vulkan_ui_smoke>
                ${CMAKE_SOURCE_DIR}/assets experience)
            set_tests_properties(experience_vulkan_readback PROPERTIES
                LABELS vulkan SKIP_RETURN_CODE 77 TIMEOUT 240 RESOURCE_LOCK vulkan_device)
            minecraftc_add_test(NAME natural_texture_vulkan_readback COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/run_vulkan_test.py $<TARGET_FILE:vulkan_ui_smoke>
                ${CMAKE_SOURCE_DIR}/assets natural_textures)
            set_tests_properties(natural_texture_vulkan_readback PROPERTIES
                LABELS vulkan SKIP_RETURN_CODE 77 TIMEOUT 240 RESOURCE_LOCK vulkan_device)
            minecraftc_add_test(NAME content_package_vulkan_readback COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/test_content_package.py
                --plugin-tests $<TARGET_FILE:plugin_tests>
                --builtin-stage "${CMAKE_BINARY_DIR}/content-assets/plugins/package_demo"
                --vulkan-smoke $<TARGET_FILE:vulkan_ui_smoke>
                ContentPackageTests.test_native_build_and_runtime)
            set_tests_properties(content_package_vulkan_readback PROPERTIES
                LABELS vulkan SKIP_RETURN_CODE 77 TIMEOUT 240 RESOURCE_LOCK vulkan_device)
            minecraftc_add_test(NAME plugin_vulkan_extensions_readback COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/run_vulkan_test.py $<TARGET_FILE:vulkan_ui_smoke>
                ${CMAKE_SOURCE_DIR}/assets plugins $<TARGET_FILE:plugin_render_demo>)
            minecraftc_add_test(NAME plugin_vulkan_capabilities_readback COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/run_vulkan_test.py $<TARGET_FILE:vulkan_ui_smoke>
                ${CMAKE_SOURCE_DIR}/assets capabilities)
            foreach(heaven_scene heaven_model heaven_off)
                minecraftc_add_test(NAME plugin_vulkan_${heaven_scene}_readback COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_SOURCE_DIR}/tests/run_vulkan_test.py $<TARGET_FILE:vulkan_ui_smoke>
                    ${CMAKE_SOURCE_DIR}/assets ${heaven_scene})
                set_tests_properties(plugin_vulkan_${heaven_scene}_readback PROPERTIES
                    LABELS vulkan SKIP_RETURN_CODE 77 TIMEOUT 240 RESOURCE_LOCK vulkan_device)
            endforeach()
            set_tests_properties(plugin_vulkan_capabilities_readback PROPERTIES
                LABELS vulkan SKIP_RETURN_CODE 77 TIMEOUT 240 RESOURCE_LOCK vulkan_device)
            set_tests_properties(plugin_vulkan_extensions_readback PROPERTIES
                LABELS vulkan SKIP_RETURN_CODE 77 TIMEOUT 240 RESOURCE_LOCK vulkan_device)
        endif()
        add_dependencies(session_flow_tests plugin_block_shapes_demo plugin_capability_native)
        minecraftc_add_test(NAME plugin_heaven_remaining_p0 COMMAND session_flow_tests --remaining-p0-tests
            ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE:plugin_capability_native>)
        set_tests_properties(plugin_heaven_remaining_p0 PROPERTIES TIMEOUT 180)
        minecraftc_add_test(NAME plugin_gameplay_presentation_capabilities COMMAND session_flow_tests --capability-tests
            ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE:plugin_capability_native>)
        set_tests_properties(plugin_gameplay_presentation_capabilities PROPERTIES TIMEOUT 180)
        minecraftc_add_test(NAME plugin_block_shapes_registration_and_runtime COMMAND session_flow_tests --shape-tests
            ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE:plugin_block_shapes_demo>)
        set_tests_properties(plugin_block_shapes_registration_and_runtime PROPERTIES TIMEOUT 180)
        minecraftc_add_test(NAME plugin_dimension_registration_and_runtime COMMAND session_flow_tests --dimension-tests
            ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE:plugin_dimension_demo>)
        set_tests_properties(plugin_dimension_registration_and_runtime PROPERTIES TIMEOUT 180)
        minecraftc_add_test(NAME plugin_entity_worldgen_extensions COMMAND session_flow_tests --extension-tests
            ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE:plugin_entity_demo> $<TARGET_FILE:plugin_terrain_demo>)
    endif()
    minecraftc_add_test(NAME plugin_game_session_integration COMMAND session_flow_tests --plugin-tests ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    minecraftc_add_test(NAME gamerule_integration COMMAND session_flow_tests --gamerule-tests ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    set_tests_properties(gamerule_integration PROPERTIES TIMEOUT 120)
    minecraftc_add_test(NAME entity_ai_integration COMMAND session_flow_tests --ai-tests ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    minecraftc_add_test(NAME entity_combat_timing COMMAND session_flow_tests --combat-timing-tests ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    set_tests_properties(entity_combat_timing PROPERTIES TIMEOUT 120)
    minecraftc_add_test(NAME projectile_flight_integration COMMAND session_flow_tests --projectile-tests ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    set_tests_properties(projectile_flight_integration PROPERTIES TIMEOUT 120)
    set_tests_properties(entity_ai_integration PROPERTIES TIMEOUT 120)
    # This test is normally sub-second; a generous ceiling turns a future
    # deadlock into a fast, actionable CTest timeout instead of a stuck job.
    set_tests_properties(game_session_flow PROPERTIES TIMEOUT 120)

    add_executable(chunk_io_queue_tests tests/ChunkIoQueueTests.cpp)
    target_link_libraries(chunk_io_queue_tests PRIVATE minecraftc_sdk)
    target_include_directories(chunk_io_queue_tests PRIVATE src)
    target_link_libraries(chunk_io_queue_tests PRIVATE glm::glm Threads::Threads)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(chunk_io_queue_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(chunk_io_queue_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    minecraftc_add_test(NAME chunk_io_queue_startup_drain_and_shutdown COMMAND chunk_io_queue_tests)
    set_tests_properties(chunk_io_queue_startup_drain_and_shutdown PROPERTIES TIMEOUT 30)

    add_executable(application_flow_tests tests/ApplicationFlowTests.cpp)
    target_link_libraries(application_flow_tests PRIVATE
        minecraftc_log minecraftc_core minecraftc_threading minecraftc_image_decoder
        minecraftc_model_animation minecraftc_animation_graph minecraftc_gltf minecraftc_assets
        minecraftc_json minecraftc_sdk glm::glm ${MINECRAFTC_SDL_TARGET} minecraftc_platform
        minecraftc_app_session minecraftc_network minecraftc_text minecraftc_render_data
        minecraftc_platform_sdl minecraftc_ui minecraftc_heaven minecraftc_content_registry
        minecraftc_atlas_bindings minecraftc_plugin_runtime)
    target_include_directories(application_flow_tests PRIVATE
        src external external/FastNoiseLite external/cgltf external/stb)
    target_compile_definitions(application_flow_tests PRIVATE
        MINECRAFTC_VERSION_STRING="${MINECRAFTC_VERSION_DISPLAY}")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(application_flow_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(application_flow_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    minecraftc_add_test(NAME application_flow_state_transitions COMMAND application_flow_tests)
    minecraftc_add_test(NAME dropped_item_rendering COMMAND application_flow_tests
        --dropped-item-tests ${CMAKE_CURRENT_SOURCE_DIR}/assets)

    add_executable(world_orchestration_tests tests/WorldOrchestrationTests.cpp)
    target_link_libraries(world_orchestration_tests PRIVATE
        minecraftc_log minecraftc_threading minecraftc_assets minecraftc_json minecraftc_sdk
        glm::glm ${MINECRAFTC_SDL_TARGET} minecraftc_platform minecraftc_simulation_test
        minecraftc_heaven minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(world_orchestration_tests PRIVATE
        src external external/FastNoiseLite)
    target_compile_definitions(world_orchestration_tests PRIVATE
        MINECRAFTC_VERSION_STRING="${MINECRAFTC_VERSION_DISPLAY}")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(world_orchestration_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(world_orchestration_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    minecraftc_add_test(NAME world_orchestration_state_transitions COMMAND world_orchestration_tests)
    minecraftc_add_test(NAME redstone_world_circuits COMMAND world_orchestration_tests --redstone-tests)
    set_tests_properties(redstone_world_circuits PROPERTIES TIMEOUT 180)
    foreach(scenario IN ITEMS comparators lamps buttons pistons)
        minecraftc_add_test(NAME redstone_tick_${scenario} COMMAND world_orchestration_tests --redstone-budget-tests ${scenario})
        set_tests_properties(redstone_tick_${scenario} PROPERTIES LABELS "fast;integration" TIMEOUT 180)
    endforeach()
    # The benchmark legs allow 60s each on heavily loaded CI runners; keep the
    # whole orchestration test well under the job timeout with diagnostics.
    set_tests_properties(world_orchestration_state_transitions PROPERTIES TIMEOUT 600)

    add_executable(localization_tests tests/LocalizationTests.cpp)
    target_link_libraries(localization_tests PRIVATE
        minecraftc_core minecraftc_assets minecraftc_json minecraftc_sdk glm::glm
        ${MINECRAFTC_SDL_TARGET} minecraftc_simulation_common minecraftc_text minecraftc_heaven
        minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(localization_tests PRIVATE
        src external external/stb external/nlohmann)
    target_compile_definitions(localization_tests PRIVATE
        MINECRAFTC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(localization_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(localization_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    minecraftc_add_test(NAME localization_resources_utf8_and_cjk_font
             COMMAND localization_tests)

    add_executable(survival_integration_tests tests/SurvivalIntegrationTests.cpp)
    target_link_libraries(survival_integration_tests PRIVATE
        minecraftc_assets minecraftc_json minecraftc_sdk glm::glm minecraftc_platform
        minecraftc_simulation_common minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(survival_integration_tests PRIVATE src external/FastNoiseLite)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(survival_integration_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minecraftc_add_test(NAME survival_progression_persistence_and_recovery COMMAND survival_integration_tests)
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(texture_generator
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/texture_generator.py
                --generate --validate --build-atlas --build-items-atlas
                --build-entity-atlas --build-entity-skins
                --output ${CMAKE_SOURCE_DIR}/assets/textures/generated
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/generate_entity_models.py
                --output ${CMAKE_SOURCE_DIR}/assets/models/entities
                --player-output ${CMAKE_SOURCE_DIR}/assets/models/player
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating deterministic textures and synchronized model skins")
    add_custom_target(texture_preview
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/texture_generator.py
                --preview --candidate-count 3
                --output ${CMAKE_SOURCE_DIR}/assets/textures/generated
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Building bright-comfortable texture previews and visual report")
endif()

if(BUILD_TESTING AND NOT ANDROID AND NOT IOS)
    add_executable(network_tests tests/NetworkTests.cpp)
    target_link_libraries(network_tests PRIVATE
        minecraftc_assets minecraftc_json minecraftc_sdk minecraftc_platform glm::glm
        minecraftc_simulation_common minecraftc_network minecraftc_heaven
        minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(network_tests PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(network_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(network_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    minecraftc_add_test(NAME lan_protocol_and_tcp COMMAND network_tests)
endif()

if(BUILD_TESTING AND NOT ANDROID AND NOT IOS)
    add_executable(inventory_transaction_tests tests/InventoryTransactionTests.cpp)
    target_link_libraries(inventory_transaction_tests PRIVATE
        minecraftc_assets minecraftc_sdk glm::glm minecraftc_simulation_common
        minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(inventory_transaction_tests PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(inventory_transaction_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(inventory_transaction_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    minecraftc_add_test(NAME authoritative_inventory_transactions COMMAND inventory_transaction_tests)
endif()

if(BUILD_TESTING AND NOT ANDROID AND NOT IOS)
    add_executable(chunk_protocol_tests tests/ChunkProtocolTests.cpp)
    target_link_libraries(chunk_protocol_tests PRIVATE
        minecraftc_json minecraftc_sdk glm::glm minecraftc_network minecraftc_heaven
        minecraftc_content_registry minecraftc_atlas_bindings)
    target_include_directories(chunk_protocol_tests PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(chunk_protocol_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(chunk_protocol_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    minecraftc_add_test(NAME lan_chunk_protocol COMMAND chunk_protocol_tests)
endif()

if(BUILD_TESTING AND NOT ANDROID AND NOT IOS AND NOT APPLE)
    add_executable(lan_discovery_tests tests/LanDiscoveryTests.cpp)
    target_link_libraries(lan_discovery_tests PRIVATE minecraftc_platform)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(lan_discovery_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(lan_discovery_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    minecraftc_add_test(NAME lan_dns_sd_discovery COMMAND lan_discovery_tests)
    set_tests_properties(lan_dns_sd_discovery PROPERTIES SKIP_RETURN_CODE 77)
endif()

if(BUILD_TESTING)
    minecraftc_add_test(NAME nether_native_mobs_barter_riding_and_persistence COMMAND session_flow_tests --nether-mob-tests ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    minecraftc_add_test(NAME redstone_slime_survival_and_replication COMMAND session_flow_tests --redstone-mob-tests ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    minecraftc_add_test(NAME nether_native_session_portals_and_anchors COMMAND session_flow_tests --nether-session-tests)
    minecraftc_add_test(NAME nether_native_without_plugins COMMAND session_flow_tests --nether-mob-tests-no-plugins ${CMAKE_CURRENT_SOURCE_DIR}/assets)
endif()

if(BUILD_TESTING AND NOT ANDROID AND NOT IOS)
    add_executable(enchantment_component_tests tests/EnchantmentComponentTests.cpp)
    target_include_directories(enchantment_component_tests PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(enchantment_component_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(enchantment_component_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    minecraftc_add_test(NAME enchantment_components COMMAND enchantment_component_tests)
    add_executable(redstone_tests tests/RedstoneTests.cpp)
    target_include_directories(redstone_tests PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(redstone_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(redstone_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    minecraftc_add_test(NAME redstone_rules_and_scheduling COMMAND redstone_tests)
    add_executable(redstone_ore_tests tests/RedstoneOreTests.cpp)
    target_include_directories(redstone_ore_tests PRIVATE src external/FastNoiseLite)
    target_link_libraries(redstone_ore_tests PRIVATE
        glm::glm minecraftc_terrain)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(redstone_ore_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(redstone_ore_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    minecraftc_add_test(NAME redstone_ore_determinism COMMAND redstone_ore_tests)
endif()

if(BUILD_TESTING)
    set_tests_properties(
        texture_recipes_sequences_and_workbench
        texture_generator_determinism_tiling_and_atlas
        music_assets_determinism_format_and_loop
        natural_texture_assets
        PROPERTIES LABELS "slow;assets" TIMEOUT 900)
    set_tests_properties(overworld_structure_placement_and_reservations
        overworld_ore_hosts_biomes_and_seed_variation
        worldgen_determinism_boundaries_and_distribution
        heaven_worldgen_determinism_boundaries_and_distribution
        heaven_p1_independent_migration_reference
        PROPERTIES LABELS "slow;worldgen" TIMEOUT 1200)
    set_tests_properties(world_orchestration_state_transitions
        PROPERTIES LABELS "slow;integration" TIMEOUT 600)
    set_tests_properties(plugin_worldgen_v2_batch_and_lod
        PROPERTIES LABELS "slow;integration" TIMEOUT 180)
    get_property(minecraftc_tests DIRECTORY PROPERTY TESTS)
    foreach(test IN LISTS minecraftc_tests)
        get_test_property(${test} LABELS test_labels)
        if("vulkan" IN_LIST test_labels)
            set_property(TEST ${test} APPEND PROPERTY LABELS "slow;integration")
            # Start this device-locked serial chain immediately on fresh CI
            # builds, overlapping it with the independent CPU tests.
            set_property(TEST ${test} PROPERTY COST 1000)
        endif()
    endforeach()
    # Seed fresh runners with approximate Linux timings (seconds). CTest has no
    # duration history on a new checkout; registration order leaves long tests
    # and the Vulkan chain queued behind short tests, extending the idle tail.
    set_tests_properties(music_assets_determinism_format_and_loop PROPERTIES COST 303)
    set_tests_properties(texture_generator_determinism_tiling_and_atlas PROPERTIES COST 259)
    set_tests_properties(worldgen_determinism_boundaries_and_distribution PROPERTIES COST 218)
    set_tests_properties(overworld_structure_placement_and_reservations PROPERTIES COST 180)
    set_tests_properties(texture_recipes_sequences_and_workbench PROPERTIES COST 115)
    set_tests_properties(world_orchestration_state_transitions PROPERTIES COST 110)
    set_tests_properties(application_flow_state_transitions PROPERTIES COST 54)
endif()
