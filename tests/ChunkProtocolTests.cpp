#include "network/ChunkProtocol.h"
#include "network/LodProtocol.h"
#include "network/EventProtocol.h"
#include "world/Block.h"
#include "Config.h"
#include <iostream>
#include <limits>
#include <stdexcept>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F function) {
    try { function(); } catch (const Lan::ProtocolError&) { return; }
    throw std::runtime_error("Malformed chunk payload accepted");
}
int main() {
    Lan::ChunkSnapshot snapshot;
    snapshot.address = {DimensionId::Heaven, -1234, 5678};
    snapshot.epoch = 9; snapshot.revision = 77;
    snapshot.blocks.resize(Config::CHUNK_VOLUME);
    for (size_t i = 0; i < snapshot.blocks.size(); ++i)
        snapshot.blocks[i] = static_cast<uint16_t>(i % static_cast<size_t>(BlockId::COUNT));
    auto bytes = Lan::encodeChunkSnapshot(snapshot);
    check(bytes.size() < Lan::MAX_PAYLOAD, "Worst-case RLE exceeds bounded frame payload");
    const auto roundtrip = Lan::decodeChunkSnapshot(bytes);
    check(roundtrip.blocks == snapshot.blocks && roundtrip.address.x == -1234 && roundtrip.address.z == 5678 &&
          roundtrip.address.dimension == DimensionId::Heaven && roundtrip.epoch == 9 && roundtrip.revision == 77,
          "Snapshot roundtrip differs");
    for (size_t size : {size_t(0), size_t(18), bytes.size() - 1}) {
        auto truncated = bytes; truncated.resize(size); rejects([&] { Lan::decodeChunkSnapshot(truncated); });
    }
    auto bad = bytes; bad[26] = 0xff; bad[27] = 0xff; bad[28] = 0xff; bad[29] = 0xff;
    rejects([&] { Lan::decodeChunkSnapshot(bad); });
    bytes.push_back(0); rejects([&] { Lan::decodeChunkSnapshot(bytes); });
    Lan::ChunkDelta delta;
    delta.address = snapshot.address; delta.epoch = 9; delta.base = 77; delta.revision = 79;
    delta.edits = {{0, static_cast<uint16_t>(BlockId::GLASS)}, {Config::CHUNK_VOLUME - 1, 0}};
    const auto decoded = Lan::decodeChunkDelta(Lan::encodeChunkDelta(delta));
    check(decoded.edits.size() == 2 && decoded.base == 77 && decoded.revision == 79 &&
          decoded.epoch == delta.epoch && decoded.address.x == delta.address.x &&
          decoded.address.z == delta.address.z && decoded.address.dimension == delta.address.dimension,
          "Delta metadata roundtrip differs");
    for (size_t i=0;i<delta.edits.size();++i)
        check(decoded.edits[i].index==delta.edits[i].index && decoded.edits[i].block==delta.edits[i].block,
              "Every delta edit index and block survives encoding");
    // Frozen little-endian schema 1, Heaven (-1,2), epoch 9, base 77,
    // revision 79, one AIR edit at index 513. Independent of Writer/Reader.
    const Lan::Bytes fixture={1,1,9,0,0,0,0,0,0,0,255,255,255,255,2,0,0,0,
        77,0,0,0,0,0,0,0,79,0,0,0,0,0,0,0,1,0,0,0,1,2,0,0,0,0};
    Lan::ChunkDelta golden;golden.address={DimensionId::Heaven,-1,2};golden.epoch=9;
    golden.base=77;golden.revision=79;golden.edits={{513,0}};
    check(Lan::encodeChunkDelta(golden)==fixture,"Delta bytes match frozen schema fixture");
    const auto frozen=Lan::decodeChunkDelta(fixture);
    check(frozen.address.dimension==DimensionId::Heaven && frozen.address.x==-1 && frozen.address.z==2 &&
          frozen.epoch==9 && frozen.base==77 && frozen.revision==79 && frozen.edits.size()==1 &&
          frozen.edits[0].index==513 && frozen.edits[0].block==0,"Decoder reads independent wire fixture");
    for(size_t size=0;size<fixture.size();++size){auto cut=fixture;cut.resize(size);rejects([&]{Lan::decodeChunkDelta(cut);});}
    auto trailing=fixture;trailing.push_back(0);rejects([&]{Lan::decodeChunkDelta(trailing);});
    auto invalid=golden;invalid.epoch=0;rejects([&]{Lan::encodeChunkDelta(invalid);});
    invalid=golden;invalid.revision=invalid.base;rejects([&]{Lan::encodeChunkDelta(invalid);});
    invalid=golden;invalid.edits[0].index=Config::CHUNK_VOLUME;rejects([&]{Lan::encodeChunkDelta(invalid);});
    delta.edits.push_back(delta.edits.front()); rejects([&] { Lan::encodeChunkDelta(delta); });
    Lan::ChunkJournal journal;
    journal.record(snapshot.address, 78, {0, 1}); journal.record(snapshot.address, 79, {0, 2});
    const auto compacted = journal.since(snapshot.address, 77, 79);
    check(compacted && compacted->size() == 1 && compacted->front().block == 2, "Journal coalesces latest block state");
    journal.record(snapshot.address, 81, {1, 3});
    check(!journal.since(snapshot.address, 79, 81), "History gap must require resnapshot");
    for (uint64_t revision = 82; revision < 600; ++revision) journal.record(snapshot.address, revision, {0, 2});
    check(journal.size() == 512 && !journal.since(snapshot.address, 77, 599), "Per-chunk history is bounded");
    for (int chunk = 0; chunk < 100; ++chunk)
        for (uint64_t revision = 1; revision <= 512; ++revision)
            journal.record({DimensionId::Overworld, chunk, 0}, revision, {0, 1});
    check(journal.size() <= 32768, "Global history is bounded independently of exploration");
    journal.clear(); check(journal.size() == 0, "Journal clear retains no history");
    Lan::LodUpdate lod; lod.key = {-4, -5, 3}; lod.revision = 2; lod.dimension=DimensionId::Heaven;lod.epoch=19;
    lod.columns.at(0,0).exact = true; lod.columns.at(0,0).spans = {{-64, -20, BlockId::STONE}, {100, 101, BlockId::GLASS}};
    const auto lodBytes = Lan::encodeLodUpdate(lod, true);
    const auto lodCopy = Lan::decodeLodUpdate(lodBytes, true);
    check(lodCopy.key == lod.key && lodCopy.columns.at(0,0).spans[0].bottom == -64, "LOD negative coordinates and intervals survive framing");
    check(lodCopy.dimension==lod.dimension && lodCopy.epoch==lod.epoch && lodCopy.revision==lod.revision,
          "LOD stream metadata survives framing");
    for(size_t column=0;column<lod.columns.columns.size();++column) {
        const auto& expected=lod.columns.columns[column];const auto& actual=lodCopy.columns.columns[column];
        check(actual.exact==expected.exact && actual.spans.size()==expected.spans.size(),"all LOD column flags and counts survive framing");
        for(size_t span=0;span<expected.spans.size();++span)
            check(actual.spans[span].bottom==expected.spans[span].bottom && actual.spans[span].top==expected.spans[span].top &&
                  actual.spans[span].block==expected.spans[span].block,"every LOD span and material survives framing");
    }
    for (size_t size = 0; size < lodBytes.size(); ++size) { auto cut = lodBytes; cut.resize(size); rejects([&] { Lan::decodeLodUpdate(cut, true); }); }
    auto invalidLod = lod; invalidLod.key.level = 255; rejects([&] { Lan::encodeLodUpdate(invalidLod, true); });
    invalidLod = lod; invalidLod.columns.at(0,0).spans[1].top = 321; rejects([&] { Lan::encodeLodUpdate(invalidLod, true); });
    invalidLod = lod; invalidLod.columns.at(0,0).spans[1].bottom = -30; rejects([&] { Lan::encodeLodUpdate(invalidLod, true); });
    check(Lan::lodContainsChunk(lod.key, -32, -40) && !Lan::lodContainsChunk(lod.key, -24, -40), "LOD membership uses mathematical floor boundaries");
    Lan::GameEvent event; event.kind = Lan::EventKind::Fishing; event.flags = static_cast<uint8_t>(FishingEventKind::Bite); event.owner = 8; event.position = {-16,64,32};
    auto eventBytes = Lan::encodeGameEvent(event);
    const auto eventCopy=Lan::decodeGameEvent(eventBytes);
    check(eventCopy.kind==event.kind && eventCopy.dimension==event.dimension && eventCopy.epoch==event.epoch &&
          eventCopy.owner==event.owner && eventCopy.position==event.position && eventCopy.block==event.block &&
          eventCopy.amount==event.amount && eventCopy.flags==event.flags,"all event fields survive bounded codec");
    eventBytes.pop_back(); rejects([&] { Lan::decodeGameEvent(eventBytes); });
    event.flags = 7; rejects([&] { Lan::encodeGameEvent(event); });
    event.flags = 0; event.position.x = std::numeric_limits<double>::infinity(); rejects([&] { Lan::encodeGameEvent(event); });
    std::cout << "Chunk protocol tests passed\n";
}
