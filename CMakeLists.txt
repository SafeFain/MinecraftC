cmake_minimum_required(VERSION 3.16)
project(MinecraftC LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# ── Dependencies ───────────────────────────────────────────────────────

find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(OpenGL REQUIRED)

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

add_executable(minecraftc ${SOURCES})
target_include_directories(minecraftc PRIVATE
    src
    external/glad/include
    external/FastNoiseLite
    external/stb
)
# Prevent GLFW from including GL/gl.h — we use our own GL loader
target_compile_definitions(minecraftc PRIVATE GLFW_INCLUDE_NONE)

# ── Debug / logging compile-time gating ─────────────────────────────────
# LOG_COMPILE_LEVEL strips log macros below the given level:
#   0=Trace  1=Debug  2=Info  3=Warn  4=Error  5=Fatal
target_compile_definitions(minecraftc PRIVATE
    $<$<CONFIG:Debug>:-DLOG_COMPILE_LEVEL=0>
    $<$<CONFIG:Release>:-DLOG_COMPILE_LEVEL=2>
)
target_link_libraries(minecraftc PRIVATE
    glfw
    glm::glm
    OpenGL::GL
    glad
)

# ── Compiler warnings ──────────────────────────────────────────────────

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(minecraftc PRIVATE -Wall -Wextra -Wpedantic)

    # Release build: optimization flags
    target_compile_options(minecraftc PRIVATE
        $<$<CONFIG:Release>:-O2 -march=native -DNDEBUG>
        $<$<CONFIG:Release>:-flto>
    )
    target_link_options(minecraftc PRIVATE
        $<$<CONFIG:Release>:-flto>
    )

    # Debug build: sanitizers for catching UB and memory errors
    target_compile_options(minecraftc PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined -g -O0>
    )
    target_link_options(minecraftc PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined -rdynamic>
    )
endif()

# ── Install ────────────────────────────────────────────────────────────

install(TARGETS minecraftc DESTINATION bin)

# ── Deterministic world-generation tests ───────────────────────────────────
include(CTest)
if(BUILD_TESTING)
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
    target_link_libraries(save_store_tests PRIVATE glm::glm)
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
    target_link_libraries(world_catalog_tests PRIVATE glm::glm)
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

    add_executable(entity_logic_tests tests/EntityLogicTests.cpp)
    target_include_directories(entity_logic_tests PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(entity_logic_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME survival_entity_spawning_despawn_and_sweep COMMAND entity_logic_tests)

    add_executable(lighting_logic_tests tests/LightingLogicTests.cpp)
    target_include_directories(lighting_logic_tests PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(lighting_logic_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME rendering_cross_chunk_block_light COMMAND lighting_logic_tests)
endif()
