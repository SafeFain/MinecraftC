cmake_minimum_required(VERSION 3.28)
include("${CMAKE_CURRENT_SOURCE_DIR}/tools/project_version.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
project(MinecraftC VERSION "${MINECRAFTC_VERSION_CORE}" LANGUAGES CXX C)

if(ANDROID OR IOS)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
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
if(NOT ANDROID AND
       NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT WIN32 AND NOT APPLE AND
       NOT IOS)
    message(FATAL_ERROR
        "MinecraftC supports Vulkan on Android, Linux, Windows, macOS, and iOS only")
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
    set(SDL_KMSDRM OFF CACHE BOOL "" FORCE)
    set(SDL_POWER OFF CACHE BOOL "" FORCE)
    set(SDL_RENDER OFF CACHE BOOL "" FORCE)
    set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
    set(SDL_TRAY OFF CACHE BOOL "" FORCE)
    set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
    set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
    set(SDL_VULKAN ON CACHE BOOL "" FORCE)
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
    find_library(MINECRAFTC_VULKAN_LIBRARY vulkan)
    if(NOT MINECRAFTC_VULKAN_LIBRARY)
        message(FATAL_ERROR "Android Vulkan loader library was not found")
    endif()
    set(MINECRAFTC_VULKAN_TARGET ${MINECRAFTC_VULKAN_LIBRARY})
else()
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

set(MINECRAFTC_PLUGIN_GAME_VERSION "${MINECRAFTC_VERSION}")
if(MINECRAFTC_VERSION_CHANNEL STREQUAL "release")
    set(MINECRAFTC_PLUGIN_GAME_VERSION "${MINECRAFTC_VERSION_CORE}")
endif()
set_source_files_properties(src/plugins/PluginManager.cpp PROPERTIES
    COMPILE_DEFINITIONS "MINECRAFTC_PLUGIN_GAME_VERSION=\"${MINECRAFTC_PLUGIN_GAME_VERSION}\"")

# Project sources and tests contain UTF-8 literals; keep their encoding independent
# of the Windows system code page without changing third-party target options.
if(MSVC)
    add_compile_options(/utf-8)
endif()

include(cmake/MinecraftCNamedModules.cmake)
include(cmake/MinecraftCTargets.cmake)

set(MINECRAFTC_IOS_RESOURCES)
if(IOS)
    foreach(content_root IN LISTS content_roots)
        file(GLOB_RECURSE content_files "${content_root}/*")
        foreach(content_file IN LISTS content_files)
            file(RELATIVE_PATH content_relative "${CMAKE_BINARY_DIR}/content-assets" "${content_file}")
            get_filename_component(content_directory "${content_relative}" DIRECTORY)
            set_source_files_properties("${content_file}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/${content_directory}")
            list(APPEND MINECRAFTC_IOS_RESOURCES "${content_file}")
        endforeach()
    endforeach()
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

if(APPLE AND NOT IOS)
    set(MINECRAFTC_MACOS_ICON
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/minecraftc.icns")
    set_source_files_properties("${MINECRAFTC_MACOS_ICON}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
endif()

set(MINECRAFTC_ENTRY_SOURCES
    src/main.cpp
    src/app/Application.cpp
    src/app/RenderDemoApplication.cpp)
if(ANDROID)
    set(MINECRAFTC_APP_TARGET main)
    add_library(${MINECRAFTC_APP_TARGET} SHARED ${MINECRAFTC_ENTRY_SOURCES})
elseif(IOS)
    set(MINECRAFTC_APP_TARGET minecraftc)
    add_executable(${MINECRAFTC_APP_TARGET} MACOSX_BUNDLE
        ${MINECRAFTC_ENTRY_SOURCES} ${MINECRAFTC_IOS_RESOURCES})
elseif(APPLE)
    set(MINECRAFTC_APP_TARGET minecraftc)
    add_executable(${MINECRAFTC_APP_TARGET} MACOSX_BUNDLE
        ${MINECRAFTC_ENTRY_SOURCES} "${MINECRAFTC_MACOS_ICON}")
else()
    set(MINECRAFTC_APP_TARGET minecraftc)
    add_executable(${MINECRAFTC_APP_TARGET} ${MINECRAFTC_ENTRY_SOURCES})
endif()
if(WIN32)
    file(TO_CMAKE_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/minecraftc.ico"
        MINECRAFTC_WINDOWS_ICON)
    configure_file(packaging/windows/minecraftc.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/minecraftc.rc" @ONLY)
    target_sources(${MINECRAFTC_APP_TARGET} PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/minecraftc.rc")
endif()
target_sources(${MINECRAFTC_APP_TARGET} PRIVATE
    src/platform/sdl/SdlMain.cpp)
target_include_directories(${MINECRAFTC_APP_TARGET} PRIVATE src)
minecraftc_enable_module_imports(${MINECRAFTC_APP_TARGET} src/app/RenderDemoApplication.cpp)
target_compile_definitions(${MINECRAFTC_APP_TARGET} PRIVATE
    MINECRAFTC_VERSION_STRING="${MINECRAFTC_VERSION_DISPLAY}")

# ── Debug / logging compile-time gating ─────────────────────────────────
# LOG_COMPILE_LEVEL strips log macros below the given level:
#   0=Trace  1=Debug  2=Info  3=Warn  4=Error  5=Fatal
target_compile_definitions(${MINECRAFTC_APP_TARGET} PRIVATE
    $<$<CONFIG:Debug>:LOG_COMPILE_LEVEL=0>
    $<$<CONFIG:Release>:LOG_COMPILE_LEVEL=2>
)
target_link_libraries(${MINECRAFTC_APP_TARGET} PRIVATE
    minecraftc_engine_policy minecraftc_app_session minecraftc_assets minecraftc_core
    minecraftc_gltf minecraftc_json minecraftc_log minecraftc_plugin_runtime minecraftc_plugins
    minecraftc_render_vulkan minecraftc_platform_sdl minecraftc_platform minecraftc_threading
    ${MINECRAFTC_SDL_TARGET})
if(APPLE AND NOT IOS)
    set_target_properties(${MINECRAFTC_APP_TARGET} PROPERTIES
        BUILD_RPATH "${MINECRAFTC_MOLTENVK_ROOT}/MoltenVK/dynamic/dylib/macOS"
        INSTALL_RPATH "@executable_path/../Frameworks"
        OUTPUT_NAME "MinecraftC"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "MinecraftC"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.SafeFain.MinecraftC"
        MACOSX_BUNDLE_ICON_FILE "minecraftc.icns"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION
            "${MINECRAFTC_VERSION_CODE}")
endif()
if(IOS)
    set(MINECRAFTC_IOS_BUILD_NUMBER "${MINECRAFTC_VERSION_CODE}")
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
        XCODE_ATTRIBUTE_STRIP_INSTALLED_PRODUCT "YES"
        XCODE_ATTRIBUTE_STRIP_STYLE "non-global"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "io.github.SafeFain.MinecraftC"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_GENERATE_SCHEME TRUE)
    if(MINECRAFTC_EXPERIMENTAL_CXX_MODULES)
        # SDL locates the statically linked MoltenVK entry point by name.
        set(ios_exported_symbols "${CMAKE_CURRENT_SOURCE_DIR}/ios/MinecraftC.exports")
        target_link_options(${MINECRAFTC_APP_TARGET} PRIVATE
            -ObjC
            "LINKER:-u,_vkGetInstanceProcAddr"
            "LINKER:-exported_symbols_list,${ios_exported_symbols}"
            "LINKER:-export_dynamic")
        set_property(TARGET ${MINECRAFTC_APP_TARGET} APPEND PROPERTY
            LINK_DEPENDS "${ios_exported_symbols}")
    endif()
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

if(MINECRAFTC_IPO_SUPPORTED)
    set_property(TARGET ${MINECRAFTC_APP_TARGET} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

if(MINECRAFTC_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${MINECRAFTC_APP_TARGET} PRIVATE
        "$<$<CONFIG:Debug>:-fsanitize=address,undefined;-g;-O0>")
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
    if(APPLE)
        install(TARGETS minecraftc BUNDLE DESTINATION .)
        install(DIRECTORY assets/
            DESTINATION "MinecraftC.app/Contents/Resources/assets")
    else()
        install(TARGETS minecraftc RUNTIME DESTINATION .)
        install(DIRECTORY assets/ DESTINATION assets)
    endif()
    install(FILES LICENSE VERSION README.md ASSET_PIPELINE.md ASSET_SOURCES.md packaging/README-release.md
            DESTINATION .)
    install(DIRECTORY docs/ DESTINATION docs FILES_MATCHING PATTERN "*.md"
        PATTERN "tasks" EXCLUDE PATTERN "superpowers" EXCLUDE PATTERN "context-migration.md" EXCLUDE)
    install(FILES android/README.md DESTINATION android)
    install(FILES ios/README.md DESTINATION ios)
    install(FILES packaging/README-release.md packaging/MOLTENVK-UPSTREAM.md DESTINATION packaging)
    install(DIRECTORY sdk/ DESTINATION sdk)
    install(DIRECTORY examples/plugins/ DESTINATION examples/plugins)
    if(WIN32)
        install(FILES packaging/run-minecraftc.cmd DESTINATION .)
    else()
        install(PROGRAMS packaging/run-minecraftc.sh DESTINATION .)
    endif()
    if(APPLE)
        install(FILES "${MINECRAFTC_MOLTENVK_LIBRARY}"
                DESTINATION "MinecraftC.app/Contents/Frameworks")
        install(FILES "${MINECRAFTC_MOLTENVK_LICENSE}"
                DESTINATION licenses RENAME MoltenVK-LICENSE.txt)
    endif()
else()
    if(APPLE)
        install(TARGETS minecraftc BUNDLE DESTINATION .)
        install(DIRECTORY assets/
            DESTINATION "MinecraftC.app/Contents/Resources/assets")
    else()
        install(TARGETS minecraftc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(DIRECTORY assets/
            DESTINATION ${CMAKE_INSTALL_DATADIR}/minecraftc/assets)
    endif()
    install(FILES LICENSE VERSION README.md ASSET_PIPELINE.md ASSET_SOURCES.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs FILES_MATCHING PATTERN "*.md"
        PATTERN "tasks" EXCLUDE PATTERN "superpowers" EXCLUDE PATTERN "context-migration.md" EXCLUDE)
    install(FILES android/README.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/android)
    install(FILES ios/README.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/ios)
    install(FILES packaging/README-release.md packaging/MOLTENVK-UPSTREAM.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/packaging)
    install(DIRECTORY sdk/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY sdk/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/sdk)
    install(DIRECTORY examples/plugins/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples/plugins)
    if(APPLE)
        install(FILES "${MINECRAFTC_MOLTENVK_LIBRARY}"
                DESTINATION "MinecraftC.app/Contents/Frameworks")
        install(FILES "${MINECRAFTC_MOLTENVK_LICENSE}"
                DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses
                RENAME MoltenVK-LICENSE.txt)
    endif()
endif()
if(NOT ANDROID AND NOT IOS)
    if(APPLE)
        set(content_install "MinecraftC.app/Contents/Resources/assets")
    elseif(MINECRAFTC_PORTABLE_LAYOUT)
        set(content_install assets)
    else()
        set(content_install "${CMAKE_INSTALL_DATADIR}/minecraftc/assets")
    endif()
    foreach(content_root IN LISTS content_roots)
        install(DIRECTORY "${content_root}" DESTINATION "${content_install}/plugins")
    endforeach()
    if(MINECRAFTC_PORTABLE_LAYOUT)
        install(FILES tools/content_package.py DESTINATION sdk/tools)
    else()
        install(FILES tools/content_package.py DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tools")
        install(FILES tools/content_package.py DESTINATION "${CMAKE_INSTALL_DOCDIR}/sdk/tools")
    endif()
    if(MINECRAFTC_PORTABLE_LAYOUT)
        set(MINECRAFTC_ASSET_GUIDE_DESTINATION assets)
    else()
        set(MINECRAFTC_ASSET_GUIDE_DESTINATION "${CMAKE_INSTALL_DOCDIR}/assets")
    endif()
    install(DIRECTORY assets/ DESTINATION "${MINECRAFTC_ASSET_GUIDE_DESTINATION}"
        FILES_MATCHING PATTERN "*.md")
endif()
if(UNIX AND NOT APPLE AND NOT ANDROID)
    install(FILES packaging/linux/io.github.SafeFain.MinecraftC.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/icons/minecraftc.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/1024x1024/apps)
endif()

include(CTest)
if(BUILD_TESTING AND NOT ANDROID AND NOT IOS)
    include(cmake/MinecraftCBoundaries.cmake)
endif()
include(cmake/MinecraftCTests.cmake)
