#include "world/ChunkStore.h"

#include "Config.h"
#include "world/Chunk.h"

#include <algorithm>

Chunk* ChunkStore::get(int cx, int cz) {
    const std::pair<int, int> key{cx, cz};

    {
        std::shared_lock lock(m_mutex);
        auto it = m_chunks.find(key);
        if (it != m_chunks.end()) return it->second.get();
    }

    std::unique_lock lock(m_mutex);
    auto it = m_chunks.find(key);
    if (it != m_chunks.end()) return it->second.get();

    auto chunk = std::make_unique<Chunk>(cx, cz);
    // Cache probing/loading is asynchronous in ChunkStreamer.  Keeping this
    // constructor path allocation-only removes filesystem latency from the
    // main thread and makes a cache miss indistinguishable from a new chunk.
    Chunk* ptr = chunk.get();
    m_chunks[key] = std::move(chunk);
    return ptr;
}

Chunk* ChunkStore::find(int cx, int cz) {
    std::shared_lock lock(m_mutex);
    return findUnlocked(cx, cz);
}

const Chunk* ChunkStore::find(int cx, int cz) const {
    std::shared_lock lock(m_mutex);
    return findUnlocked(cx, cz);
}

bool ChunkStore::contains(int cx, int cz) const {
    std::shared_lock lock(m_mutex);
    return m_chunks.find({cx, cz}) != m_chunks.end();
}

bool ChunkStore::isGenerated(int cx, int cz) const {
    std::shared_lock lock(m_mutex);
    const auto it = m_chunks.find({cx, cz});
    return it != m_chunks.end() && it->second->generated.load();
}

void ChunkStore::markDirty(int cx, int cz) {
    std::shared_lock lock(m_mutex);
    const auto it = m_chunks.find({cx, cz});
    if (it != m_chunks.end()) it->second->markDirty();
}

void ChunkStore::erase(int cx, int cz) {
    std::unique_lock lock(m_mutex);
    eraseUnlocked(cx, cz);
}

void ChunkStore::clear() {
    std::unique_lock lock(m_mutex);
    clearUnlocked();
}

void ChunkStore::clearUnlocked() {
    m_chunks.clear();
    m_activeChunks.clear();
}

void ChunkStore::rebuildActiveChunks(
    int pcx, int pcz, const std::unordered_set<uint64_t>* visible) {
    std::shared_lock lock(m_mutex);
    m_activeChunks.clear();
    m_activeChunks.reserve(m_chunks.size());
    for (auto& [key, chunk] : m_chunks) {
        if (visible != nullptr) {
            const uint64_t packed =
                (static_cast<uint64_t>(static_cast<uint32_t>(key.first)) << 32) |
                static_cast<uint32_t>(key.second);
            if (visible->count(packed) == 0) continue;
        }
        m_activeChunks.push_back(chunk.get());
    }
    std::sort(m_activeChunks.begin(), m_activeChunks.end(),
              [pcx, pcz](const Chunk* a, const Chunk* b) {
                  const int64_t adx = static_cast<int64_t>(a->cx) - pcx;
                  const int64_t adz = static_cast<int64_t>(a->cz) - pcz;
                  const int64_t bdx = static_cast<int64_t>(b->cx) - pcx;
                  const int64_t bdz = static_cast<int64_t>(b->cz) - pcz;
                  return adx * adx + adz * adz < bdx * bdx + bdz * bdz;
              });
}

size_t ChunkStore::size() const {
    std::shared_lock lock(m_mutex);
    return m_chunks.size();
}

Chunk* ChunkStore::findUnlocked(int cx, int cz) {
    const auto it = m_chunks.find({cx, cz});
    return it == m_chunks.end() ? nullptr : it->second.get();
}

const Chunk* ChunkStore::findUnlocked(int cx, int cz) const {
    const auto it = m_chunks.find({cx, cz});
    return it == m_chunks.end() ? nullptr : it->second.get();
}

void ChunkStore::eraseUnlocked(int cx, int cz) {
    m_chunks.erase({cx, cz});
}
