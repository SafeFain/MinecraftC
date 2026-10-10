#pragma once
#include "network/Protocol.h"
#include "plugins/ContentRegistry.h"
#include <algorithm>

namespace Lan {
// Frozen registry keys define canonical wire IDs independently of native load
// order. Local IDs returned by the plugin SDK are never reassigned.
struct ContentIds {
    uint64_t revision = UINT64_MAX;
    size_t blockCount = 0, itemCount = 0;
    std::map<uint16_t,uint16_t> blockEncode, blockDecode, itemEncode, itemDecode;
    void refresh() {
        const auto& c=Plugins::content();
        if(revision==c.revision && blockCount==c.blocks.size() && itemCount==c.items.size())return;
        revision=c.revision;blockCount=c.blocks.size();itemCount=c.items.size();
        auto build=[](const auto& values,auto& encode,auto& decode) {
            encode.clear();decode.clear();std::map<std::string,uint16_t> keys;
            for(const auto& entry:values)keys.emplace(entry.second.key,entry.first);
            uint32_t next=Plugins::FIRST_CONTENT_ID;
            for(const auto& entry:keys) {
                if(next>=Plugins::INVALID_CONTENT_ID)throw ProtocolError("Network content palette is too large");
                encode.emplace(entry.second,static_cast<uint16_t>(next));decode.emplace(static_cast<uint16_t>(next++),entry.second);
            }
        };
        build(c.blocks,blockEncode,blockDecode);build(c.items,itemEncode,itemDecode);
    }
};
inline ContentIds& contentIds() {static thread_local ContentIds ids;ids.refresh();return ids;}
inline uint16_t contentId(uint16_t id,uint16_t builtinCount,const std::map<uint16_t,uint16_t>& mapping) {
    if(id<builtinCount)return id;
    const auto found=mapping.find(id);
    if(found==mapping.end())throw ProtocolError("Unknown network content ID");
    return found->second;
}
inline uint16_t encodeBlockId(uint16_t id) {return contentId(id,static_cast<uint16_t>(BlockId::COUNT),contentIds().blockEncode);}
inline uint16_t decodeBlockId(uint16_t id) {return contentId(id,static_cast<uint16_t>(BlockId::COUNT),contentIds().blockDecode);}
inline uint16_t encodeItemId(ItemId id) {return contentId(static_cast<uint16_t>(id),static_cast<uint16_t>(ItemId::COUNT),contentIds().itemEncode);}
inline ItemId decodeItemId(uint16_t id) {return static_cast<ItemId>(contentId(id,static_cast<uint16_t>(ItemId::COUNT),contentIds().itemDecode));}
}
