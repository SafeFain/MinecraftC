#pragma once

#include <cctype>
#include <stdexcept>
#include <string>

#include "plugins/ContentTypes.h"

// Read-only content queries. Runtime code may include this header instead of
// the registration APIs; the frozen registry is the single source of truth for
// built-in and plugin-provided ids.
namespace Plugins {
inline const ContentState& content() {return contentStorage();}
inline bool availableBlock(BlockId id){const auto raw=static_cast<uint16_t>(id);return raw>=FIRST_CONTENT_ID?content().blocks.count(raw)!=0:content().validBlocks[raw]!=0;}
inline bool availableItem(ItemId id){const auto raw=static_cast<uint16_t>(id);return raw>=FIRST_CONTENT_ID?content().items.count(raw)!=0:content().validItems[raw]!=0;}
inline const BlockBehaviorContent* blockBehavior(BlockId id){const auto& entries=content().extensions.blockBehaviors;const auto it=entries.find(static_cast<uint16_t>(id));return it==entries.end()?nullptr:&it->second;}
inline const ItemBehaviorContent* itemBehavior(ItemId id){const auto& entries=content().extensions.itemBehaviors;const auto it=entries.find(static_cast<uint16_t>(id));return it==entries.end()?nullptr:&it->second;}
inline bool validBlock(BlockId id) {
    return availableBlock(id);
}
inline const BlockContent* pluginBlock(BlockId id) {
    auto it=content().blocks.find(static_cast<uint16_t>(id));return it==content().blocks.end()?nullptr:&it->second;
}
inline const ItemContent* pluginItem(ItemId id) {
    auto it=content().items.find(static_cast<uint16_t>(id));return it==content().items.end()?nullptr:&it->second;
}
inline std::string blockKey(BlockId id) {
    if(const auto* b=pluginBlock(id))return b->key;
    if(!validBlock(id))throw std::runtime_error("Unknown block ID");
    return "minecraftc:block_"+std::to_string(static_cast<uint16_t>(id));
}
inline std::string itemKey(ItemId id) {
    if(const auto* item=pluginItem(id))return item->key;
    if(!isValidItemId(id))throw std::runtime_error("Unknown item ID");
    return "minecraftc:item_"+std::to_string(static_cast<uint16_t>(id));
}
inline uint16_t builtinKeyId(const std::string& key,const char* prefix,uint16_t count) {
    const std::string start(prefix);if(key.rfind(start,0)!=0)return INVALID_CONTENT_ID;
    const auto digits=key.substr(start.size());if(digits.empty())return INVALID_CONTENT_ID;
    uint32_t n=0;for(char c:digits){if(c<'0'||c>'9')return INVALID_CONTENT_ID;n=n*10+static_cast<uint32_t>(c-'0');if(n>=count)return INVALID_CONTENT_ID;}
    return static_cast<uint16_t>(n);
}
inline BlockId resolveBlock(const std::string& key) {
    for(const auto& entry:content().blocks)if(entry.second.key==key)return static_cast<BlockId>(entry.first);
    const uint16_t raw=builtinKeyId(key,"minecraftc:block_",static_cast<uint16_t>(BlockId::COUNT));
    if(raw!=INVALID_CONTENT_ID){const auto id=static_cast<BlockId>(raw);if(!availableBlock(id))throw std::runtime_error("Block provider unavailable");return id;}
    std::string name=key.rfind("minecraftc:",0)==0?key.substr(11):key;
    for(uint16_t i=0;i<static_cast<uint16_t>(BlockId::COUNT);++i) {
        std::string normalized;for(unsigned char c:BLOCK_TABLE[i].name)normalized+=c==' '?'_':static_cast<char>(std::tolower(c));
        if(name==normalized&&availableBlock(static_cast<BlockId>(i)))return static_cast<BlockId>(i);
    }
    throw std::runtime_error("Unknown block: "+key);
}
inline ItemId resolveItem(const std::string& key) {
    for(const auto& entry:content().items)if(entry.second.key==key)return static_cast<ItemId>(entry.first);
    const uint16_t raw=builtinKeyId(key,"minecraftc:item_",static_cast<uint16_t>(ItemId::COUNT));
    if(raw!=INVALID_CONTENT_ID){const auto id=static_cast<ItemId>(raw);if(!availableItem(id))throw std::runtime_error("Item provider unavailable");return id;}
    auto id=itemFromCommandName(key.rfind("minecraftc:",0)==0?key.substr(11):key);
    if(id)return *id;
    throw std::runtime_error("Unknown item: "+key);
}
inline const EntityContent* entityContent(uint16_t id) {
    auto it=content().extensions.entities.find(id);
    return it==content().extensions.entities.end()?nullptr:&it->second;
}
inline bool validEntity(uint16_t id) {return id<MC_BUILTIN_ENTITY_COUNT||entityContent(id);}
inline std::string entityKey(uint16_t id) {
    if(id<MC_BUILTIN_ENTITY_COUNT)return "minecraftc:entity_"+std::to_string(id);
    if(const auto* e=entityContent(id))return e->key;
    throw std::runtime_error("Unknown entity type");
}
inline uint16_t resolveEntity(const std::string& key) {
    const auto builtin=builtinKeyId(key,"minecraftc:entity_",MC_BUILTIN_ENTITY_COUNT);
    if(builtin!=INVALID_CONTENT_ID)return builtin;
    for(const auto& e:content().extensions.entities)if(e.second.key==key)return e.first;
    throw std::runtime_error("Unknown entity type: "+key);
}
inline bool validDimension(uint32_t id) {return content().extensions.dimensions.count(id)!=0;}
inline bool validDimension(DimensionId id) {return validDimension(static_cast<uint32_t>(id));}
inline const DimensionContent& dimensionContent(uint32_t id) {
    auto it=content().extensions.dimensions.find(id);
    if(it==content().extensions.dimensions.end())throw std::runtime_error("Unknown dimension handle");
    return it->second;
}
inline const DimensionContent& dimensionContent(DimensionId id) {return dimensionContent(static_cast<uint32_t>(id));}
inline uint32_t resolveDimension(const std::string& key) {
    for(const auto& entry:content().extensions.dimensions)if(entry.second.key==key)return entry.first;
    throw std::runtime_error("Missing dimension: "+key);
}
inline std::string dimensionKey(DimensionId id) {return dimensionContent(id).key;}
inline std::string dimensionDirectory(DimensionId id) {
    if(id==DimensionId::Overworld)return "";
    if(id==DimensionId::Heaven)return "heaven";
    if(id==DimensionId::Nether)return "nether";
    // Hex encoding is injective and cannot introduce separators or traversal.
    std::string result="d_";const char* hex="0123456789abcdef";
    for(unsigned char c:dimensionKey(id)){result+=hex[c>>4];result+=hex[c&15];}
    for(size_t i=122;i<result.size();i+=123)result.insert(i,"/");
    return result;
}
} // namespace Plugins
