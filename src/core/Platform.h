#pragma once

#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>

enum class DesktopPlatform {
    Linux,
    Windows,
    MacOS,
    Android,
    IOS
};

struct RuntimePathInputs {
    DesktopPlatform platform = DesktopPlatform::Linux;
    std::filesystem::path executablePath;
    std::filesystem::path currentDirectory;
    std::filesystem::path homeDirectory;
    std::filesystem::path roamingAppData;
    std::filesystem::path xdgDataHome;
};

struct RuntimePaths {
    std::filesystem::path assetRoot;
    std::filesystem::path dataRoot;

    std::filesystem::path savesDirectory() const { return dataRoot / "saves"; }
    std::filesystem::path settingsFile() const {
        return savesDirectory() / "options.txt";
    }
    std::filesystem::path logFile() const { return dataRoot / "minecraftc.log"; }
    std::filesystem::path asset(const std::filesystem::path& relative) const {
        return assetRoot / relative;
    }
};

DesktopPlatform currentDesktopPlatform();
RuntimePaths resolveRuntimePaths(const RuntimePathInputs& inputs);
RuntimePaths discoverRuntimePaths(const char* argv0);

namespace Platform {

bool replaceFileAtomically(const std::filesystem::path& source,
                           const std::filesystem::path& destination,
                           std::error_code& error);
bool stdoutSupportsColor();
bool localTime(std::time_t value, std::tm& result);

} // namespace Platform
