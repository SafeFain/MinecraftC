#include "app/Application.h"
#include "Config.h"
#include "game/ClientSettings.h"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

int benchmarkFrameCount(int argc, char** argv) {
    constexpr std::string_view prefix = "--benchmark-frames=";
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument.rfind(prefix.data(), 0) != 0) continue;
        const std::string value = argument.substr(prefix.size());
        size_t parsed = 0;
        const long frames = std::stol(value, &parsed);
        if (parsed != value.size() || frames <= 0 || frames > 100000)
            throw std::runtime_error("Invalid benchmark frame count: " + value);
        return static_cast<int>(frames);
    }
    return 0;
}

void printHelp() {
    std::cout << "MinecraftC " << Config::GAME_VERSION << "\n"
              << "Usage: minecraftc [--help] [--version]"
#if defined(MINECRAFTC_ENABLE_OPENGL) && defined(MINECRAFTC_ENABLE_VULKAN)
              << " [--renderer=opengl|vulkan|opengl-demo|vulkan-demo|vulkan-textured-demo]"
#elif defined(MINECRAFTC_ENABLE_OPENGL)
              << " [--renderer=opengl|opengl-demo]"
#else
              << " [--renderer=vulkan|vulkan-demo|vulkan-textured-demo]"
#endif
              << " [--benchmark-frames=N]\n"
              << "Worlds and settings are stored in the platform user-data directory.\n";
}

}  // namespace

std::unique_ptr<ApplicationHost> createApplication(int argc, char** argv) {
    const int benchmarkFrames = benchmarkFrameCount(argc, argv);
    std::optional<GraphicsApi> commandLineApi;

    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument == "--version") {
            std::cout << "MinecraftC " << Config::GAME_VERSION << '\n';
            return {};
        }
        if (argument == "--help" || argument == "-h") {
            printHelp();
            return {};
        }
    }

    RuntimePaths paths = discoverRuntimePaths(argc > 0 ? argv[0] : nullptr);
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument == "--renderer=vulkan-demo" ||
            argument == "--renderer=vulkan-textured-demo") {
#if defined(MINECRAFTC_ENABLE_VULKAN)
            return createRenderDemoApplication(
                std::move(paths), GraphicsApi::Vulkan,
                argument == "--renderer=vulkan-textured-demo", benchmarkFrames);
#else
            throw std::runtime_error(
                "Vulkan support is disabled; rebuild with "
                "-DMINECRAFTC_ENABLE_VULKAN=ON");
#endif
        }
        if (argument == "--renderer=opengl-demo") {
#if defined(MINECRAFTC_ENABLE_OPENGL)
            return createRenderDemoApplication(
                std::move(paths), GraphicsApi::OpenGL33, false, benchmarkFrames);
#else
            throw std::runtime_error("OpenGL support is disabled in this build");
#endif
        }
        if (argument == "--renderer=opengl") {
#if defined(MINECRAFTC_ENABLE_OPENGL)
            commandLineApi = GraphicsApi::OpenGL33;
            continue;
#else
            throw std::runtime_error("OpenGL support is disabled in this build");
#endif
        }
        if (argument == "--renderer=vulkan") {
#if defined(MINECRAFTC_ENABLE_VULKAN)
            commandLineApi = GraphicsApi::Vulkan;
            continue;
#else
            throw std::runtime_error(
                "Vulkan support is disabled; rebuild with "
                "-DMINECRAFTC_ENABLE_VULKAN=ON");
#endif
        }
        if (argument.rfind("--renderer=", 0) == 0)
            throw std::runtime_error("Unknown renderer: " + argument.substr(11));
        if (argument.rfind("--benchmark-frames=", 0) == 0) continue;
    }

    if (benchmarkFrames > 0)
        throw std::runtime_error("--benchmark-frames requires a renderer demo");

    GraphicsApi api =
#if defined(MINECRAFTC_ENABLE_OPENGL)
        GraphicsApi::OpenGL33;
#else
        GraphicsApi::Vulkan;
#endif
#if defined(MINECRAFTC_ENABLE_VULKAN)
    if (ClientSettings::load(paths.settingsFile()).rendererBackend ==
        RendererBackend::Vulkan)
        api = GraphicsApi::Vulkan;
#endif
    if (commandLineApi) api = *commandLineApi;

    try {
        return createGameApplication(paths, api);
    } catch (const std::exception& error) {
#if defined(MINECRAFTC_ENABLE_OPENGL)
        if (api != GraphicsApi::Vulkan) throw;
        std::cerr << "Vulkan startup failed; falling back to OpenGL: "
                  << error.what() << '\n';
        return createGameApplication(std::move(paths), GraphicsApi::OpenGL33);
#else
        (void)error;
        throw;
#endif
    }
}
