#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <tuple>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "entity/EntityRecord.h"
#include "game/GameRules.h"
#include "game/StatusEffects.h"
#include "plugins/Extensions.h"
#include "game/GameRuleRegistry.h"
#include "game/InventoryModel.h"
#include "game/Weather.h"
#include "game/VillagerTrade.h"
#include "world/Block.h"
#include "world/BlockEntity.h"

// Recovery is an explicit decoding policy; ordinary reads remain strict.
enum class SaveReadPolicy { Strict, RecoverPlugins };
struct PluginCompatibilityIssue {
    std::string kind, key, saved, current;
    bool operator<(const PluginCompatibilityIssue& other) const {
        return std::tie(key,kind,saved,current)<std::tie(other.key,other.kind,other.saved,other.current);
    }
    bool operator==(const PluginCompatibilityIssue& other) const {
        return kind==other.kind && key==other.key && saved==other.saved && current==other.current;
    }
};
enum class WorldCompatibilityState { Compatible, PluginMismatch, Blocked };
struct WorldCompatibilityReport {
    std::vector<PluginCompatibilityIssue> issues;
    std::string blocker, token;
    WorldCompatibilityState state() const {
        return !blocker.empty()?WorldCompatibilityState::Blocked:issues.empty()?WorldCompatibilityState::Compatible:WorldCompatibilityState::PluginMismatch;
    }
    bool compatible() const { return state()==WorldCompatibilityState::Compatible; }
    bool recoverable() const { return state()==WorldCompatibilityState::PluginMismatch; }
};

struct WorldMetadata {
    struct DimensionState {
        glm::dvec3 playerPosition{0.0, 120.0, 0.0};
        glm::ivec3 safePosition{0, 120, 0}; // builtin inspection prefix
        glm::dvec3 safePlayerPosition{0.5,120.01,0.5};
        bool hasSafePosition = false;
        bool positioned = false;
        WeatherSaveState weather;
        std::optional<glm::ivec3> bedSpawn;
        std::optional<glm::ivec3> anchorSpawn;
        uint64_t worldTicks = 0;
        // DayNightCycle deliberately stores a plain phase here so the save
        // layer does not depend on renderer headers.
        float dayPhase = 0.04f;
    };

    std::map<uint32_t,Plugins::GenerationConfig> generation{{0,{}}};
    std::map<uint32_t,std::pair<uint32_t,uint32_t>> timePolicies; // cycle override, advancing policy
    std::string displayName;
    std::vector<std::pair<std::string,std::string>> pluginRequirements;
    std::string pluginCompatibilityError;
    bool needsRecoverySpawn=false; // Decode-only: validate landing after terrain loads.
    std::map<uint32_t,std::string> savedDimensionKeys; // Decode-only; never serialized as runtime IDs.
    uint64_t seed = 0;
    uint32_t generationVersion = 0;
    uint32_t rulesetVersion = SURVIVAL_RULESET_VERSION;
    GameMode gameMode = GameMode::Survival;
    Difficulty difficulty = Difficulty::Normal;
    bool cheatsEnabled = false;
    uint64_t worldTicks = 0;
    uint32_t dayNightDurationSeconds = DEFAULT_DAY_NIGHT_DURATION_SECONDS;
    GameRuleSet gameRules;
    WeatherSaveState weather;
    glm::dvec3 playerPosition{0.0, 50.0, 0.0};
    glm::ivec3 worldSpawn{0, 50, 0};
    std::optional<glm::ivec3> bedSpawn;
    float health = 20.0f;
    uint8_t hunger = 20;
    float saturation = 5.0f;
    float exhaustion = 0.0f;
    uint32_t foodTickTimer = 0;
    std::vector<ActiveStatusEffect> effects;
    InventoryModel inventory;
    // Durable entity partition shared with chunk persistence and transfers.
    // The record contract lives in entity/EntityRecord.h; this alias preserves
    // every existing WorldMetadata::PersistedEntity spelling and the save format.
    using PersistedEntity = EntityRecord;
    std::vector<PersistedEntity> entities;
    WorldType worldType = WorldType::Normal;
    DimensionId activeDimension = DimensionId::Overworld;
    float overworldDayPhase = 0.04f;
    DimensionState heaven;
    std::map<uint32_t,DimensionState> dimensions;
};

struct BlockOverride {
    uint32_t localIndex = 0;
    BlockId block = BlockId::AIR;
};

struct ChunkEntityLoadData {
    std::vector<WorldMetadata::PersistedEntity> entities;
    uint32_t populationVersion = 0;
};

// One asynchronous read unit for a resident chunk. The generated terrain is
// optional because a cache miss must fall back to deterministic generation;
// the remaining vectors may be empty when the corresponding partition has
// never been written.
struct ChunkLoadBundle {
    std::optional<std::vector<uint16_t>> generated;
    std::vector<BlockOverride> overrides;
    std::vector<PersistedBlockEntity> blockEntities;
    std::vector<WorldMetadata::PersistedEntity> entities;
    uint32_t entityPopulationVersion = 0;
};

class SaveStore {
public:
    static constexpr const char* RECOVERY_SPAWN_MARKER=".recovery-safe-spawn";
    explicit SaveStore(std::filesystem::path worldDirectory,
                       SaveReadPolicy policy = SaveReadPolicy::Strict,
                       std::vector<PluginCompatibilityIssue>* issues = nullptr);
    WorldCompatibilityReport inspectCompatibility() const;
    // Convert one known durable partition, or copy a private file unchanged.
    void recoverFileTo(const std::filesystem::path& relative, const std::filesystem::path& destination) const;

    const std::filesystem::path& worldDirectory() const { return m_worldDirectory; }
    bool exists() const;
    void indexNetherPortal(const glm::ivec3&) const;
    std::vector<glm::ivec3> netherPortalsNear(const glm::dvec3&,int radius) const;
    void validatePluginFiles() const;
    void saveMetadata(const WorldMetadata& metadata) const;
    // Inspection reports incompatibility without loading or writing the world.
    WorldMetadata loadMetadata(bool inspection = false) const;
    void saveChunkOverrides(int chunkX, int chunkZ,
                            const std::vector<BlockOverride>& overrides) const;
    std::vector<BlockOverride> loadChunkOverrides(int chunkX, int chunkZ) const;
    void saveGeneratedChunk(int chunkX, int chunkZ,
                            const std::vector<uint16_t>& blocks,
                            uint32_t generationVersion,uint64_t generationFingerprint=0) const;
    std::optional<std::vector<uint16_t>> loadGeneratedChunk(
        int chunkX, int chunkZ, uint32_t generationVersion,uint64_t generationFingerprint=0) const;
    ChunkLoadBundle loadChunkLoadBundle(
        int chunkX, int chunkZ, uint32_t generationVersion,uint64_t generationFingerprint=0) const;
    void saveBlockEntities(int chunkX, int chunkZ,
                           const std::vector<PersistedBlockEntity>& entities) const;
    std::vector<PersistedBlockEntity> loadBlockEntities(int chunkX, int chunkZ) const;
    void saveChunkEntities(int chunkX, int chunkZ,
                           const std::vector<WorldMetadata::PersistedEntity>& entities) const;
    std::vector<WorldMetadata::PersistedEntity> loadChunkEntities(int chunkX, int chunkZ) const;
    void saveChunkEntityPopulationVersion(int chunkX, int chunkZ,
                                          uint32_t version) const;
    uint32_t loadChunkEntityPopulationVersion(int chunkX, int chunkZ) const;

private:
    std::filesystem::path m_worldDirectory;
    SaveReadPolicy m_policy;
    std::vector<PluginCompatibilityIssue>* m_issues;

    std::filesystem::path chunkPath(int chunkX, int chunkZ) const;
    std::filesystem::path generatedChunkPath(int chunkX, int chunkZ) const;
    std::filesystem::path blockEntityPath(int chunkX, int chunkZ) const;
    std::filesystem::path entityPath(int chunkX, int chunkZ) const;
    std::filesystem::path entityPopulationPath(int chunkX, int chunkZ) const;
};

// Runs one file per step on the main thread, keeping cancellation/UI responsive
// without executing plugin code or mutating registries on worker threads.
class WorldRecovery {
public:
    WorldRecovery(std::filesystem::path world, const std::string& confirmedToken, bool keepBackup=true);
    ~WorldRecovery();
    WorldRecovery(const WorldRecovery&) = delete;
    WorldRecovery& operator=(const WorldRecovery&) = delete;
    void step();
    void cancel();
    bool done() const { return m_done; }
    bool succeeded() const { return m_succeeded; }
    float progress() const;
    const std::string& error() const { return m_error; }
    const std::filesystem::path& backupPath() const { return m_backup; }
    bool backupCleanupPending() const { return m_succeeded && !m_keepBackup && !m_backup.empty(); }
    static void restoreInterrupted(const std::filesystem::path& saves);
private:
    std::filesystem::path m_world, m_transaction, m_stage, m_backup;
    enum class Phase { Convert, Validate, VerifySource, Publish };
    Phase m_phase=Phase::Convert;
    std::vector<std::filesystem::path> m_files,m_sourceFiles;
    uint64_t m_sourceHash=0;
    size_t m_next=0;
    bool m_done=false, m_succeeded=false, m_published=false;
    bool m_keepBackup=true;
    std::string m_error, m_confirmedToken;
    void rollback();
};
