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
        long frames = 0;
        try {
            frames = std::stol(value, &parsed);
        } catch (const std::exception&) {
            throw CommandLineError("Invalid benchmark frame count: " + value);
        }
        if (parsed != value.size() || frames <= 0 || frames > 100000)
            throw CommandLineError("Invalid benchmark frame count: " + value);
        return static_cast<int>(frames);
    }
    return 0;
}

void printHelp() {
    std::cout << "MinecraftC " << Config::GAME_VERSION << "\n"
              << "Usage: minecraftc [--help] [--version]"
              << " [--renderer=vulkan|vulkan-demo|vulkan-textured-demo]"
              << " [--benchmark-frames=N] [--benchmark-models=128|512] [--benchmark-model-parts=1|4] [--benchmark-single-instance] [--benchmark-model-only] [--safe-mode]\n"
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

    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument.rfind("--renderer=", 0) != 0) continue;
        if (argument == "--renderer=vulkan" ||
            argument == "--renderer=vulkan-demo" ||
            argument == "--renderer=vulkan-textured-demo")
            continue;
        throw CommandLineError("Unknown renderer: " + argument.substr(11));
    }

    ModelBenchmarkOptions modelOptions;
    for(int i=1;i<argc;++i) {
        const std::string argument(argv[i]);
        const auto parseOption=[&](const std::string& prefix,int first,int second,int& output) {
            if(argument.rfind(prefix,0)!=0) return;
            const auto value=argument.substr(prefix.size());
            if(value!=std::to_string(first) && value!=std::to_string(second)) throw CommandLineError("Invalid model benchmark option: "+argument);
            output=std::stoi(value);
        };
        parseOption("--benchmark-models=",128,512,modelOptions.instances);
        parseOption("--benchmark-model-parts=",1,4,modelOptions.parts);
        if(argument=="--benchmark-single-instance") modelOptions.batching=false;
        if(argument=="--benchmark-model-only") modelOptions.only=true;
        if(argument.rfind("--benchmark-model",0)==0 || argument=="--benchmark-single-instance")
            if(benchmarkFrames==0) throw CommandLineError("Model benchmark options require --benchmark-frames");
    }
    RuntimePaths paths = discoverRuntimePaths(argc > 0 ? argv[0] : nullptr);
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument == "--renderer=vulkan-demo" ||
            argument == "--renderer=vulkan-textured-demo") {
            return createRenderDemoApplication(
                std::move(paths),
                argument == "--renderer=vulkan-textured-demo", benchmarkFrames, modelOptions);
        }
        if (argument == "--renderer=vulkan") {
            continue;
        }
        if (argument.rfind("--benchmark-frames=", 0) == 0) continue;
    }

    if (benchmarkFrames > 0)
        throw CommandLineError("--benchmark-frames requires a renderer demo");

    bool safeMode=false;
    for(int i=1;i<argc;++i) if(std::string_view(argv[i])=="--safe-mode") safeMode=true;
    return createGameApplication(std::move(paths),safeMode);
}
