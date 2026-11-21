#include "app/GameSession.h"
#include "network/LodProtocol.h"
#include "game/SaveStore.h"
#include "debug/Log.h"
#include "world/WorldGenerator.h"
#include <algorithm>
#include <cmath>
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
                       std::filesystem::path storeRoot, uint64_t seed, WorldType type,Plugins::GenerationConfig config) {
    SaveStore store(std::move(storeRoot)); WorldGenerator generator(seed, type, update.dimension,std::move(config));
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
    if (request.revision == Lan::CANCEL_LOD_SUBSCRIPTION) { guest.lodSubscriptions.cancel(request.key); return; }
    if (inLodRange(guest, request.key)) guest.lodSubscriptions.request(request.key);
}
void GameSession::invalidateLanLod(DimensionId target, int x, int z) {
    for (auto& guest : guests) if (guest.second->profile.dimension == target)
        guest.second->lodSubscriptions.invalidateChunk(x,z);
}
void GameSession::sendLanLod() {
    for (auto it = lodJobs.begin(); it != lodJobs.end();) {
        if (it->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { ++it; continue; }
        const auto peer = guests.find(it->peer);
        if (peer != guests.end() && peer->second->chunkEpoch == it->epoch && peer->second->profile.dimension == it->dimension) {
            try {
                peer->second->lodSubscriptions.finish({it->key,it->revision,it->subscription},[&](bool current) {
                    auto bytes=it->result.get();
                    return current&&lanHost.send(it->peer,{Lan::MessageType::LodColumns,0,std::move(bytes)},true);
                });
            } catch (const std::exception& error) {
                LOG_WARN("LAN LOD read failed: " << error.what()); lanHost.disconnect(it->peer);
            }
        }
        it = lodJobs.erase(it);
    }
    size_t notices = 0;
    for (auto& guest : guests) {
        guest.second->lodSubscriptions.notifyInvalidations(
            [&](const LodTileKey& key){return inLodRange(*guest.second,key);},
            [&](const LodTileKey& key,uint64_t revision) {
                if(notices>=8)return false;
                Lan::LodUpdate update;update.dimension=guest.second->profile.dimension;
                update.epoch=guest.second->chunkEpoch;update.key=key;update.revision=revision;
                if(!lanHost.send(guest.first,{Lan::MessageType::LodInvalidate,0,Lan::encodeLodUpdate(update,false)}))return false;
                ++notices;return true;
            });
    }
    // Rotate participants while keeping only two global jobs in flight.
    if (guests.empty()) return;
    auto peer = guests.upper_bound(lodLastPeer);
    for (size_t count = 0; count < guests.size() && lodJobs.size() < 2; ++count) {
        if (peer == guests.end()) peer = guests.begin();
        auto& guest = *peer->second; lodLastPeer = peer->first;
        auto& runtime = *simulations[static_cast<size_t>(guest.profile.dimension)];
        const auto* store = guest.profile.dimension == DimensionId::Overworld ? saveStore.get() : runtime.store.get();
        if(store) {
            const auto work=guest.lodSubscriptions.claimNext([&](const LodTileKey& key) {
                const double side=16<<key.level;
                return std::hypot((key.x+.5)*side-guest.player.getPosition().x,
                                  (key.z+.5)*side-guest.player.getPosition().z);
            });
            if(work) {
                Lan::LodUpdate update;update.dimension=guest.profile.dimension;update.epoch=guest.chunkEpoch;
                update.key=work->key;update.revision=work->revision;
                auto sources = captureSources(runtime.world, update.key);
                const auto root = store->worldDirectory(); const auto seed = worldMetadata.seed; const auto type = worldMetadata.worldType; const auto config=worldMetadata.generation[static_cast<size_t>(update.dimension)];
                auto future = threadPool.enqueue([update, sources = std::move(sources), root, seed, type,config]() mutable { return buildColumns(update, std::move(sources), root, seed, type,config); });
                lodJobs.push_back({peer->first,update.epoch,update.revision,work->subscription,update.dimension,update.key,std::move(future)});
            }
        }
        ++peer;
    }
}
void GameSession::pollReplicaLod() {
    replicaLodSubscriptions.poll(world().selectedLodTiles(), [&](const LodTileKey& key, uint64_t revision) {
        Lan::LodUpdate request;
        request.dimension = dimension;
        request.epoch = replicaEpoch;
        request.key = key;
        request.revision = revision;
        return lanClient.send({Lan::MessageType::LodRequest, 0, Lan::encodeLodUpdate(request, false)});
    });
}
