#include "network/ChunkProtocol.h"
#include "Config.h"
#include "world/Block.h"
#include <algorithm>
#include <set>

namespace Lan {
namespace {
constexpr size_t MAX_DELTA_EDITS = 4096;
void addressValid(const ChunkAddress& address) {
    if (address.dimension > DimensionId::Heaven ||
        std::abs(static_cast<int64_t>(address.x)) > 1875000 ||
        std::abs(static_cast<int64_t>(address.z)) > 1875000)
        throw ProtocolError("Invalid chunk address");
}
void blockValid(uint16_t block) {
    if (block >= static_cast<uint16_t>(BlockId::COUNT)) throw ProtocolError("Invalid LAN block ID");
}
void writeAddress(Writer& writer, const ChunkAddress& address, uint64_t epoch) {
    addressValid(address);
    if (!epoch) throw ProtocolError("Invalid chunk stream epoch");
    writer.u8(1); writer.u8(static_cast<uint8_t>(address.dimension));
    writer.u64(epoch); writer.u32(static_cast<uint32_t>(address.x)); writer.u32(static_cast<uint32_t>(address.z));
}
ChunkAddress readAddress(Reader& reader, uint64_t& epoch) {
    if (reader.u8() != 1) throw ProtocolError("Unknown chunk schema");
    ChunkAddress address;
    address.dimension = static_cast<DimensionId>(reader.u8());
    epoch = reader.u64(); address.x = static_cast<int32_t>(reader.u32()); address.z = static_cast<int32_t>(reader.u32());
    addressValid(address);
    if (!epoch) throw ProtocolError("Invalid chunk stream epoch");
    return address;
}
}
Bytes encodeChunkSnapshot(const ChunkSnapshot& value) {
    if (!value.revision || value.blocks.size() != Config::CHUNK_VOLUME) throw ProtocolError("Invalid chunk snapshot");
    Writer writer; writeAddress(writer, value.address, value.epoch); writer.u64(value.revision);
    for (size_t start = 0; start < value.blocks.size();) {
        const auto block = value.blocks[start]; blockValid(block);
        size_t end = start + 1;
        while (end < value.blocks.size() && value.blocks[end] == block) ++end;
        writer.u32(static_cast<uint32_t>(end - start)); writer.u16(block);
        start = end;
    }
    return std::move(writer.bytes);
}
ChunkSnapshot decodeChunkSnapshot(const Bytes& bytes) {
    Reader reader(bytes); ChunkSnapshot value;
    value.address = readAddress(reader, value.epoch); value.revision = reader.u64();
    if (!value.revision) throw ProtocolError("Invalid chunk revision");
    value.blocks.reserve(Config::CHUNK_VOLUME);
    while (value.blocks.size() < Config::CHUNK_VOLUME) {
        const uint32_t count = reader.u32(); const uint16_t block = reader.u16(); blockValid(block);
        if (!count || count > Config::CHUNK_VOLUME - value.blocks.size()) throw ProtocolError("Invalid block run");
        value.blocks.insert(value.blocks.end(), count, block);
    }
    reader.finish(); return value;
}
Bytes encodeChunkDelta(const ChunkDelta& value) {
    if (!value.base || value.revision <= value.base || value.edits.empty() || value.edits.size() > MAX_DELTA_EDITS)
        throw ProtocolError("Invalid chunk delta");
    Writer writer; writeAddress(writer, value.address, value.epoch);
    writer.u64(value.base); writer.u64(value.revision); writer.u32(static_cast<uint32_t>(value.edits.size()));
    std::set<uint32_t> indices;
    for (const auto& edit : value.edits) {
        if (edit.index >= Config::CHUNK_VOLUME || !indices.insert(edit.index).second) throw ProtocolError("Invalid delta index");
        blockValid(edit.block); writer.u32(edit.index); writer.u16(edit.block);
    }
    return std::move(writer.bytes);
}
ChunkDelta decodeChunkDelta(const Bytes& bytes) {
    Reader reader(bytes); ChunkDelta value;
    value.address = readAddress(reader, value.epoch); value.base = reader.u64(); value.revision = reader.u64();
    const uint32_t count = reader.u32();
    if (!value.base || value.revision <= value.base || !count || count > MAX_DELTA_EDITS)
        throw ProtocolError("Invalid chunk delta");
    value.edits.reserve(count); std::set<uint32_t> indices;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t index = reader.u32(); const uint16_t block = reader.u16(); blockValid(block);
        if (index >= Config::CHUNK_VOLUME || !indices.insert(index).second) throw ProtocolError("Invalid delta index");
        value.edits.push_back({index, block});
    }
    reader.finish(); return value;
}
void ChunkJournal::record(ChunkAddress address, uint64_t revision, ChunkEdit edit) {
    auto& history = m_history[address]; history.touched = ++m_sequence;
    if (!history.entries.empty() && revision <= history.entries.back().revision) {
        m_count -= history.entries.size(); history.entries.clear();
    }
    history.entries.push_back({revision, edit}); ++m_count;
    if (history.entries.size() > 512) { history.entries.pop_front(); --m_count; }
    while (m_count > 32768) {
        auto oldest = std::min_element(m_history.begin(), m_history.end(),
            [](const auto& a, const auto& b) { return a.second.touched < b.second.touched; });
        m_count -= oldest->second.entries.size(); m_history.erase(oldest);
    }
}
std::optional<std::vector<ChunkEdit>> ChunkJournal::since(ChunkAddress address, uint64_t base, uint64_t revision) const {
    const auto found = m_history.find(address);
    if (found == m_history.end() || revision <= base) return std::nullopt;
    uint64_t expected = base;
    std::map<uint32_t, uint16_t> finalEdits;
    for (const auto& entry : found->second.entries) {
        if (entry.revision <= base) continue;
        if (entry.revision > revision) break;
        if (expected == UINT64_MAX || entry.revision != expected + 1) return std::nullopt;
        expected = entry.revision; finalEdits[entry.edit.index] = entry.edit.block;
    }
    if (expected != revision) return std::nullopt;
    std::vector<ChunkEdit> edits;
    for (const auto& entry : finalEdits) edits.push_back({entry.first, entry.second});
    return edits;
}
void ChunkJournal::erase(ChunkAddress address) {
    const auto found = m_history.find(address);
    if (found == m_history.end()) return;
    m_count -= found->second.entries.size(); m_history.erase(found);
}
}
