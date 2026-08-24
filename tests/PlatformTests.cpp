#include "core/Platform.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

namespace fs = std::filesystem;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void makeAssets(const fs::path& root) {
    fs::create_directories(root / "shaders");
    fs::create_directories(root / "textures" / "definitions");
    fs::create_directories(root / "shaders" / "vulkan");
    std::ofstream(root / "shaders" / "vulkan" / "chunk.vert.spv") << "shader";
    std::ofstream(root / "textures" / "definitions" / "blocks.json") << "{}";
}

RuntimePathInputs baseInputs(const fs::path& root) {
    RuntimePathInputs inputs;
    inputs.executablePath = root / "bin" / "minecraftc";
    inputs.currentDirectory = root / "work";
    inputs.homeDirectory = root / "home";
    inputs.roamingAppData = root / "roaming";
    inputs.xdgDataHome = root / "xdg";
    return inputs;
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() /
        ("minecraftc-platform-test-" + std::to_string(
            static_cast<unsigned long long>(std::rand())) + "-路径");
    fs::remove_all(root);

    try {
        makeAssets(root / "share" / "minecraftc" / "assets");
        RuntimePathInputs inputs = baseInputs(root);

        inputs.platform = DesktopPlatform::Linux;
        RuntimePaths paths = resolveRuntimePaths(inputs);
        require(paths.assetRoot == fs::weakly_canonical(
                    root / "share" / "minecraftc" / "assets"),
                "installed assets resolve relative to the executable");
        require(paths.dataRoot == root / "xdg" / "minecraftc",
                "Linux honors an absolute XDG data directory");

        inputs.xdgDataHome = "relative-xdg";
        paths = resolveRuntimePaths(inputs);
        require(paths.dataRoot == root / "home" / ".local" / "share" / "minecraftc",
                "Linux ignores a relative XDG data directory");

        inputs.platform = DesktopPlatform::MacOS;
        paths = resolveRuntimePaths(inputs);
        require(paths.dataRoot == root / "home" / "Library" /
                    "Application Support" / "MinecraftC",
                "macOS uses Application Support");

        inputs.platform = DesktopPlatform::Windows;
        paths = resolveRuntimePaths(inputs);
        require(paths.dataRoot == root / "roaming" / "MinecraftC",
                "Windows uses roaming application data");

        fs::create_directories(inputs.currentDirectory / "saves");
        paths = resolveRuntimePaths(inputs);
        require(paths.dataRoot == inputs.currentDirectory,
                "an existing local saves directory retains legacy paths");
        require(paths.settingsFile() == inputs.currentDirectory / "saves" / "options.txt",
                "legacy settings remain beside legacy saves");

        const fs::path developmentRoot = root / "development";
        RuntimePathInputs developmentInputs = baseInputs(developmentRoot);
        makeAssets(developmentInputs.currentDirectory / "assets");
        developmentInputs.platform = DesktopPlatform::Linux;
        developmentInputs.xdgDataHome = developmentRoot / "xdg";
        paths = resolveRuntimePaths(developmentInputs);
        require(paths.assetRoot == fs::weakly_canonical(
                    developmentInputs.currentDirectory / "assets"),
                "development assets resolve from the working directory");

        bool missingAssetsRejected = false;
        try {
            RuntimePathInputs missingInputs = baseInputs(root / "missing");
            (void)resolveRuntimePaths(missingInputs);
        } catch (const std::runtime_error&) {
            missingAssetsRejected = true;
        }
        require(missingAssetsRejected, "missing runtime assets produce a startup error");

        const fs::path destination = root / "replace" / "状态.txt";
        fs::create_directories(destination.parent_path());
        std::ofstream(destination) << "old";
        fs::path source = destination;
        source += ".tmp";
        std::ofstream(source) << "new";
        std::error_code error;
        require(Platform::replaceFileAtomically(source, destination, error),
                "atomic replacement succeeds when the destination exists");
        std::ifstream replaced(destination);
        std::string content;
        replaced >> content;
        require(content == "new" && !fs::exists(source),
                "atomic replacement installs the new content");
    } catch (...) {
        fs::remove_all(root);
        throw;
    }

    fs::remove_all(root);
    std::cout << "Platform tests passed\n";
    return 0;
}
