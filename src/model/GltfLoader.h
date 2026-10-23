#pragma once

#include "model/ModelAsset.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <map>

namespace model {

struct LoadResult {
    std::shared_ptr<const ModelAsset> asset;
    std::string error;
    std::vector<std::string> warnings;

    explicit operator bool() const { return static_cast<bool>(asset); }
};

LoadResult loadGltf(const std::filesystem::path& path);

struct AssetSource {
    std::string namespaceKey;
    std::filesystem::path root;
    std::function<std::vector<uint8_t>(const std::filesystem::path&)> read;
};

LoadResult loadGltf(const AssetSource& source, const std::filesystem::path& relative);

// Application-owned; diagnostics (including failed reads) survive cache hits.
class ModelAssetCache {
public:
    using Loader = std::function<LoadResult(const std::filesystem::path&)>;
    explicit ModelAssetCache(Loader loader = {}) : m_loader(std::move(loader)) {}
    const LoadResult& load(const std::filesystem::path& path) {
        const auto key = std::filesystem::absolute(path).lexically_normal();
        auto found = m_entries.find(key);
        if (found == m_entries.end()) found = m_entries.emplace(key, m_loader ? m_loader(path) : model::loadGltf(path)).first;
        return found->second;
    }
    const LoadResult& load(const AssetSource& source, const std::filesystem::path& path) {
        const auto key = std::make_pair(source.namespaceKey + ":" + source.root.generic_u8string(),
                                        path.lexically_normal().generic_u8string());
        auto found = m_sources.find(key);
        if (found == m_sources.end()) found = m_sources.emplace(key, loadGltf(source, path)).first;
        return found->second;
    }
    void alias(const std::filesystem::path& path, const LoadResult& result) {
        m_entries[std::filesystem::absolute(path).lexically_normal()] = result;
    }
private:
    Loader m_loader;
    std::map<std::filesystem::path, LoadResult> m_entries;
    std::map<std::pair<std::string, std::string>, LoadResult> m_sources;
};

} // namespace model
