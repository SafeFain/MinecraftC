#pragma once

#include <filesystem>
#include <string>

// Preserve UTF-8 bytes at char-based API boundaries, independently of native encoding.
inline std::string pathToUtf8(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

inline std::string genericPathToUtf8(const std::filesystem::path& path) {
    const auto bytes = path.generic_u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
