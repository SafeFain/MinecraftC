#include "app/Application.h"
#include "Config.h"

#include <iostream>
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
              << " [--renderer=vulkan|vulkan-demo|vulkan-textured-demo]"
              << " [--benchmark-frames=N]\n"
              << "Worlds and settings are stored in the platform user-data directory.\n";
}

}  // namespace

std::unique_ptr<ApplicationHost> createApplication(int argc, char** argv) {
    const int benchmarkFrames = benchmarkFrameCount(argc, argv);

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
            return createRenderDemoApplication(
                std::move(paths),
                argument == "--renderer=vulkan-textured-demo", benchmarkFrames);
        }
        if (argument == "--renderer=vulkan") {
            continue;
        }
        if (argument.rfind("--renderer=", 0) == 0)
            throw std::runtime_error("Unknown renderer: " + argument.substr(11));
        if (argument.rfind("--benchmark-frames=", 0) == 0) continue;
    }

    if (benchmarkFrames > 0)
        throw std::runtime_error("--benchmark-frames requires a renderer demo");

    return createGameApplication(std::move(paths));
}
