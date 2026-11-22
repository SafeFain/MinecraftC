option(MINECRAFTC_EXPERIMENTAL_CXX_MODULES
    "Use experimental C++20 named modules in selected MinecraftC sources" OFF)

set(minecraftc_named_module_sources
    src/core/AssetStore.cppm
    src/core/RuntimeClock.cppm
    src/core/Utf8Path.cppm
    src/core/Window.cppm
    src/world/WorldGenContext.cppm)

if(MINECRAFTC_EXPERIMENTAL_CXX_MODULES)
    if(NOT CMAKE_GENERATOR MATCHES "^Ninja( Multi-Config)?$")
        message(FATAL_ERROR
            "MINECRAFTC_EXPERIMENTAL_CXX_MODULES requires a Ninja generator "
            "with Ninja 1.11 or newer.")
    endif()
    if(WIN32)
        if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" OR MSVC_VERSION LESS 1934
           OR NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "^(x64|ARM64)$")
            message(FATAL_ERROR
                "Windows named modules require MSVC 19.34 or newer targeting x64 or ARM64.")
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
            message(FATAL_ERROR "Linux named modules with GCC require GCC 14 or newer.")
        endif()
    else()
        if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
           OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
            message(FATAL_ERROR
                "This named-module build requires LLVM Clang 16 or newer "
                "with matching clang-scan-deps (use upstream LLVM on Apple platforms).")
        endif()
        if(NOT EXISTS "${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}")
            message(FATAL_ERROR
                "clang-scan-deps was not found. Install the scanner matching clang++, "
                "or set CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS to its absolute path.")
        endif()
    endif()
    if(APPLE)
        list(LENGTH CMAKE_OSX_ARCHITECTURES named_modules_arch_count)
        if(named_modules_arch_count GREATER 1)
            message(FATAL_ERROR
                "Apple named modules require a separate build directory for each architecture.")
        endif()
    endif()

    add_library(minecraftc_named_modules STATIC)
    target_include_directories(minecraftc_named_modules PRIVATE src)
    if(MSVC)
        target_compile_options(minecraftc_named_modules PRIVATE /W4 /permissive-)
    else()
        target_compile_options(minecraftc_named_modules PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    target_sources(minecraftc_named_modules PUBLIC
        FILE_SET named_modules TYPE CXX_MODULES
        FILES ${minecraftc_named_module_sources})
else()
    # Keep interface source ownership visible without adding build rules.
    add_library(minecraftc_named_modules INTERFACE ${minecraftc_named_module_sources})
endif()

function(minecraftc_enable_module_imports target)
    if(MINECRAFTC_EXPERIMENTAL_CXX_MODULES)
        set_property(SOURCE ${ARGN} PROPERTY CXX_SCAN_FOR_MODULES ON)
        set_property(SOURCE ${ARGN} APPEND PROPERTY COMPILE_DEFINITIONS
            MINECRAFTC_USE_NAMED_MODULES=1)
        target_link_libraries(${target} PRIVATE minecraftc_named_modules)
    endif()
endfunction()
