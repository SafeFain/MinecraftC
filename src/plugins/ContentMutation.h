#pragma once

#include <stdexcept>
#include <string>
#include <utility>

#include "plugins/ContentTypes.h"

// Registration and freeze APIs. Only startup and plugin loading may include
// this header; runtime/gameplay code uses the read-only ContentQueries.h view.
namespace Plugins {
inline ContentState& mutableContentState() {return contentStorage();}
inline void mutableContent(const std::string& key) {
    if(mutableContentState().frozen)throw std::runtime_error("Content registry is frozen");
    if(!validKey(key))throw std::runtime_error("Invalid content key: "+key);
}
inline BlockId registerBlock(BlockContent value) {
    mutableContent(value.key);
    for(const auto& entry:mutableContentState().blocks)if(entry.second.key==value.key)throw std::runtime_error("Duplicate block: "+value.key);
    uint32_t raw=FIRST_CONTENT_ID;
    const std::string prefix="minecraftc:block_";
    if(value.key.rfind(prefix,0)==0) {
        raw=static_cast<uint32_t>(std::stoul(value.key.substr(prefix.size())));
        if(raw>=static_cast<uint16_t>(BlockId::COUNT)||!heavenBlockSlot(static_cast<BlockId>(raw)))throw std::runtime_error("Unowned reserved block slot");
    } else while(mutableContentState().blocks.count(static_cast<uint16_t>(raw)))++raw;
    if(mutableContentState().blocks.count(static_cast<uint16_t>(raw)))throw std::runtime_error("Duplicate reserved block slot");
    if(raw>=INVALID_CONTENT_ID)throw std::runtime_error("Block registry capacity exceeded");
    auto id=static_cast<BlockId>(raw);value.properties.id=id;
    auto it=mutableContentState().blocks.emplace(static_cast<uint16_t>(raw),std::move(value)).first;
    mutableContentState().validBlocks[raw]=1;
    it->second.properties.name=it->second.name.c_str();return id;
}
inline ItemId registerItem(ItemContent value) {
    mutableContent(value.key);
    for(const auto& entry:mutableContentState().items)if(entry.second.key==value.key)throw std::runtime_error("Duplicate item: "+value.key);
    uint32_t raw=FIRST_CONTENT_ID;
    const std::string prefix="minecraftc:item_";
    if(value.key.rfind(prefix,0)==0) {
        raw=static_cast<uint32_t>(std::stoul(value.key.substr(prefix.size())));
        if(raw>=static_cast<uint16_t>(ItemId::COUNT)||!heavenItemSlot(static_cast<ItemId>(raw)))throw std::runtime_error("Unowned reserved item slot");
    } else while(mutableContentState().items.count(static_cast<uint16_t>(raw)))++raw;
    if(mutableContentState().items.count(static_cast<uint16_t>(raw)))throw std::runtime_error("Duplicate reserved item slot");
    if(raw>=INVALID_CONTENT_ID)throw std::runtime_error("Item registry capacity exceeded");
    if(value.properties.maxStack==0||value.properties.maxStack>64)throw std::runtime_error("Invalid stack size");
    const auto id=static_cast<ItemId>(raw);
    mutableContentState().items.emplace(static_cast<uint16_t>(raw),std::move(value));mutableContentState().validItems[raw]=1;return id;
}
inline void freezeContent() {
    for(auto& entry:mutableContentState().blocks)entry.second.properties.name=entry.second.name.c_str();
    for(auto& category:mutableContentState().categories)category.clear();
    for(const auto& entry:mutableContentState().items)if(entry.first>=FIRST_CONTENT_ID)mutableContentState().categories.at(static_cast<size_t>(entry.second.category)).push_back(static_cast<ItemId>(entry.first));
    ++mutableContentState().revision;
    mutableContentState().frozen=true;
}
} // namespace Plugins
