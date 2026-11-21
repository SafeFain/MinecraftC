#pragma once

#include "network/ProtocolError.h"
#include "world/LodTileKey.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>

namespace Lan {
constexpr size_t MAX_LOD_SUBSCRIPTIONS = 4096;
constexpr uint64_t CANCEL_LOD_SUBSCRIPTION = std::numeric_limits<uint64_t>::max();

inline bool lodContainsChunk(const LodTileKey& key, int x, int z) {
    const int64_t side = int64_t{1} << key.level;
    return x >= int64_t{key.x} * side && x < (int64_t{key.x} + 1) * side &&
           z >= int64_t{key.z} * side && z < (int64_t{key.z} + 1) * side;
}

// Owns subscription identity independently of worker scheduling and transport.
// A cancelled/re-added key is a new consumer even if its revision starts at one.
// Old completion must neither deliver data nor release the new consumer's work.
class LodSubscriptions {
public:
    struct Work {LodTileKey key;uint64_t revision,subscription;};
    explicit LodSubscriptions(size_t limit=MAX_LOD_SUBSCRIPTIONS):m_limit(limit) {}
    void request(const LodTileKey& key) {
        if(!m_entries.count(key)&&m_entries.size()>=m_limit)
            throw ProtocolError("LOD subscription limit exceeded");
        auto inserted=m_entries.try_emplace(key);
        if(inserted.second)inserted.first->second.subscription=m_nextSubscription++;
        inserted.first->second.dirty=true;
    }
    void cancel(const LodTileKey& key) {m_entries.erase(key);}
    void clear() {m_entries.clear();}
    void invalidateChunk(int x,int z) {
        for(auto& entry:m_entries)if(lodContainsChunk(entry.first,x,z)) {
            ++entry.second.revision;entry.second.dirty=true;entry.second.notified=false;
        }
    }
    template<class InRange,class Send>
    void notifyInvalidations(InRange inRange,Send send) {
        for(auto it=m_entries.begin();it!=m_entries.end();) {
            if(!inRange(it->first)){it=m_entries.erase(it);continue;}
            auto& value=it->second;
            if(!value.notified&&value.revision>1&&send(it->first,value.revision))value.notified=true;
            ++it;
        }
    }
    template<class Distance>
    std::optional<Work> claimNext(Distance distance) {
        auto chosen=m_entries.end();double nearest=std::numeric_limits<double>::max();
        for(auto it=m_entries.begin();it!=m_entries.end();++it) {
            if(!it->second.dirty||it->second.queued)continue;
            const double value=distance(it->first);
            if(value<nearest){nearest=value;chosen=it;}
        }
        if(chosen==m_entries.end())return std::nullopt;
        chosen->second.queued=true;
        return Work{chosen->first,chosen->second.revision,chosen->second.subscription};
    }
    // Consume/decode a matching worker even if its revision became stale, so
    // errors still reach the session fault boundary. Only current data may be
    // sent; false (backpressure/stale data) leaves the subscription retryable.
    template<class Consume>
    void finish(const Work& work,Consume consume) {
        const auto found=m_entries.find(work.key);
        if(found==m_entries.end()||found->second.subscription!=work.subscription)return;
        auto& value=found->second;value.queued=false;
        const bool current=value.revision==work.revision;
        if(consume(current)&&current)value.dirty=false;
    }
private:
    struct Entry {uint64_t revision=1,subscription=0;bool dirty=true,queued=false,notified=false;};
    size_t m_limit;
    uint64_t m_nextSubscription=1;
    std::unordered_map<LodTileKey,Entry,LodTileKeyHash> m_entries;
};

// Tracks only the current replica stream. The session resets on epoch changes,
// decides which tiles it wants, and owns all transport and world updates.
class ReplicaLodSubscriptions {
public:
    void clear() { m_revisions.clear(); }

    void invalidate(const LodTileKey& key, uint64_t required) {
        const auto found = m_revisions.find(key);
        if (found == m_revisions.end()) return;
        auto& revision = found->second;
        revision.required = std::max(revision.required, required);
        revision.pending = false;
    }

    template<class Apply>
    void receive(const LodTileKey& key, uint64_t number, Apply apply) {
        const auto found = m_revisions.find(key);
        if (found == m_revisions.end()) return;
        auto& revision = found->second;
        revision.pending = false;
        if (number < revision.required || number <= revision.received) return;
        apply();
        revision.received = number;
    }

    // A failed send leaves the subscription eligible for a later poll.
    template<class Send>
    void poll(std::span<const LodTileKey> selected, Send send) {
        constexpr size_t sendBudget = 2;
        constexpr size_t pendingLimit = 8;
        const std::unordered_set<LodTileKey, LodTileKeyHash> wanted(selected.begin(), selected.end());

        size_t sent = 0;
        for (auto it = m_revisions.begin(); it != m_revisions.end();) {
            if (wanted.count(it->first) || sent >= sendBudget ||
                !send(it->first, CANCEL_LOD_SUBSCRIPTION)) {
                ++it;
                continue;
            }
            ++sent;
            it = m_revisions.erase(it);
        }

        size_t pending = 0;
        for (const auto& entry : m_revisions) if (entry.second.pending) ++pending;
        for (const auto& key : selected) {
            if (sent >= sendBudget || pending >= pendingLimit) break;
            if (m_revisions.size() >= MAX_LOD_SUBSCRIPTIONS && !m_revisions.count(key)) continue;
            auto& revision = m_revisions[key];
            if (revision.pending || revision.received >= revision.required || !send(key, 0)) continue;
            revision.pending = true;
            ++sent;
            ++pending;
        }
    }

private:
    struct Revision {
        uint64_t received = 0;
        uint64_t required = 1;
        bool pending = false;
    };

    std::unordered_map<LodTileKey, Revision, LodTileKeyHash> m_revisions;
};
} // namespace Lan
