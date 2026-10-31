#include "core/Platform.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <chrono>

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
        fs::u8path("minecraftc-platform-test-" + std::to_string(
            static_cast<unsigned long long>(std::rand())) + u8"-路径");
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

        const fs::path destination = root / "replace" / fs::u8path(u8"状态.txt");
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
#if defined(_WIN32)
        // MSVC's file streams do not share deletion. A reader may finish while
        // an atomic publication is pending; the old destination must survive.
        std::ofstream(source) << "after-reader";
        std::thread releaseReader([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            replaced.close();
        });
        const bool published = Platform::replaceFileAtomically(source, destination, error);
        releaseReader.join();
        require(published && !error, "atomic replacement tolerates a briefly held reader");
        std::ifstream locked(destination);
        locked >> content;
        require(content == "after-reader", "reader retry did not publish the complete replacement");
        std::ofstream(source) << "must-not-publish";
        require(!Platform::replaceFileAtomically(source, destination, error) && error && fs::exists(source),
                "a persistently held destination must fail without deleting either file");
        locked.close();
        std::ifstream preserved(destination);
        preserved >> content;
        require(content == "after-reader", "failed atomic publication damaged the original destination");
#endif
    } catch (...) {
        fs::remove_all(root);
        throw;
    }

    fs::remove_all(root);
    std::cout << "Platform tests passed\n";
    return 0;
}
