#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <vector>

class Chunk;
class SaveStore;

// Owns the loaded chunk map and the mutex that guards it, plus the
// near-to-far active-chunk list used by rendering and weather ticks.
//
// All public operations lock internally, so callers can safely query from
// worker threads. The explicit *Unlocked operations and withShared/withUnique
// callbacks exist for callers that must span several operations under one
// lock (e.g. unload sequences); inside a callback use only *Unlocked
// operations, never the locking ones.
class ChunkStore {
public:
    ChunkStore() = default;

    void setSaveStore(SaveStore* store) { m_saveStore = store; }

    // Get or create the chunk at (cx, cz), loading a cached copy from the
    // save store when available. Generation is deferred to the generation
    // pipeline; a fresh chunk starts all-AIR. The returned pointer stays
    // valid until the chunk is erased from this store.
    Chunk* get(int cx, int cz);

    // Locked lookups; return nullptr when the chunk is not loaded.
    Chunk* find(int cx, int cz);
    const Chunk* find(int cx, int cz) const;
    bool contains(int cx, int cz) const;
    bool isGenerated(int cx, int cz) const;

    void markDirty(int cx, int cz);
    void erase(int cx, int cz);
    void clear();
    // Rebuild the active-chunk list sorted near-to-far from (pcx, pcz).
    // Main thread only; the list is read lock-free by renderers.
    void rebuildActiveChunks(int pcx, int pcz);
    const std::vector<Chunk*>& activeChunks() const { return m_activeChunks; }
    size_t size() const;

    // Locked iteration. The callback receives this store; use only the
    // *Unlocked operations inside. Shared mode is the common read path;
    // unique mode spans mutation sequences (e.g. unload + erase).
    template <typename F>
    void withShared(F&& fn) const {
        std::shared_lock lock(m_mutex);
        fn(const_cast<ChunkStore&>(*this));
    }
    template <typename F>
    void withUnique(F&& fn) {
        std::unique_lock lock(m_mutex);
        fn(*this);
    }

    // ── Unlocked operations (the caller holds the lock) ─────────────
    Chunk* findUnlocked(int cx, int cz);
    const Chunk* findUnlocked(int cx, int cz) const;
    void eraseUnlocked(int cx, int cz);
    void clearUnlocked();
    template <typename F>
    void forEachSharedUnlocked(F&& fn) const {
        for (const auto& [key, chunk] : m_chunks) fn(chunk.get());
    }
    template <typename F>
    void forEachUniqueUnlocked(F&& fn) {
        for (auto& [key, chunk] : m_chunks) fn(chunk.get());
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
    using ChunkMap =
        std::unordered_map<std::pair<int, int>, std::unique_ptr<Chunk>,
                           PairHash>;

    ChunkMap m_chunks;
    std::vector<Chunk*> m_activeChunks;
    mutable std::shared_mutex m_mutex;
    SaveStore* m_saveStore = nullptr;
};
