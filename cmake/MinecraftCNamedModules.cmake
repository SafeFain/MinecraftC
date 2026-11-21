option(MINECRAFTC_EXPERIMENTAL_CXX_MODULES
    "Use experimental C++20 named modules in selected Windows x64 sources" OFF)

set(minecraftc_named_module_sources
    src/core/RuntimeClock.cppm
    src/world/WorldGenContext.cppm)

if(MINECRAFTC_EXPERIMENTAL_CXX_MODULES)
    if(NOT WIN32 OR CMAKE_CROSSCOMPILING
       OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"
       OR NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64"
       OR MSVC_VERSION LESS 1934 OR NOT CMAKE_GENERATOR STREQUAL "Ninja")
        message(FATAL_ERROR
            "MINECRAFTC_EXPERIMENTAL_CXX_MODULES requires native Windows x64, "
            "MSVC 19.34 or newer, and the Ninja generator (Ninja 1.11 or newer).")
    endif()
    add_library(minecraftc_named_modules STATIC)
    target_include_directories(minecraftc_named_modules PRIVATE src)
    target_compile_options(minecraftc_named_modules PRIVATE /W4 /permissive-)
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
