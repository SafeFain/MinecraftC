#pragma once
#include "world/Block.h"
#include "game/SurvivalRules.h"
#include <map>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

namespace Plugins {
constexpr uint16_t FIRST_CONTENT_ID=4096;
constexpr uint16_t INVALID_CONTENT_ID=65535;
struct BlockContent {
    std::string key, name, dropKey;
    BlockProperties properties;
    BlockSurvivalProperties survival;
    std::array<std::string,6> materials;
    std::array<uint16_t,6> tiles{};
    uint8_t emission=0;
    ItemId drop=ItemId::EMPTY;
};
struct ItemContent {
    std::string key, material;
    ItemProperties properties;
    CreativeItemCategory category=CreativeItemCategory::Materials;
    uint16_t tile=0;
};
struct MaterialContent {
    std::string key;
    std::filesystem::path image, normal, properties;
    glm::vec4 color{1.0f};
    bool replace=false;
    uint16_t tile=0;
};
struct ContentState {
    bool frozen=false;
    uint64_t revision=0;
    std::string runtimeFault;
    std::map<uint16_t,BlockContent> blocks;
    std::map<uint16_t,ItemContent> items;
    std::map<std::string,MaterialContent> materials;
    std::map<std::string,CraftingRecipe> crafting;
    std::map<std::string,SmeltingRecipe> smelting;
    std::vector<std::string> removeCrafting, removeSmelting;
    std::vector<std::pair<std::string,std::string>> requirements;
    std::map<std::string,std::string> networkDescription;
    std::array<std::vector<ItemId>,static_cast<size_t>(CreativeItemCategory::Count)> categories;
};
inline ContentState& content() {static ContentState state;return state;}
inline bool validKey(const std::string& key) {
    auto colon=key.find(':');if(colon==std::string::npos||colon==0||colon+1==key.size()||key.size()>256)return false;
    for(size_t i=0;i<key.size();++i) {
        const char c=key[i];if(i==colon)continue;
        if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||(i>colon&&c=='/')))return false;
    }
    return true;
}
inline bool validBlock(BlockId id) {
    auto raw=static_cast<uint16_t>(id);return raw<static_cast<uint16_t>(BlockId::COUNT)||content().blocks.count(raw);
}
inline const BlockContent* pluginBlock(BlockId id) {
    if(static_cast<uint16_t>(id)<FIRST_CONTENT_ID)return nullptr;
    auto it=content().blocks.find(static_cast<uint16_t>(id));return it==content().blocks.end()?nullptr:&it->second;
}
inline const ItemContent* pluginItem(ItemId id) {
    if(static_cast<uint16_t>(id)<FIRST_CONTENT_ID)return nullptr;
    auto it=content().items.find(static_cast<uint16_t>(id));return it==content().items.end()?nullptr:&it->second;
}
inline void mutableContent(const std::string& key) {
    if(content().frozen)throw std::runtime_error("Content registry is frozen");
    if(!validKey(key))throw std::runtime_error("Invalid content key: "+key);
}
inline BlockId registerBlock(BlockContent value) {
    mutableContent(value.key);
    for(const auto& entry:content().blocks)if(entry.second.key==value.key)throw std::runtime_error("Duplicate block: "+value.key);
    const uint32_t raw=FIRST_CONTENT_ID+static_cast<uint32_t>(content().blocks.size());
    if(raw>=INVALID_CONTENT_ID)throw std::runtime_error("Block registry capacity exceeded");
    auto id=static_cast<BlockId>(raw);value.properties.id=id;
    auto it=content().blocks.emplace(static_cast<uint16_t>(raw),std::move(value)).first;
    it->second.properties.name=it->second.name.c_str();return id;
}
inline ItemId registerItem(ItemContent value) {
    mutableContent(value.key);
    for(const auto& entry:content().items)if(entry.second.key==value.key)throw std::runtime_error("Duplicate item: "+value.key);
    const uint32_t raw=FIRST_CONTENT_ID+static_cast<uint32_t>(content().items.size());
    if(raw>=INVALID_CONTENT_ID)throw std::runtime_error("Item registry capacity exceeded");
    if(value.properties.maxStack==0||value.properties.maxStack>64)throw std::runtime_error("Invalid stack size");
    const auto id=static_cast<ItemId>(raw);
    content().items.emplace(static_cast<uint16_t>(raw),std::move(value));return id;
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
    if(raw!=INVALID_CONTENT_ID)return static_cast<BlockId>(raw);
    std::string name=key.rfind("minecraftc:",0)==0?key.substr(11):key;
    for(uint16_t i=0;i<static_cast<uint16_t>(BlockId::COUNT);++i) {
        std::string normalized;for(unsigned char c:BLOCK_TABLE[i].name)normalized+=c==' '?'_':static_cast<char>(std::tolower(c));
        if(name==normalized)return static_cast<BlockId>(i);
    }
    throw std::runtime_error("Unknown block: "+key);
}
inline ItemId resolveItem(const std::string& key) {
    for(const auto& entry:content().items)if(entry.second.key==key)return static_cast<ItemId>(entry.first);
    const uint16_t raw=builtinKeyId(key,"minecraftc:item_",static_cast<uint16_t>(ItemId::COUNT));
    if(raw!=INVALID_CONTENT_ID)return static_cast<ItemId>(raw);
    auto id=itemFromCommandName(key.rfind("minecraftc:",0)==0?key.substr(11):key);
    if(id)return *id;
    throw std::runtime_error("Unknown item: "+key);
}
inline void freezeContent() {
    for(auto& entry:content().blocks)entry.second.properties.name=entry.second.name.c_str();
    for(auto& category:content().categories)category.clear();
    for(const auto& entry:content().items)content().categories.at(static_cast<size_t>(entry.second.category)).push_back(static_cast<ItemId>(entry.first));
    ++content().revision;
    content().frozen=true;
}
} // namespace Plugins
