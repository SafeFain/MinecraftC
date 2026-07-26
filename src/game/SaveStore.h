#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "game/GameRules.h"
#include "game/InventoryModel.h"
#include "world/Block.h"
#include "world/BlockEntity.h"

struct WorldMetadata {
    std::string displayName;
    uint64_t seed = 0;
    uint32_t generationVersion = 0;
    uint32_t rulesetVersion = SURVIVAL_RULESET_VERSION;
    GameMode gameMode = GameMode::Survival;
    Difficulty difficulty = Difficulty::Normal;
    bool cheatsEnabled = false;
    uint64_t worldTicks = 0;
    glm::dvec3 playerPosition{0.0, 50.0, 0.0};
    glm::ivec3 worldSpawn{0, 50, 0};
    std::optional<glm::ivec3> bedSpawn;
    float health = 20.0f;
    uint8_t hunger = 20;
    float saturation = 5.0f;
    float exhaustion = 0.0f;
    InventoryModel inventory;
    struct PersistedEntity {
        uint8_t type = 0;
        glm::dvec3 position{0.0};
        glm::vec3 velocity{0.0f};
        float health = 0.0f;
        float ageSeconds = 0.0f;
        ItemStack item;
        uint32_t behaviorSeed = 0;
        uint8_t flags = 0;
        float projectileDamage = 0.0f;
    };
    std::vector<PersistedEntity> entities;
};

struct BlockOverride {
    uint32_t localIndex = 0;
    BlockId block = BlockId::AIR;
};

class SaveStore {
public:
    explicit SaveStore(std::filesystem::path worldDirectory);

    const std::filesystem::path& worldDirectory() const { return m_worldDirectory; }
    bool exists() const;
    void saveMetadata(const WorldMetadata& metadata) const;
    WorldMetadata loadMetadata() const;
    void saveChunkOverrides(int chunkX, int chunkZ,
                            const std::vector<BlockOverride>& overrides) const;
    std::vector<BlockOverride> loadChunkOverrides(int chunkX, int chunkZ) const;
    void saveGeneratedChunk(int chunkX, int chunkZ,
                            const std::vector<uint8_t>& blocks,
                            uint32_t generationVersion) const;
    std::optional<std::vector<uint8_t>> loadGeneratedChunk(
        int chunkX, int chunkZ, uint32_t generationVersion) const;
    void saveBlockEntities(int chunkX, int chunkZ,
                           const std::vector<PersistedBlockEntity>& entities) const;
    std::vector<PersistedBlockEntity> loadBlockEntities(int chunkX, int chunkZ) const;
    void saveChunkEntities(int chunkX, int chunkZ,
                           const std::vector<WorldMetadata::PersistedEntity>& entities) const;
    std::vector<WorldMetadata::PersistedEntity> loadChunkEntities(int chunkX, int chunkZ) const;

private:
    std::filesystem::path m_worldDirectory;

    std::filesystem::path chunkPath(int chunkX, int chunkZ) const;
    std::filesystem::path generatedChunkPath(int chunkX, int chunkZ) const;
    std::filesystem::path blockEntityPath(int chunkX, int chunkZ) const;
    std::filesystem::path entityPath(int chunkX, int chunkZ) const;
};
