#pragma once

#include "model/ModelAsset.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace model {

struct LoadResult {
    std::shared_ptr<const ModelAsset> asset;
    std::string error;
    std::vector<std::string> warnings;

    explicit operator bool() const { return static_cast<bool>(asset); }
};

LoadResult loadGltf(const std::filesystem::path& path);

} // namespace model
