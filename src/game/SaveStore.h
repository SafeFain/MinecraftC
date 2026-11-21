#pragma once

#include "game/ChunkSaveData.h"
#include "game/WorldCompatibility.h"
#include "game/WorldMetadata.h"
#include "world/PistonMotion.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

// Recovery is an explicit decoding policy; ordinary reads remain strict.
enum class SaveReadPolicy { Strict, RecoverPlugins };

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
    void savePistonMotion(const Redstone::PistonMotion&) const;
    void erasePistonMotion(uint64_t id) const;
    void savePistonMotions(const std::vector<Redstone::PistonMotion>& motions) const;
    std::vector<Redstone::PistonMotion> loadPistonMotions() const;
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
    std::shared_ptr<std::mutex> m_pistonMutex=std::make_shared<std::mutex>();
    void writePistonJournal(const std::filesystem::path&,const std::vector<Redstone::PistonMotion>&) const;
    std::vector<Redstone::PistonMotion> readPistonJournal(const std::filesystem::path&) const;
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
