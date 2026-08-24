#include "core/Platform.h"
#include "platform/sdl/SdlPlatformPaths.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>

#if defined(__APPLE__)
#  include <TargetConditionals.h>
#endif

#if defined(_WIN32)
#  define NOMINMAX
#  include <io.h>
#  include <windows.h>
#  include <shlobj.h>
#elif defined(__APPLE__)
#  include <unistd.h>
#  if !TARGET_OS_IPHONE
#    include <mach-o/dyld.h>
#    include <pwd.h>
#  endif
#else
#  include <pwd.h>
#  include <unistd.h>
#endif

#ifndef MINECRAFTC_INSTALL_DATADIR
#  define MINECRAFTC_INSTALL_DATADIR "share"
#endif

namespace {

namespace fs = std::filesystem;

bool isAssetRoot(const fs::path& path) {
    return fs::is_regular_file(path / "shaders" / "vulkan" / "chunk.vert.spv") &&
           fs::is_regular_file(path / "textures" / "definitions" / "blocks.json");
}

#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
fs::path executablePathFromSystem(const char* argv0) {
#if defined(_WIN32)
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) break;
        if (length < buffer.size() - 1)
            return fs::path(std::wstring(buffer.data(), length));
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) == 0)
        return fs::weakly_canonical(buffer.data());
#else
    std::array<char, 4096> buffer{};
    const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length > 0) return fs::path(std::string(buffer.data(), length));
#endif
    if (argv0 && *argv0) return fs::absolute(argv0);
    throw std::runtime_error("Cannot determine executable path");
}
fs::path environmentPath(const char* name) {
    const char* value = std::getenv(name);
    return value && *value ? fs::path(value) : fs::path{};
}

fs::path windowsRoamingData() {
#if defined(_WIN32)
    PWSTR value = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(
            FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &value))) {
        fs::path result(value);
        CoTaskMemFree(value);
        return result;
    }
    if (value) CoTaskMemFree(value);
#endif
    return environmentPath("APPDATA");
}

fs::path unixHomeDirectory() {
    const fs::path environmentHome = environmentPath("HOME");
    if (!environmentHome.empty()) return environmentHome;
#if !defined(_WIN32)
    long suggestedSize = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (suggestedSize < 1024) suggestedSize = 16384;
    std::vector<char> buffer(static_cast<size_t>(suggestedSize));
    passwd entry{};
    passwd* result = nullptr;
    if (getpwuid_r(getuid(), &entry, buffer.data(), buffer.size(), &result) == 0 &&
        result && result->pw_dir && *result->pw_dir) {
        return result->pw_dir;
    }
#endif
    return {};
}
#endif

} // namespace

DesktopPlatform currentDesktopPlatform() {
#if defined(__ANDROID__)
    return DesktopPlatform::Android;
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    return DesktopPlatform::IOS;
#elif defined(_WIN32)
    return DesktopPlatform::Windows;
#elif defined(__APPLE__)
    return DesktopPlatform::MacOS;
#else
    return DesktopPlatform::Linux;
#endif
}

RuntimePaths resolveRuntimePaths(const RuntimePathInputs& inputs) {
    namespace fs = std::filesystem;
    if (inputs.executablePath.empty() || inputs.currentDirectory.empty())
        throw std::runtime_error("Runtime path inputs are incomplete");

    RuntimePaths result;
    const fs::path executableDirectory = inputs.executablePath.parent_path();
    const fs::path installDataDirectory(MINECRAFTC_INSTALL_DATADIR);
    const std::array<fs::path, 4> candidates = {
        executableDirectory.parent_path() / installDataDirectory /
            "minecraftc" / "assets",
        executableDirectory.parent_path() / "Resources" / "assets",
        executableDirectory / "assets",
        inputs.currentDirectory / "assets"
    };
    for (const auto& candidate : candidates) {
        if (isAssetRoot(candidate)) {
            result.assetRoot = fs::weakly_canonical(candidate);
            break;
        }
    }
    if (result.assetRoot.empty()) {
        throw std::runtime_error(
            "Cannot locate MinecraftC assets beside the executable or in the current directory");
    }

    if (fs::is_directory(inputs.currentDirectory / "saves")) {
        result.dataRoot = inputs.currentDirectory;
        return result;
    }

    switch (inputs.platform) {
        case DesktopPlatform::Windows:
            if (!inputs.roamingAppData.empty())
                result.dataRoot = inputs.roamingAppData / "MinecraftC";
            break;
        case DesktopPlatform::MacOS:
            if (!inputs.homeDirectory.empty())
                result.dataRoot = inputs.homeDirectory / "Library" /
                    "Application Support" / "MinecraftC";
            break;
        case DesktopPlatform::Linux:
            if (!inputs.xdgDataHome.empty() && inputs.xdgDataHome.is_absolute())
                result.dataRoot = inputs.xdgDataHome / "minecraftc";
            else if (!inputs.homeDirectory.empty())
                result.dataRoot = inputs.homeDirectory / ".local" / "share" / "minecraftc";
            break;
        case DesktopPlatform::Android:
        case DesktopPlatform::IOS:
            break;
    }
    if (result.dataRoot.empty())
        throw std::runtime_error("Cannot determine a writable user data directory");
    return result;
}

RuntimePaths discoverRuntimePaths(const char* argv0) {
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    (void)argv0;
    RuntimePaths result;
    result.dataRoot = platform::sdl::preferencePath();
    // An empty title-storage override selects the packaged mobile asset namespace.
    result.assetRoot.clear();
    return result;
#else
    RuntimePathInputs inputs;
    inputs.platform = currentDesktopPlatform();
    inputs.executablePath = executablePathFromSystem(argv0);
    inputs.currentDirectory = std::filesystem::current_path();
    inputs.homeDirectory = unixHomeDirectory();
    inputs.xdgDataHome = environmentPath("XDG_DATA_HOME");
    inputs.roamingAppData = windowsRoamingData();
    return resolveRuntimePaths(inputs);
#endif
}

namespace Platform {

bool replaceFileAtomically(const std::filesystem::path& source,
                           const std::filesystem::path& destination,
                           std::error_code& error) {
#if defined(_WIN32)
    if (MoveFileExW(source.c_str(), destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error.clear();
        return true;
    }
    error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    return false;
#else
    std::filesystem::rename(source, destination, error);
    return !error;
#endif
}

bool stdoutSupportsColor() {
#if defined(_WIN32)
    if (!_isatty(_fileno(stdout))) return false;
    const HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (handle == INVALID_HANDLE_VALUE || !GetConsoleMode(handle, &mode)) return false;
    return SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

bool localTime(std::time_t value, std::tm& result) {
#if defined(_WIN32)
    return localtime_s(&result, &value) == 0;
#else
    return localtime_r(&value, &result) != nullptr;
#endif
}

} // namespace Platform
