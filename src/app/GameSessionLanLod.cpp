#include "app/GameSession.h"
#include "debug/Log.h"
#include "world/WorldGenerator.h"
#include <algorithm>
#include <limits>
#include <map>
#include <set>

namespace {
int floorDivide(int value, int divisor) {
    const int quotient = value / divisor;
    return quotient - (value % divisor < 0 ? 1 : 0);
}
bool inLodRange(const LanPlayerRuntime& guest, const LodTileKey& key) {
    if (!guest.lodRadius) return false;
    const int64_t side = int64_t{1} << key.level;
    const int64_t x = World::worldToChunkX(guest.player.getPosition().x), z = World::worldToChunkZ(guest.player.getPosition().z);
    const int64_t dx = std::max({int64_t{key.x} * side - x, x - ((int64_t{key.x} + 1) * side - 1), int64_t{0}});
    const int64_t dz = std::max({int64_t{key.z} * side - z, z - ((int64_t{key.z} + 1) * side - 1), int64_t{0}});
    const int64_t radius = guest.lodRadius + 3 + side; // Include selected prefetch/transition footprints.
    return dx <= radius && dz <= radius && dx * dx + dz * dz <= radius * radius;
}
using ChunkKey = std::pair<int, int>;
struct ColumnSource {
    std::set<uint32_t> columns;
    std::optional<std::vector<BlockOverride>> edits;
};
std::map<ChunkKey, ColumnSource> captureSources(const World& world, const LodTileKey& key) {
    std::map<ChunkKey, ColumnSource> sources;
    const int cell = 1 << key.level;
    for (int z = 0; z < 16; ++z) for (int x = 0; x < 16; ++x) {
        const int wx = key.x * 16 * cell + x * cell + cell / 2, wz = key.z * 16 * cell + z * cell + cell / 2;
        const int cx = floorDivide(wx, 16), cz = floorDivide(wz, 16);
        sources[{cx, cz}].columns.insert(static_cast<uint32_t>(wx - cx * 16 + (wz - cz * 16) * 16));
    }
    for (auto& entry : sources) {
        entry.second.edits = world.copyKnownOverrides(entry.first.first, entry.first.second);
        if (entry.second.edits) {
            auto& edits = *entry.second.edits;
            edits.erase(std::remove_if(edits.begin(), edits.end(), [&](const BlockOverride& edit) {
                return !entry.second.columns.count(edit.localIndex % 256);
            }), edits.end());
        }
    }
    return sources;
}
Lan::Bytes buildColumns(Lan::LodUpdate update, std::map<ChunkKey, ColumnSource> sources,
                       std::filesystem::path storeRoot, uint64_t seed, WorldType type) {
    SaveStore store(std::move(storeRoot)); WorldGenerator generator(seed, type, update.dimension);
    const int cell = 1 << update.key.level;
    for (auto& entry : sources) {
        auto& source = entry.second;
        if (!source.edits) source.edits = store.loadChunkOverrides(entry.first.first, entry.first.second);
        auto& edits = *source.edits;
        edits.erase(std::remove_if(edits.begin(), edits.end(), [&](const BlockOverride& edit) {
            return !source.columns.count(edit.localIndex % 256);
        }), edits.end());
        if (edits.empty()) continue;
        // A worker owns this generated chunk. It never allocates authoritative
        // near terrain or mutates a World, entity, persistence map or GPU.
        Chunk chunk(entry.first.first, entry.first.second);
        const auto cached = store.loadGeneratedChunk(entry.first.first, entry.first.second, generator.generationVersion());
        if (cached) chunk.loadRawBlocks(*cached); else generator.generate(chunk);
        std::set<uint32_t> changed;
        for (const auto& edit : edits) {
            const auto column = edit.localIndex % 256; changed.insert(column);
            chunk.setBlock(column % 16, Config::WORLD_MIN_Y + static_cast<int>(edit.localIndex / 256), column / 16, edit.block);
        }
        std::vector<uint16_t> blocks; chunk.copyRawBlocks(blocks);
        const auto exact = extractExactLodChunk(blocks);
        for (int z = 0; z < 16; ++z) for (int x = 0; x < 16; ++x) {
            const int wx = update.key.x * 16 * cell + x * cell + cell / 2, wz = update.key.z * 16 * cell + z * cell + cell / 2;
            const int lx = wx - entry.first.first * 16, lz = wz - entry.first.second * 16;
            if (lx >= 0 && lx < 16 && lz >= 0 && lz < 16 && changed.count(static_cast<uint32_t>(lx + lz * 16)))
                update.columns.at(x, z) = exact.at(lx, lz);
        }
    }
    return Lan::encodeLodUpdate(update, true);
}
}

void GameSession::requestLanLod(LanPlayerRuntime& guest, const Lan::LodUpdate& request) {
    if (request.dimension != guest.profile.dimension || request.epoch != guest.chunkEpoch) return;
    if (request.revision == std::numeric_limits<uint64_t>::max()) { guest.lodSubscriptions.erase(request.key); return; }
    if (!inLodRange(guest, request.key)) return;
    if (!guest.lodSubscriptions.count(request.key) && guest.lodSubscriptions.size() >= Lan::MAX_LOD_SUBSCRIPTIONS)
        throw Lan::ProtocolError("LOD subscription limit exceeded");
    auto inserted = guest.lodSubscriptions.try_emplace(request.key);
    auto& subscription = inserted.first->second;
    if (inserted.second) subscription.generation = guest.nextLodSubscription++;
    subscription.dirty = true;
}
void GameSession::invalidateLanLod(DimensionId target, int x, int z) {
    for (auto& guest : guests) if (guest.second->profile.dimension == target)
        for (auto& entry : guest.second->lodSubscriptions) if (Lan::lodContainsChunk(entry.first, x, z)) {
            ++entry.second.revision; entry.second.dirty = true; entry.second.notified = false;
        }
}
void GameSession::sendLanLod() {
    for (auto it = lodJobs.begin(); it != lodJobs.end();) {
        if (it->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { ++it; continue; }
        const auto peer = guests.find(it->peer);
        if (peer != guests.end() && peer->second->chunkEpoch == it->epoch && peer->second->profile.dimension == it->dimension) {
            auto& subscriptions = peer->second->lodSubscriptions;
            const auto subscription = subscriptions.find(it->key);
            if (subscription != subscriptions.end() && subscription->second.generation == it->subscription) {
                subscription->second.queued = false;
                try {
                    auto bytes = it->result.get();
                    if (subscription->second.revision == it->revision &&
                        lanHost.send(it->peer, {Lan::MessageType::LodColumns, 0, std::move(bytes)}, true)) subscription->second.dirty = false;
                } catch (const std::exception& error) {
                    LOG_WARN("LAN LOD read failed: " << error.what()); lanHost.disconnect(it->peer);
                }
            }
        }
        it = lodJobs.erase(it);
    }
    size_t notices = 0;
    for (auto& guest : guests) {
        for (auto it = guest.second->lodSubscriptions.begin(); it != guest.second->lodSubscriptions.end();) {
            if (!inLodRange(*guest.second, it->first)) { it = guest.second->lodSubscriptions.erase(it); continue; }
            auto& value = it->second;
            if (!value.notified && value.revision > 1 && notices < 8) {
                Lan::LodUpdate update; update.dimension = guest.second->profile.dimension; update.epoch = guest.second->chunkEpoch; update.key = it->first; update.revision = value.revision;
                if (lanHost.send(guest.first, {Lan::MessageType::LodInvalidate, 0, Lan::encodeLodUpdate(update, false)})) { value.notified = true; ++notices; }
            }
            ++it;
        }
    }
    // Rotate participants while keeping only two global jobs in flight.
    if (guests.empty()) return;
    auto peer = guests.upper_bound(lodLastPeer);
    for (size_t count = 0; count < guests.size() && lodJobs.size() < 2; ++count) {
        if (peer == guests.end()) peer = guests.begin();
        auto& guest = *peer->second; lodLastPeer = peer->first;
        auto chosen = guest.lodSubscriptions.end(); double nearest = std::numeric_limits<double>::max();
        for (auto it = guest.lodSubscriptions.begin(); it != guest.lodSubscriptions.end(); ++it) {
            if (!it->second.dirty || it->second.queued) continue;
            const double side = 16 << it->first.level;
            const double distance = std::hypot((it->first.x + .5) * side - guest.player.getPosition().x, (it->first.z + .5) * side - guest.player.getPosition().z);
            if (distance < nearest) { nearest = distance; chosen = it; }
        }
        if (chosen != guest.lodSubscriptions.end()) {
            auto& runtime = *simulations[static_cast<size_t>(guest.profile.dimension)];
            const auto* store = guest.profile.dimension == DimensionId::Overworld ? saveStore.get() : runtime.store.get();
            if (store) {
                Lan::LodUpdate update; update.dimension = guest.profile.dimension; update.epoch = guest.chunkEpoch; update.key = chosen->first; update.revision = chosen->second.revision;
                auto sources = captureSources(runtime.world, update.key);
                const auto root = store->worldDirectory(); const auto seed = worldMetadata.seed; const auto type = worldMetadata.worldType;
                auto future = threadPool.enqueue([update, sources = std::move(sources), root, seed, type]() mutable { return buildColumns(update, std::move(sources), root, seed, type); });
                lodJobs.push_back({peer->first, update.epoch, update.revision, chosen->second.generation, update.dimension, update.key, std::move(future)});
                chosen->second.queued = true;
            }
        }
        ++peer;
    }
}
void GameSession::pollReplicaLod() {
    const auto selected = world().selectedLodTiles();
    const std::unordered_set<LodTileKey, LodTileKeyHash> wanted(selected.begin(), selected.end());
    size_t sent = 0;
    for (auto it = replicaLodRevisions.begin(); it != replicaLodRevisions.end();) {
        if (wanted.count(it->first)) { ++it; continue; }
        Lan::LodUpdate cancel; cancel.dimension = dimension; cancel.epoch = replicaEpoch; cancel.key = it->first; cancel.revision = std::numeric_limits<uint64_t>::max();
        if (sent >= 2 || !lanClient.send({Lan::MessageType::LodRequest, 0, Lan::encodeLodUpdate(cancel, false)})) { ++it; continue; }
        ++sent; it = replicaLodRevisions.erase(it);
    }
    size_t pending = 0;
    for (const auto& entry : replicaLodRevisions) if (entry.second.pending) ++pending;
    for (const auto& key : selected) {
        if (sent >= 2 || pending >= 8) break;
        if (!replicaLodRevisions.count(key) && replicaLodRevisions.size() >= Lan::MAX_LOD_SUBSCRIPTIONS) continue;
        auto& revision = replicaLodRevisions[key];
        if (revision.pending || revision.received >= revision.required) continue;
        Lan::LodUpdate request; request.dimension = dimension; request.epoch = replicaEpoch; request.key = key;
        if (lanClient.send({Lan::MessageType::LodRequest, 0, Lan::encodeLodUpdate(request, false)})) { ++sent; ++pending; revision.pending = true; }
    }
}
