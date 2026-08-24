#pragma once

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "game/Item.h"
#include "game/SaveStore.h"
#include "world/Block.h"
#include "world/BlockEntity.h"
#include "world/Structure.h"

class ChunkStore;
class SaveStore;

struct ItemStack;

// Owns player-edit persistence state: per-chunk block overrides and block
// entities, their dirty/pending save queues, and the saved-state bookkeeping
// (applied markers). All mutations share the ChunkStore lock so the
// persistence state stays consistent with chunk loading/unloading; the
// *Unlocked and save methods below expect the caller to already hold it.
class WorldPersistence {
public:
    explicit WorldPersistence(ChunkStore& chunks) : m_chunks(chunks) {}

    void setSaveStore(SaveStore* store) { m_saveStore = store; }

    bool hasModifiedChunks() const {
        return !m_dirtyOverrideChunks.empty() || !m_dirtyBlockEntityChunks.empty() ||
               !m_pendingOverrideSaves.empty() || !m_pendingBlockEntitySaves.empty();
    }
    bool hasPendingModifiedChunkSaves() const {
        return !m_pendingOverrideSaves.empty() ||
               !m_pendingBlockEntitySaves.empty();
    }

    // Record a player block edit: updates the override map and creates or
    // removes the matching block entity. Locks internally.
    void recordOverride(int cx, int cz, uint32_t localIndex, BlockId id);

    // Register a Chest/Furnace block entity for a world-generated work block
    // (structure content). Never creates an override entry; leaves an
    // existing player-created entity untouched. Caller holds the chunk lock.
    void registerGeneratedBlockEntityUnlocked(int cx, int cz,
                                              uint32_t localIndex, BlockId id,
                                              StructureLootProfile lootProfile =
                                                  StructureLootProfile::None,
                                              uint64_t lootSeed = 0);

    // Apply saved overrides for a freshly generated chunk.
    // Caller must hold the ChunkStore lock.
    void applySavedOverridesUnlocked(int cx, int cz);

    // Install disk data obtained by the asynchronous chunk-load bundle. The
    // normal apply/load methods then consume these maps without doing main
    // thread filesystem I/O.
    void installLoadedChunkDataUnlocked(
        int cx, int cz, const std::vector<BlockOverride>& overrides,
        const std::vector<PersistedBlockEntity>& entities);

    // Persist dirty overrides / block entities for one chunk.
    // Caller must hold the ChunkStore lock.
    void saveOverrides(int cx, int cz);
    void saveBlockEntities(int cx, int cz);

    // Load persisted block entities for a freshly generated chunk.
    // Caller must hold the ChunkStore lock.
    void loadBlockEntities(int cx, int cz);

    // Move the dirty sets into the pending-save queues. Locks internally.
    void beginModifiedChunkAutosave();
    // Save up to maxFiles pending chunks; true when everything is persisted.
    // Locks internally.
    bool flushModifiedChunks(size_t maxFiles);

    BlockEntity* getBlockEntity(const glm::ivec3& position);
    const BlockEntity* getBlockEntity(const glm::ivec3& position) const;
    std::vector<ItemStack> takeBlockEntityContents(const glm::ivec3& position);

    // Advance furnace/crafting state for all loaded block entities.
    // Locks internally.
    void tickBlockEntities();

    // Read-only iteration over overrides of generated chunks, with the
    // chunk lock held. fn(key, localIndex, block).
    using OverrideVisitor =
        std::function<void(const std::pair<int, int>&, uint32_t, BlockId)>;
    void forEachOverride(const OverrideVisitor& fn) const;

    // Drop all persistence state (seed reset / world teardown).
    void clear() {
        m_blockOverrides.clear();
        m_dirtyOverrideChunks.clear();
        m_pendingOverrideSaves.clear();
        m_overridesApplied.clear();
        m_blockEntities.clear();
        m_dirtyBlockEntityChunks.clear();
        m_pendingBlockEntitySaves.clear();
        m_blockEntitiesApplied.clear();
        m_prefetchedChunks.clear();
    }

    // Mark a chunk's saved state as loaded (unload bookkeeping).
    void markOverridesApplied(int cx, int cz) { m_overridesApplied.emplace(cx, cz); }
    void eraseBlockEntities(int cx, int cz) { m_blockEntities.erase({cx, cz}); }
    void eraseBlockEntitiesApplied(int cx, int cz) {
        m_blockEntitiesApplied.erase({cx, cz});
    }
    void eraseOverridesApplied(int cx, int cz) {
        m_overridesApplied.erase({cx, cz});
        m_prefetchedChunks.erase({cx, cz});
    }

    // ── Lock-free accessors (caller holds the chunk lock) ────────────
    bool isOverridesApplied(int cx, int cz) const {
        return m_overridesApplied.count({cx, cz}) != 0;
    }
    template <typename F>
    void forEachOverrideInChunkUnlocked(int cx, int cz, F&& fn) const {
        const auto it = m_blockOverrides.find({cx, cz});
        if (it == m_blockOverrides.end()) return;
        for (const auto& [index, block] : it->second) fn(index, block);
    }

private:
    struct PairHash {
        size_t operator()(const std::pair<int, int>& p) const {
            // Shift through uint64_t: left-shifting a negative int64_t is UB.
            return std::hash<uint64_t>{}(
                (static_cast<uint64_t>(static_cast<uint32_t>(p.first)) << 32) |
                static_cast<uint32_t>(p.second));
        }
    };
    using OverrideMap = std::unordered_map<uint32_t, BlockId>;
    using BlockEntityMap = std::unordered_map<uint32_t, BlockEntity>;

    ChunkStore& m_chunks;
    SaveStore* m_saveStore = nullptr;

    std::unordered_map<std::pair<int, int>, OverrideMap, PairHash> m_blockOverrides;
    std::unordered_set<std::pair<int, int>, PairHash> m_dirtyOverrideChunks;
    std::unordered_set<std::pair<int, int>, PairHash> m_pendingOverrideSaves;
    std::unordered_set<std::pair<int, int>, PairHash> m_overridesApplied;
    std::unordered_map<std::pair<int, int>, BlockEntityMap, PairHash> m_blockEntities;
    std::unordered_set<std::pair<int, int>, PairHash> m_dirtyBlockEntityChunks;
    std::unordered_set<std::pair<int, int>, PairHash> m_pendingBlockEntitySaves;
    std::unordered_set<std::pair<int, int>, PairHash> m_blockEntitiesApplied;
    std::unordered_set<std::pair<int, int>, PairHash> m_prefetchedChunks;
};
