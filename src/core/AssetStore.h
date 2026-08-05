#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class AssetStore {
public:
    explicit AssetStore(const std::filesystem::path& root, uint64_t readyTimeoutMs = 5000);
    ~AssetStore();
    AssetStore(const AssetStore&) = delete;
    AssetStore& operator=(const AssetStore&) = delete;

    static bool validPath(std::string_view relative);
    std::vector<uint8_t> readBinary(std::string_view relative) const;
    std::string readText(std::string_view relative) const;
    const std::filesystem::path& root() const { return m_root; }
    static std::vector<uint8_t> readPath(const std::filesystem::path& path);
    static std::string readTextPath(const std::filesystem::path& path);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::filesystem::path m_root;
    static const AssetStore* s_current;
};
