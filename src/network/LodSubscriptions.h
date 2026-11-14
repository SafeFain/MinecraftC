#pragma once

#include "network/LodProtocol.h"
#include <limits>
#include <optional>
#include <unordered_map>

namespace Lan {
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
}
