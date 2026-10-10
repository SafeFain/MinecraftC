#pragma once

#include "network/Protocol.h"
#include "network/ContentIds.h"
#include "world/LodTerrainSystem.h"
#include "world/Block.h"

namespace Lan {
constexpr size_t MAX_LOD_SUBSCRIPTIONS = 4096;
struct LodUpdate {
    DimensionId dimension = DimensionId::Overworld;
    uint64_t epoch = 1, revision = 0;
    LodTileKey key;
    // Only edited representative columns are exact; the other columns retain
    // the client's seeded approximation.
    LodTileData columns;
};
inline void validateLodKey(const LodTileKey& key) {
    if (key.level > 12) throw ProtocolError("Invalid LOD level");
    const int64_t side = int64_t{16} << key.level;
    if (int64_t{key.x} * side < -30000000 || (int64_t{key.x} + 1) * side > 30000000 ||
        int64_t{key.z} * side < -30000000 || (int64_t{key.z} + 1) * side > 30000000)
        throw ProtocolError("Invalid LOD coordinates");
}
inline Bytes encodeLodUpdate(const LodUpdate& value, bool includeColumns) {
    validateLodKey(value.key);
    if (value.dimension > DimensionId::Heaven || !value.epoch) throw ProtocolError("Invalid LOD stream");
    Writer w; w.u8(1); w.u8(static_cast<uint8_t>(value.dimension)); w.u64(value.epoch); w.u64(value.revision);
    w.u32(static_cast<uint32_t>(value.key.x)); w.u32(static_cast<uint32_t>(value.key.z)); w.u8(value.key.level);
    if (includeColumns) for (const auto& column : value.columns.columns) {
        if (column.spans.size() > Config::CHUNK_SIZE_Y || (!column.exact && !column.spans.empty())) throw ProtocolError("Invalid LOD column");
        w.u8(column.exact); w.u16(static_cast<uint16_t>(column.spans.size()));
        int top = Config::WORLD_MIN_Y - 1;
        for (const auto& span : column.spans) {
            if (span.bottom <= top || span.bottom > span.top || span.top >= Config::WORLD_MAX_Y ||
                span.block == BlockId::AIR || !Plugins::validBlock(span.block)) throw ProtocolError("Invalid LOD span");
            w.u16(static_cast<uint16_t>(span.bottom)); w.u16(static_cast<uint16_t>(span.top)); w.u16(encodeBlockId(static_cast<uint16_t>(span.block))); top = span.top;
        }
    }
    return std::move(w.bytes);
}
inline LodUpdate decodeLodUpdate(const Bytes& bytes, bool includeColumns) {
    Reader r(bytes); LodUpdate value;
    if (r.u8() != 1) throw ProtocolError("Unknown LOD schema");
    value.dimension = static_cast<DimensionId>(r.u8()); value.epoch = r.u64(); value.revision = r.u64();
    value.key.x = static_cast<int32_t>(r.u32()); value.key.z = static_cast<int32_t>(r.u32()); value.key.level = r.u8();
    if (includeColumns) for (auto& column : value.columns.columns) {
        const auto exact = r.u8(); const auto count = r.u16();
        if (exact > 1 || count > Config::CHUNK_SIZE_Y) throw ProtocolError("Invalid LOD columns");
        column.exact = exact != 0;
        for (uint16_t i = 0; i < count; ++i) column.spans.push_back({static_cast<int16_t>(r.u16()), static_cast<int16_t>(r.u16()), static_cast<BlockId>(decodeBlockId(r.u16()))});
    }
    r.finish(); (void)encodeLodUpdate(value, includeColumns); return value;
}
inline bool lodContainsChunk(const LodTileKey& key, int x, int z) {
    const int64_t side = int64_t{1} << key.level;
    return x >= int64_t{key.x} * side && x < (int64_t{key.x} + 1) * side &&
           z >= int64_t{key.z} * side && z < (int64_t{key.z} + 1) * side;
}
}
