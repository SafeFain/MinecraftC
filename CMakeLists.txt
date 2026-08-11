cmake_minimum_required(VERSION 3.16)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" MINECRAFTC_VERSION
     LIMIT_COUNT 1)
if(NOT MINECRAFTC_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR
        "VERSION must contain one numeric semantic version (for example 1.2.3)")
endif()
project(MinecraftC VERSION "${MINECRAFTC_VERSION}" LANGUAGES CXX C)

if(ANDROID OR IOS)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

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
option(MINECRAFTC_DESKTOP_GLES3
       "Use an OpenGL ES 3.0 context on desktop for backend validation" OFF)
set(MINECRAFTC_OPENGL_DEFAULT ON)
if(IOS)
    set(MINECRAFTC_OPENGL_DEFAULT OFF)
endif()
option(MINECRAFTC_ENABLE_OPENGL
       "Build the OpenGL/OpenGL ES gameplay backend"
       ${MINECRAFTC_OPENGL_DEFAULT})
set(MINECRAFTC_VULKAN_DEFAULT OFF)
if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32 OR APPLE OR IOS)
    set(MINECRAFTC_VULKAN_DEFAULT ON)
endif()
option(MINECRAFTC_ENABLE_VULKAN
       "Build the Vulkan gameplay backend"
       ${MINECRAFTC_VULKAN_DEFAULT})
if(IOS AND MINECRAFTC_ENABLE_OPENGL)
    message(FATAL_ERROR
        "iOS is Vulkan-only; MINECRAFTC_ENABLE_OPENGL cannot be enabled")
elseif(IOS AND NOT MINECRAFTC_ENABLE_VULKAN)
    message(FATAL_ERROR
        "iOS is Vulkan-only; MINECRAFTC_ENABLE_VULKAN cannot be disabled")
elseif((ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32 OR
        (APPLE AND NOT IOS)) AND
   NOT MINECRAFTC_ENABLE_VULKAN)
    message(FATAL_ERROR
        "Android, Linux, Windows, and macOS builds require both the Vulkan and OpenGL render backends; "
        "MINECRAFTC_ENABLE_VULKAN cannot be disabled")
elseif((ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32 OR
        (APPLE AND NOT IOS)) AND
       NOT MINECRAFTC_ENABLE_OPENGL)
    message(FATAL_ERROR
        "Android, Linux, Windows, and macOS builds require both the Vulkan and OpenGL render backends; "
        "MINECRAFTC_ENABLE_OPENGL cannot be disabled")
elseif(MINECRAFTC_ENABLE_VULKAN AND
       NOT ANDROID AND
       NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT WIN32 AND NOT APPLE AND
       NOT IOS)
    message(FATAL_ERROR
        "MINECRAFTC_ENABLE_VULKAN supports Android, Linux, Windows, macOS, and iOS only")
endif()
if(IOS AND CMAKE_VERSION VERSION_LESS 3.28)
    message(FATAL_ERROR "iOS builds require CMake 3.28 or newer")
endif()
if(IOS AND MINECRAFTC_DESKTOP_GLES3)
    message(FATAL_ERROR "MINECRAFTC_DESKTOP_GLES3 is not available for iOS")
endif()
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
    if(ANDROID)
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    else()
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
    endif()
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
    if(IOS)
        set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
    else()
        set(SDL_HIDAPI ON CACHE BOOL "" FORCE)
    endif()
    set(SDL_JOYSTICK ON CACHE BOOL "" FORCE)
    set(SDL_POWER OFF CACHE BOOL "" FORCE)
    set(SDL_RENDER OFF CACHE BOOL "" FORCE)
    set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
    set(SDL_TRAY OFF CACHE BOOL "" FORCE)
    if(IOS)
        set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
        set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
        set(SDL_VULKAN ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.10
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3)
    if(ANDROID)
        set(MINECRAFTC_SDL_TARGET SDL3::SDL3-shared)
    else()
        set(MINECRAFTC_SDL_TARGET SDL3::SDL3-static)
    endif()
endif()
if(ANDROID)
    find_library(MINECRAFTC_GLES_LIBRARY GLESv3)
    if(NOT MINECRAFTC_GLES_LIBRARY)
        message(FATAL_ERROR "Android OpenGL ES 3 library was not found")
    endif()
    if(MINECRAFTC_ENABLE_VULKAN)
        find_library(MINECRAFTC_VULKAN_LIBRARY vulkan)
        if(NOT MINECRAFTC_VULKAN_LIBRARY)
            message(FATAL_ERROR "Android Vulkan loader library was not found")
        endif()
        set(MINECRAFTC_VULKAN_TARGET ${MINECRAFTC_VULKAN_LIBRARY})
    endif()
else()
    if(MINECRAFTC_ENABLE_OPENGL)
        find_package(OpenGL REQUIRED)
    endif()
    if(MINECRAFTC_ENABLE_VULKAN)
        if(APPLE)
            set(MINECRAFTC_MOLTENVK_ROOT "" CACHE PATH
                "Root of the extracted MoltenVK Apple runtime package")
            if(NOT MINECRAFTC_MOLTENVK_ROOT)
                message(FATAL_ERROR
                    "Apple Vulkan builds require MINECRAFTC_MOLTENVK_ROOT to point to "
                    "the extracted MoltenVK package root")
            endif()
            set(MINECRAFTC_MOLTENVK_INCLUDE_DIR
                "${MINECRAFTC_MOLTENVK_ROOT}/MoltenVK/include")
            if(IOS)
                set(MINECRAFTC_MOLTENVK_XCFRAMEWORK
                    "${MINECRAFTC_MOLTENVK_ROOT}/MoltenVK/static/MoltenVK.xcframework")
                string(TOLOWER "${CMAKE_OSX_SYSROOT}" MINECRAFTC_IOS_SYSROOT)
                if(MINECRAFTC_IOS_SYSROOT MATCHES "iphonesimulator")
                    set(MINECRAFTC_MOLTENVK_LIBRARY
                        "${MINECRAFTC_MOLTENVK_XCFRAMEWORK}/ios-arm64_x86_64-simulator/libMoltenVK.a")
                else()
                    set(MINECRAFTC_MOLTENVK_LIBRARY
                        "${MINECRAFTC_MOLTENVK_XCFRAMEWORK}/ios-arm64/libMoltenVK.a")
                endif()
            else()
                set(MINECRAFTC_MOLTENVK_LIBRARY
                    "${MINECRAFTC_MOLTENVK_ROOT}/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib")
            endif()
            set(MINECRAFTC_MOLTENVK_LICENSE
                "${MINECRAFTC_MOLTENVK_ROOT}/LICENSE")
            foreach(MOLTENVK_FILE IN ITEMS
                    "${MINECRAFTC_MOLTENVK_INCLUDE_DIR}/vulkan/vulkan.h"
                    "${MINECRAFTC_MOLTENVK_LIBRARY}"
                    "${MINECRAFTC_MOLTENVK_LICENSE}")
                if(NOT EXISTS "${MOLTENVK_FILE}")
                    message(FATAL_ERROR
                        "MoltenVK package is incomplete: ${MOLTENVK_FILE} was not found")
                endif()
            endforeach()
            if(IOS)
                add_library(minecraftc_moltenvk STATIC IMPORTED GLOBAL)
            else()
                add_library(minecraftc_moltenvk SHARED IMPORTED GLOBAL)
            endif()
            set_target_properties(minecraftc_moltenvk PROPERTIES
                IMPORTED_LOCATION "${MINECRAFTC_MOLTENVK_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${MINECRAFTC_MOLTENVK_INCLUDE_DIR}")
            set(MINECRAFTC_VULKAN_TARGET minecraftc_moltenvk)
        else()
            find_package(Vulkan REQUIRED)
            set(MINECRAFTC_VULKAN_TARGET Vulkan::Vulkan)
        endif()
    endif()
endif()

add_library(minecraftc_assets STATIC
    src/core/AssetStore.cpp
    src/platform/sdl/SdlAssetStore.cpp)
target_include_directories(minecraftc_assets PUBLIC src)
target_link_libraries(minecraftc_assets PUBLIC ${MINECRAFTC_SDL_TARGET})
link_libraries(minecraftc_assets)

# ── GLAD (minimal OpenGL loader) ───────────────────────────────────────

if(MINECRAFTC_ENABLE_OPENGL)
    add_library(glad STATIC
        external/glad/src/glad.c
    )
    # Compile glad as C++ (uses reinterpret_cast, extern "C")
    set_source_files_properties(external/glad/src/glad.c PROPERTIES LANGUAGE CXX)
    target_include_directories(glad PUBLIC external/glad/include)
endif()

# ── Main executable ────────────────────────────────────────────────────

file(GLOB_RECURSE SOURCES
    src/*.cpp
    src/*.h
)
set(MINECRAFTC_SDL_BACKEND_SOURCES
    src/platform/sdl/SdlAudioSystem.cpp
    src/platform/sdl/SdlClipboard.cpp
    src/platform/sdl/SdlGamepadManager.cpp
    src/platform/sdl/SdlWindow.cpp)
set(MINECRAFTC_OPENGL_BACKEND_SOURCES
    src/renderer/backend/opengl/OpenGLBlockTextureAtlas.cpp
    src/renderer/backend/opengl/OpenGLFontRenderer.cpp
    src/renderer/backend/opengl/OpenGLModelRenderer.cpp
    src/renderer/backend/opengl/OpenGLRenderer.cpp
    src/renderer/backend/opengl/OpenGLShader.cpp
    src/renderer/backend/opengl/OpenGLUIRenderer.cpp)
set(MINECRAFTC_VULKAN_BACKEND_SOURCES
    src/renderer/backend/vulkan/VulkanRenderer.cpp
    src/renderer/backend/vulkan/VulkanUIRenderer.cpp
    src/renderer/backend/vulkan/VmaImplementation.cpp)
foreach(BACKEND_SOURCE IN LISTS MINECRAFTC_SDL_BACKEND_SOURCES
                                MINECRAFTC_OPENGL_BACKEND_SOURCES
                                MINECRAFTC_VULKAN_BACKEND_SOURCES)
    list(REMOVE_ITEM SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/${BACKEND_SOURCE}")
endforeach()
list(REMOVE_ITEM SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/native/NativePlatform.cpp")
list(REMOVE_ITEM SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/sdl/SdlMain.cpp")
list(REMOVE_ITEM SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/native/NativeCrashHandler.cpp")
list(REMOVE_ITEM SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/sdl/SdlPlatformPaths.cpp")
list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/core/AssetStore.cpp")
list(REMOVE_ITEM SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/sdl/SdlAssetStore.cpp")

add_library(minecraftc_platform STATIC
    src/platform/native/NativePlatform.cpp
    src/platform/native/NativeCrashHandler.cpp
    src/platform/sdl/SdlPlatformPaths.cpp)
target_include_directories(minecraftc_platform PUBLIC src)
target_compile_definitions(minecraftc_platform PRIVATE
    MINECRAFTC_INSTALL_DATADIR="${CMAKE_INSTALL_DATADIR}")
target_link_libraries(minecraftc_platform PRIVATE ${MINECRAFTC_SDL_TARGET})
if(WIN32)
    target_link_libraries(minecraftc_platform PUBLIC shell32 ole32)
endif()

add_library(minecraftc_platform_sdl STATIC ${MINECRAFTC_SDL_BACKEND_SOURCES})
target_include_directories(minecraftc_platform_sdl PRIVATE src)
target_compile_definitions(minecraftc_platform_sdl PRIVATE
    MINECRAFTC_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(minecraftc_platform_sdl PRIVATE ${MINECRAFTC_SDL_TARGET})
if(MINECRAFTC_ENABLE_OPENGL)
    target_compile_definitions(minecraftc_platform_sdl PRIVATE
        MINECRAFTC_ENABLE_OPENGL=1)
endif()
if(MINECRAFTC_DESKTOP_GLES3)
    target_compile_definitions(minecraftc_platform_sdl PRIVATE
        MINECRAFTC_FORCE_GLES3=1)
endif()
if(MINECRAFTC_ENABLE_VULKAN)
    target_compile_definitions(minecraftc_platform_sdl PRIVATE
        MINECRAFTC_ENABLE_VULKAN=1)
    target_link_libraries(minecraftc_platform_sdl PRIVATE
        ${MINECRAFTC_VULKAN_TARGET})
endif()
if(MINECRAFTC_ENABLE_OPENGL)
    add_library(minecraftc_render_opengl STATIC ${MINECRAFTC_OPENGL_BACKEND_SOURCES})
    target_include_directories(minecraftc_render_opengl PRIVATE
        src external/glad/include external/stb external)
    target_compile_definitions(minecraftc_render_opengl PUBLIC
        MINECRAFTC_ENABLE_OPENGL=1)
    target_link_libraries(minecraftc_render_opengl PRIVATE
        glad glm::glm minecraftc_assets ${MINECRAFTC_SDL_TARGET})
    if(ANDROID)
        target_link_libraries(minecraftc_render_opengl PRIVATE
            ${MINECRAFTC_GLES_LIBRARY} android log)
    else()
        target_link_libraries(minecraftc_render_opengl PRIVATE OpenGL::GL)
    endif()
endif()
if(MINECRAFTC_ENABLE_VULKAN)
    add_library(minecraftc_render_vulkan STATIC
        ${MINECRAFTC_VULKAN_BACKEND_SOURCES})
    target_include_directories(minecraftc_render_vulkan PRIVATE src)
    target_include_directories(minecraftc_render_vulkan SYSTEM PRIVATE
        external/VulkanMemoryAllocator/include external/stb)
    target_compile_definitions(minecraftc_render_vulkan PRIVATE
        MINECRAFTC_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        MINECRAFTC_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        MINECRAFTC_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        VMA_VULKAN_VERSION=1000000)
    target_compile_definitions(minecraftc_render_vulkan PUBLIC
        MINECRAFTC_ENABLE_VULKAN=1)
    target_link_libraries(minecraftc_render_vulkan PRIVATE
        glm::glm ${MINECRAFTC_VULKAN_TARGET})
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(minecraftc_render_vulkan PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(minecraftc_platform PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(minecraftc_platform PRIVATE /W4 /permissive- /utf-8)
endif()

set(MINECRAFTC_IOS_RESOURCES)
if(IOS)
    file(GLOB_RECURSE MINECRAFTC_IOS_ASSET_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
    foreach(IOS_ASSET IN LISTS MINECRAFTC_IOS_ASSET_FILES)
        file(RELATIVE_PATH IOS_ASSET_RELATIVE
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${IOS_ASSET}")
        get_filename_component(IOS_ASSET_DIRECTORY "${IOS_ASSET_RELATIVE}" DIRECTORY)
        set_source_files_properties("${IOS_ASSET}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources/${IOS_ASSET_DIRECTORY}")
        list(APPEND MINECRAFTC_IOS_RESOURCES "${IOS_ASSET}")
    endforeach()
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ios")
    configure_file("${MINECRAFTC_MOLTENVK_LICENSE}"
        "${CMAKE_CURRENT_BINARY_DIR}/ios/MoltenVK-LICENSE.txt" COPYONLY)
    set_source_files_properties(
        "${CMAKE_CURRENT_BINARY_DIR}/ios/MoltenVK-LICENSE.txt" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/licenses")
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/ios/Assets.xcassets" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    list(APPEND MINECRAFTC_IOS_RESOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        "${CMAKE_CURRENT_BINARY_DIR}/ios/MoltenVK-LICENSE.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/ios/Assets.xcassets")
endif()

if(ANDROID)
    set(MINECRAFTC_APP_TARGET main)
    add_library(${MINECRAFTC_APP_TARGET} SHARED ${SOURCES})
elseif(IOS)
    set(MINECRAFTC_APP_TARGET minecraftc)
    add_executable(${MINECRAFTC_APP_TARGET} MACOSX_BUNDLE
        ${SOURCES} ${MINECRAFTC_IOS_RESOURCES})
else()
    set(MINECRAFTC_APP_TARGET minecraftc)
    add_executable(${MINECRAFTC_APP_TARGET} ${SOURCES})
endif()
target_sources(${MINECRAFTC_APP_TARGET} PRIVATE
    src/platform/sdl/SdlMain.cpp)
target_include_directories(${MINECRAFTC_APP_TARGET} PRIVATE
    src
    external/FastNoiseLite
    external/cgltf
    external/stb
    external
)
target_compile_definitions(${MINECRAFTC_APP_TARGET} PRIVATE
    MINECRAFTC_VERSION_STRING="${PROJECT_VERSION}")
if(MINECRAFTC_ENABLE_OPENGL)
    target_compile_definitions(${MINECRAFTC_APP_TARGET} PRIVATE
        MINECRAFTC_ENABLE_OPENGL=1)
endif()
if(MINECRAFTC_DESKTOP_GLES3)
    target_compile_definitions(${MINECRAFTC_APP_TARGET} PRIVATE
        MINECRAFTC_FORCE_GLES3=1)
endif()
if(MINECRAFTC_ENABLE_VULKAN)
    target_compile_definitions(${MINECRAFTC_APP_TARGET} PRIVATE
        MINECRAFTC_ENABLE_VULKAN=1)
endif()

# ── Debug / logging compile-time gating ─────────────────────────────────
# LOG_COMPILE_LEVEL strips log macros below the given level:
#   0=Trace  1=Debug  2=Info  3=Warn  4=Error  5=Fatal
target_compile_definitions(${MINECRAFTC_APP_TARGET} PRIVATE
    $<$<CONFIG:Debug>:LOG_COMPILE_LEVEL=0>
    $<$<CONFIG:Release>:LOG_COMPILE_LEVEL=2>
)
target_link_libraries(${MINECRAFTC_APP_TARGET} PRIVATE
    glm::glm
    minecraftc_platform
    minecraftc_platform_sdl
    ${CMAKE_DL_LIBS}
)
if(MINECRAFTC_ENABLE_OPENGL)
    target_link_libraries(${MINECRAFTC_APP_TARGET} PRIVATE
        minecraftc_render_opengl)
endif()
if(MINECRAFTC_ENABLE_VULKAN)
    target_link_libraries(${MINECRAFTC_APP_TARGET} PRIVATE
        minecraftc_render_vulkan)
endif()
if(APPLE AND NOT IOS)
    set_target_properties(${MINECRAFTC_APP_TARGET} PROPERTIES
        BUILD_RPATH "${MINECRAFTC_MOLTENVK_ROOT}/MoltenVK/dynamic/dylib/macOS"
        INSTALL_RPATH "@executable_path")
endif()
if(IOS)
    math(EXPR MINECRAFTC_IOS_BUILD_NUMBER
        "${PROJECT_VERSION_MAJOR} * 10000 + ${PROJECT_VERSION_MINOR} * 100 + ${PROJECT_VERSION_PATCH}")
    configure_file(ios/Info.plist.in
        "${CMAKE_CURRENT_BINARY_DIR}/ios/Info.plist" @ONLY)
    string(CONCAT MINECRAFTC_IOS_OTHER_LDFLAGS
        "$(inherited) -ObjC -Wl,-u,_vkGetInstanceProcAddr "
        "-Wl,-exported_symbol,_vkGetInstanceProcAddr -Wl,-export_dynamic")
    set_target_properties(${MINECRAFTC_APP_TARGET} PROPERTIES
        OUTPUT_NAME "MinecraftC"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/ios/Info.plist"
        MACOSX_BUNDLE_BUNDLE_NAME "MinecraftC"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.SafeFain.MinecraftC"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${MINECRAFTC_IOS_BUILD_NUMBER}"
        ENABLE_EXPORTS ON
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ""
        XCODE_ATTRIBUTE_EXPORTED_SYMBOLS_FILE
            "${CMAKE_CURRENT_SOURCE_DIR}/ios/MinecraftC.exports"
        XCODE_ATTRIBUTE_INSTALL_PATH "$(LOCAL_APPS_DIR)"
        XCODE_ATTRIBUTE_LD_EXPORT_SYMBOLS "YES"
        XCODE_ATTRIBUTE_OTHER_LDFLAGS "${MINECRAFTC_IOS_OTHER_LDFLAGS}"
        XCODE_ATTRIBUTE_SKIP_INSTALL "NO"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "io.github.SafeFain.MinecraftC"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_GENERATE_SCHEME TRUE)
    foreach(MINECRAFTC_IOS_LIBRARY IN ITEMS
            minecraftc_assets
            minecraftc_platform
            minecraftc_platform_sdl
            minecraftc_render_vulkan)
        set_target_properties(${MINECRAFTC_IOS_LIBRARY} PROPERTIES
            XCODE_ATTRIBUTE_SKIP_INSTALL "YES")
    endforeach()
    foreach(IOS_FRAMEWORK IN ITEMS Metal UIKit QuartzCore Foundation
                                   CoreGraphics IOSurface)
        find_library(MINECRAFTC_${IOS_FRAMEWORK}_FRAMEWORK ${IOS_FRAMEWORK}
            REQUIRED)
        target_link_libraries(${MINECRAFTC_APP_TARGET} PRIVATE
            ${MINECRAFTC_${IOS_FRAMEWORK}_FRAMEWORK})
    endforeach()
endif()

# ── Compiler warnings ──────────────────────────────────────────────────

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${MINECRAFTC_APP_TARGET} PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(${MINECRAFTC_APP_TARGET} PRIVATE /W4 /permissive- /utf-8)
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT MINECRAFTC_IPO_SUPPORTED OUTPUT MINECRAFTC_IPO_ERROR)
if(MINECRAFTC_IPO_SUPPORTED)
    set_property(TARGET ${MINECRAFTC_APP_TARGET} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

if(MINECRAFTC_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${MINECRAFTC_APP_TARGET} PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined -g -O0>)
    target_link_options(${MINECRAFTC_APP_TARGET} PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>)
    if(UNIX AND NOT APPLE)
        target_link_options(${MINECRAFTC_APP_TARGET} PRIVATE $<$<CONFIG:Debug>:-rdynamic>)
    endif()
endif()

# ── Install ────────────────────────────────────────────────────────────

if(ANDROID)
    # The Gradle project packages native libraries and assets.
elseif(IOS)
    # Keep the app installable so Xcode archive places it under
    # Products/Applications instead of IntermediateBuildFilesPath/UninstalledProducts.
    install(TARGETS ${MINECRAFTC_APP_TARGET}
        BUNDLE DESTINATION Applications)
elseif(MINECRAFTC_PORTABLE_LAYOUT)
    install(TARGETS minecraftc RUNTIME DESTINATION .)
    install(DIRECTORY assets/ DESTINATION assets)
    install(FILES LICENSE README.md packaging/README-release.md
            DESTINATION .)
    if(WIN32)
        install(FILES packaging/run-minecraftc.cmd DESTINATION .)
    else()
        install(PROGRAMS packaging/run-minecraftc.sh DESTINATION .)
    endif()
    if(APPLE)
        install(FILES "${MINECRAFTC_MOLTENVK_LIBRARY}" DESTINATION .)
        install(FILES "${MINECRAFTC_MOLTENVK_LICENSE}"
                DESTINATION licenses RENAME MoltenVK-LICENSE.txt)
    endif()
else()
    install(TARGETS minecraftc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY assets/ DESTINATION ${CMAKE_INSTALL_DATADIR}/minecraftc/assets)
    install(FILES LICENSE README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
    if(APPLE)
        install(FILES "${MINECRAFTC_MOLTENVK_LIBRARY}"
                DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(FILES "${MINECRAFTC_MOLTENVK_LICENSE}"
                DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses
                RENAME MoltenVK-LICENSE.txt)
    endif()
endif()

# ── Deterministic world-generation tests ───────────────────────────────────
include(CTest)
if(ANDROID OR IOS)
    set(BUILD_TESTING OFF CACHE BOOL "Build tests" FORCE)
endif()
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME api_dependency_boundaries
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tools/check_api_boundaries.py
                --root ${CMAKE_SOURCE_DIR})
    add_test(NAME release_version_cli COMMAND minecraftc --version)
    set_tests_properties(release_version_cli PROPERTIES
        PASS_REGULAR_EXPRESSION "MinecraftC Release-${PROJECT_VERSION}")
    add_test(NAME texture_generator_determinism_tiling_and_atlas
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/test_texture_generator.py)
    add_test(NAME vulkan_shader_assets
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tools/vulkan_shaders.py
                --root ${CMAKE_SOURCE_DIR})

    add_executable(vulkan_index_rebase_tests
        tests/VulkanIndexRebaseTests.cpp)
    target_include_directories(vulkan_index_rebase_tests PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(vulkan_index_rebase_tests PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME vulkan_zero_base_vertex_index_rebasing
        COMMAND vulkan_index_rebase_tests)

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
        src/renderer/CloudRenderData.cpp
        src/renderer/RenderDevice.cpp
        src/world/Block.cpp
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
        src/model/ModelRenderer.cpp
        src/model/GltfLoader.cpp
        src/renderer/backend/opengl/OpenGLModelRenderer.cpp
        src/platform/sdl/SdlGamepadManager.cpp
        src/platform/sdl/SdlWindow.cpp
        src/debug/Log.cpp
        src/renderer/backend/opengl/OpenGLShader.cpp
        src/renderer/StbImageImpl.cpp)
    target_include_directories(entity_model_registry_tests PRIVATE
        src external external/cgltf external/stb external/glad/include)
    target_compile_definitions(entity_model_registry_tests PRIVATE
        MINECRAFTC_ENABLE_OPENGL=1
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
        src/platform/sdl/SdlGamepadManager.cpp
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
