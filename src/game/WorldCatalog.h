#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <utility>

#include "game/SaveStore.h"

struct WorldSummary {
    std::string id;
    std::string displayName;
    GameMode mode = GameMode::Survival;
    Difficulty difficulty = Difficulty::Normal;
    uint64_t seed = 0;
    uint64_t worldTicks = 0;
};

class WorldCatalog {
public:
    explicit WorldCatalog(std::filesystem::path savesDirectory = "saves")
        : m_savesDirectory(std::move(savesDirectory)) {}

    std::vector<WorldSummary> list() const;
    std::string create(const std::string& displayName, uint64_t seed,
                       GameMode mode, Difficulty difficulty,
                       bool cheatsEnabled = false) const;
    SaveStore open(const std::string& id) const;

private:
    std::filesystem::path m_savesDirectory;

    static std::string slug(const std::string& displayName);
    static bool validId(const std::string& id);
};
