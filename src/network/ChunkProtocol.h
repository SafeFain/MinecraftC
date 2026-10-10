#pragma once

#include "network/Protocol.h"
#include "game/GameRules.h"
#include <deque>
#include <map>
#include <optional>
#include <tuple>

namespace Lan {
struct ChunkAddress {
    DimensionId dimension = DimensionId::Overworld;
    int32_t x = 0, z = 0;
    bool operator<(const ChunkAddress& other) const {
        return std::tie(dimension, x, z) < std::tie(other.dimension, other.x, other.z);
    }
};
struct ChunkSnapshot {
    ChunkAddress address;
    uint64_t epoch = 0, revision = 0;
    std::vector<uint16_t> blocks;
};
struct ChunkEdit { uint32_t index = 0; uint16_t block = 0; };
struct ChunkDelta {
    ChunkAddress address;
    uint64_t epoch = 0, base = 0, revision = 0;
    std::vector<ChunkEdit> edits;
};
Bytes encodeChunkSnapshot(const ChunkSnapshot& snapshot);
ChunkSnapshot decodeChunkSnapshot(const Bytes& bytes);
Bytes encodeChunkDelta(const ChunkDelta& delta);
ChunkDelta decodeChunkDelta(const Bytes& bytes);

// Bounded authority history. Missing/gapped/retired history requires a fresh
// snapshot; retaining one full block array per player/chunk is unnecessary.
class ChunkJournal {
public:
    void record(ChunkAddress address, uint64_t revision, ChunkEdit edit);
    std::optional<std::vector<ChunkEdit>> since(ChunkAddress address,
                                              uint64_t base, uint64_t revision) const;
    void erase(ChunkAddress address);
    void clear() { m_history.clear(); m_count = 0; }
    size_t size() const { return m_count; }
private:
    struct Entry { uint64_t revision; ChunkEdit edit; };
    struct History { std::deque<Entry> entries; uint64_t touched = 0; };
    std::map<ChunkAddress, History> m_history;
    uint64_t m_sequence = 0;
    size_t m_count = 0;
};
}
