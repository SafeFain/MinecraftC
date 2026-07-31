#include "game/WorldCatalog.h"

#include "world/WorldGenContext.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <system_error>

std::string WorldCatalog::slug(const std::string& displayName) {
    std::string result;
    bool separator = false;
    for (unsigned char character : displayName) {
        if (std::isalnum(character)) {
            result.push_back(static_cast<char>(std::tolower(character)));
            separator = false;
        } else if (!result.empty() && !separator) {
            result.push_back('-');
            separator = true;
        }
    }
    while (!result.empty() && result.back() == '-') result.pop_back();
    return result.empty() ? "world" : result;
}

bool WorldCatalog::validId(const std::string& id) {
    return !id.empty() && id != "." && id != ".." &&
        std::all_of(id.begin(), id.end(), [](unsigned char character) {
            return std::isalnum(character) || character == '-' || character == '_';
        });
}

std::vector<WorldSummary> WorldCatalog::list() const {
    std::vector<WorldSummary> worlds;
    if (!std::filesystem::exists(m_savesDirectory)) return worlds;
    for (const auto& entry : std::filesystem::directory_iterator(m_savesDirectory)) {
        if (!entry.is_directory()) continue;
        const std::string id = entry.path().filename().string();
        if (!validId(id)) continue;
        SaveStore store(entry.path());
        if (!store.exists()) continue;
        try {
            const auto metadata = store.loadMetadata();
            worlds.push_back({id, metadata.displayName, metadata.gameMode,
                              metadata.difficulty, metadata.seed,
                              metadata.worldTicks, metadata.generationVersion,
                              metadata.generationVersion == WorldGenContext::GENERATION_VERSION});
        } catch (const std::runtime_error&) {
            // Invalid worlds stay untouched on disk but are not loadable.
        }
    }
    std::sort(worlds.begin(), worlds.end(),
              [](const WorldSummary& a, const WorldSummary& b) {
                  return a.displayName < b.displayName;
              });
    return worlds;
}

std::string WorldCatalog::create(
    const std::string& displayName, uint64_t seed,
    GameMode mode, Difficulty difficulty, bool cheatsEnabled) const {
    std::filesystem::create_directories(m_savesDirectory);
    const std::string base = slug(displayName);
    std::string id = base;
    for (unsigned suffix = 2;
         std::filesystem::exists(m_savesDirectory / id); ++suffix) {
        id = base + "-" + std::to_string(suffix);
    }
    WorldMetadata metadata;
    metadata.displayName = displayName.empty() ? "New World" : displayName;
    metadata.seed = seed;
    metadata.generationVersion = WorldGenContext::GENERATION_VERSION;
    metadata.gameMode = mode;
    metadata.difficulty = difficulty;
    metadata.cheatsEnabled = cheatsEnabled;
    SaveStore(m_savesDirectory / id).saveMetadata(metadata);
    return id;
}

SaveStore WorldCatalog::open(const std::string& id) const {
    if (!validId(id)) throw std::runtime_error("Invalid world id");
    SaveStore store(m_savesDirectory / id);
    if (!store.exists()) throw std::runtime_error("World does not exist");
    return store;
}

bool WorldCatalog::deleteWorld(const std::string& id) const {
    if (!validId(id)) throw std::runtime_error("Invalid world id");
    const std::filesystem::path target = m_savesDirectory / id;
    std::error_code error;
    const auto status = std::filesystem::symlink_status(target, error);
    if (error || !std::filesystem::exists(status)) return false;
    if (std::filesystem::is_symlink(status) ||
        !std::filesystem::is_directory(status)) {
        throw std::runtime_error("World path is not a direct save directory");
    }
    if (!SaveStore(target).exists())
        throw std::runtime_error("World metadata does not exist");
    const auto removed = std::filesystem::remove_all(target, error);
    if (error) throw std::runtime_error("Could not delete world: " + error.message());
    return removed > 0;
}
