#pragma once
#include "network/ChunkProtocol.h"
#include "network/ContentIds.h"
#include "world/PistonMotion.h"

namespace Lan {
// All chunk snapshots and motions in a packet are validated before publication.
// This travels on the ordered chunk stream, alongside ordinary snapshots/deltas.
struct BlockTransaction {
    DimensionId dimension=DimensionId::Overworld;
    uint64_t epoch=0,tick=0;
    std::vector<ChunkSnapshot> chunks;
    std::vector<Redstone::PistonMotion> motions;
};
inline Bytes encodeBlockTransaction(const BlockTransaction& value) {
    if(!value.epoch || value.chunks.size()>255 || value.motions.size()>4096)throw ProtocolError("Invalid block transaction");
    Writer w;w.u8(1);writeDimension(w,value.dimension);w.u64(value.epoch);w.u64(value.tick);
    w.u8(static_cast<uint8_t>(value.chunks.size()));
    std::set<ChunkAddress> addresses;
    for(const auto& chunk:value.chunks) {
        if(chunk.address.dimension!=value.dimension || chunk.epoch!=value.epoch || !addresses.insert(chunk.address).second)throw ProtocolError("Mismatched transaction chunk");
        auto bytes=encodeChunkSnapshot(chunk);w.u32(static_cast<uint32_t>(bytes.size()));w.raw(bytes);
    }
    w.u16(static_cast<uint16_t>(value.motions.size()));std::set<uint64_t> ids;
    const auto position=[&](Redstone::Position p){w.u32(static_cast<uint32_t>(p.x));w.u32(static_cast<uint32_t>(p.y));w.u32(static_cast<uint32_t>(p.z));};
    for(const auto& motion:value.motions) {
        if(!Redstone::validMotion(motion)||!ids.insert(motion.id).second)throw ProtocolError("Invalid piston motion");
        w.u64(motion.id);position(motion.base);w.u16(Redstone::encode(motion.piston));w.u8(motion.extending?1:0);w.u8(motion.age);
        w.u8(static_cast<uint8_t>(motion.moves.size()));
        for(const auto& move:motion.moves){position(move.from);position(move.to);w.u16(encodeBlockId(move.block));}
        w.u8(motion.destroyed?1:0);if(motion.destroyed)position(*motion.destroyed);
        w.u8(static_cast<uint8_t>(motion.additionalDestroy.size()));for(const auto p:motion.additionalDestroy)position(p);
    }
    if(w.bytes.size()>MAX_QUEUED_BYTES)throw ProtocolError("Block transaction exceeds assembly bounds");
    return std::move(w.bytes);
}
inline BlockTransaction decodeBlockTransaction(const Bytes& bytes) {
    Reader r(bytes);BlockTransaction value;
    if(r.u8()!=1)throw ProtocolError("Unknown block transaction schema");
    value.dimension=readDimension(r);value.epoch=r.u64();value.tick=r.u64();
    const auto chunks=r.u8();
    for(unsigned i=0;i<chunks;++i) {
        const auto length=r.u32();if(length>r.remaining())throw ProtocolError("Truncated transaction chunk");
        Bytes chunk;chunk.reserve(length);for(uint32_t j=0;j<length;++j)chunk.push_back(r.u8());
        value.chunks.push_back(decodeChunkSnapshot(chunk));
    }
    const auto count=r.u16();if(count>4096)throw ProtocolError("Too many transaction motions");
    const auto position=[&](){Redstone::Position p;p.x=static_cast<int32_t>(r.u32());p.y=static_cast<int32_t>(r.u32());p.z=static_cast<int32_t>(r.u32());return p;};
    for(unsigned i=0;i<count;++i) {
        Redstone::PistonMotion motion;motion.id=r.u64();motion.base=position();
        const auto state=Redstone::decode(r.u16());if(!state)throw ProtocolError("Invalid piston state");motion.piston=*state;
        const auto flag=r.u8();if(flag>1)throw ProtocolError("Invalid piston flag");motion.extending=flag!=0;motion.age=r.u8();
        const auto moves=r.u8();if(moves>12)throw ProtocolError("Too many piston moves");
        for(unsigned j=0;j<moves;++j){Redstone::Move move;move.from=position();move.to=position();move.block=decodeBlockId(r.u16());motion.moves.push_back(move);}
        const auto destroyed=r.u8();if(destroyed>1)throw ProtocolError("Invalid piston destroy flag");if(destroyed)motion.destroyed=position();
        const auto extra=r.u8();if(extra>2)throw ProtocolError("Too many paired piston breaks");
        for(unsigned j=0;j<extra;++j)motion.additionalDestroy.push_back(position());
        value.motions.push_back(std::move(motion));
    }
    r.finish();(void)encodeBlockTransaction(value);return value;
}
inline std::vector<Bytes> encodeBlockTransactionParts(const BlockTransaction& value,uint64_t id) {
    if(!id)throw ProtocolError("Invalid transaction sequence");
    const auto payload=encodeBlockTransaction(value);
    constexpr size_t fragment=MAX_PAYLOAD-32;
    const auto count=(payload.size()+fragment-1)/fragment;
    std::vector<Bytes> result;
    for(size_t part=0;part<count;++part) {
        Writer w;w.u8(2);w.u64(value.epoch);w.u64(id);w.u8(static_cast<uint8_t>(part));w.u8(static_cast<uint8_t>(count));w.u32(static_cast<uint32_t>(payload.size()));
        const auto begin=part*fragment,end=std::min(payload.size(),begin+fragment);
        w.bytes.insert(w.bytes.end(),payload.begin()+begin,payload.begin()+end);result.push_back(std::move(w.bytes));
    }
    return result;
}
class BlockTransactionAssembler {
public:
    void clear(){m_epoch=m_completed=m_id=0;m_next=m_count=0;m_total=0;m_bytes.clear();}
    std::optional<BlockTransaction> push(const Bytes& part) {
        Reader r(part);if(r.u8()!=2)throw ProtocolError("Unknown transaction fragment schema");
        const auto epoch=r.u64(),id=r.u64();const auto index=r.u8(),count=r.u8();const auto total=r.u32();
        if(!epoch || !id || !count || count>5 || index>=count || !total || total>MAX_QUEUED_BYTES)throw ProtocolError("Invalid transaction fragment");
        if(epoch!=m_epoch){clear();m_epoch=epoch;}
        if(id<=m_completed)return {};
        if(!m_id){if(index)throw ProtocolError("Missing first transaction fragment");m_id=id;m_count=count;m_total=total;}
        if(id!=m_id || count!=m_count || total!=m_total || index>m_next)throw ProtocolError("Out-of-order transaction fragment");
        if(index<m_next)return {}; // duplicate has no world side effects
        if(r.remaining()>m_total-m_bytes.size())throw ProtocolError("Oversized transaction fragment");
        while(r.remaining())m_bytes.push_back(r.u8());++m_next;
        if(m_next!=m_count)return {};
        if(m_bytes.size()!=m_total)throw ProtocolError("Truncated transaction assembly");
        auto result=decodeBlockTransaction(m_bytes);if(result.epoch!=m_epoch)throw ProtocolError("Mismatched transaction epoch");
        m_completed=m_id;m_id=0;m_next=m_count=0;m_total=0;m_bytes.clear();return result;
    }
private:
    uint64_t m_epoch=0,m_completed=0,m_id=0;
    uint8_t m_next=0,m_count=0;
    size_t m_total=0;
    Bytes m_bytes;
};
} // namespace Lan
