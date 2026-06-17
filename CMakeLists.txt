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
