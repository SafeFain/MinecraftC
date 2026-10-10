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
    check(roundtrip.blocks == snapshot.blocks && roundtrip.address.x == -1234 && roundtrip.epoch == 9,
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
    check(decoded.edits.size() == 2 && decoded.base == 77 && decoded.revision == 79, "Delta roundtrip differs");
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
    Lan::LodUpdate lod; lod.key = {-4, -5, 3}; lod.revision = 2;
    lod.columns.at(0,0).exact = true; lod.columns.at(0,0).spans = {{-64, -20, BlockId::STONE}, {100, 101, BlockId::GLASS}};
    const auto lodBytes = Lan::encodeLodUpdate(lod, true);
    const auto lodCopy = Lan::decodeLodUpdate(lodBytes, true);
    check(lodCopy.key == lod.key && lodCopy.columns.at(0,0).spans[0].bottom == -64, "LOD negative coordinates and intervals survive framing");
    for (size_t size = 0; size < lodBytes.size(); ++size) { auto cut = lodBytes; cut.resize(size); rejects([&] { Lan::decodeLodUpdate(cut, true); }); }
    auto invalidLod = lod; invalidLod.key.level = 255; rejects([&] { Lan::encodeLodUpdate(invalidLod, true); });
    invalidLod = lod; invalidLod.columns.at(0,0).spans[1].top = 321; rejects([&] { Lan::encodeLodUpdate(invalidLod, true); });
    invalidLod = lod; invalidLod.columns.at(0,0).spans[1].bottom = -30; rejects([&] { Lan::encodeLodUpdate(invalidLod, true); });
    check(Lan::lodContainsChunk(lod.key, -32, -40) && !Lan::lodContainsChunk(lod.key, -24, -40), "LOD membership uses mathematical floor boundaries");
    Lan::GameEvent event; event.kind = Lan::EventKind::Fishing; event.flags = static_cast<uint8_t>(FishingEventKind::Bite); event.owner = 8; event.position = {-16,64,32};
    auto eventBytes = Lan::encodeGameEvent(event);
    check(Lan::decodeGameEvent(eventBytes).owner == 8, "Event owner survives its bounded codec");
    eventBytes.pop_back(); rejects([&] { Lan::decodeGameEvent(eventBytes); });
    event.flags = 7; rejects([&] { Lan::encodeGameEvent(event); });
    event.flags = 0; event.position.x = std::numeric_limits<double>::infinity(); rejects([&] { Lan::encodeGameEvent(event); });
    std::cout << "Chunk protocol tests passed\n";
}
