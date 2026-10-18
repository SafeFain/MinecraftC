#pragma once
#include "world/Block.h"
#include "game/GameRules.h"
#include "plugins/Extensions.h"
#include "plugins/BuiltinOwnership.h"
#include "plugins/Json.h"
#include <set>
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
    bool tilesReady=false;
    uint16_t family=0;
    uint32_t familyKind=0, stateIndex=0, releaseTicks=30;
    bool manualOpen=true, villagerOpen=true, arrowTrigger=true;
    std::string fullBlock;
    std::optional<BlockCollisionBoxes> collision, selection, model;
    std::string key, name, dropKey;
    BlockProperties properties;
    BlockSurvivalProperties survival;
    std::array<std::string,6> materials;
    std::array<uint16_t,6> tiles{};
    uint8_t emission=0;
    ItemId drop=ItemId::EMPTY;
};
struct ItemContent {
    std::filesystem::path icon;
    std::string key, material,command;
    ItemProperties properties;
    CreativeItemCategory category=CreativeItemCategory::Materials;
    uint16_t tile=0;
    uint16_t spawnEntity=MC_INVALID_ID;
};
struct MaterialContent {
    uint32_t variants=1,frames=1,fpsMilli=0,seed=0;
    std::vector<std::filesystem::path> images,normals,propertyFrames;
    std::string key;
    std::filesystem::path image, normal, properties;
    glm::vec4 color{1.0f};
    bool replace=false;
    uint16_t tile=0;
};
struct ContentState {
    std::array<uint8_t,65536> validBlocks=[] {
        std::array<uint8_t,65536> mask{};
        for(uint16_t i=0;i<static_cast<uint16_t>(BlockId::COUNT);++i)mask[i]=!heavenBlockSlot(static_cast<BlockId>(i));
        return mask;
    }();
    std::array<uint8_t,65536> validItems=[] {
        std::array<uint8_t,65536> mask{};
        for(uint16_t i=0;i<static_cast<uint16_t>(ItemId::COUNT);++i)mask[i]=!heavenItemSlot(static_cast<ItemId>(i));
        return mask;
    }();
    ExtensionContent extensions;
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
inline bool availableBlock(BlockId id){const auto raw=static_cast<uint16_t>(id);return raw>=FIRST_CONTENT_ID?content().blocks.count(raw)!=0:content().validBlocks[raw]!=0;}
inline bool availableItem(ItemId id){const auto raw=static_cast<uint16_t>(id);return raw>=FIRST_CONTENT_ID?content().items.count(raw)!=0:content().validItems[raw]!=0;}
inline bool validKey(const std::string& key) {
    auto colon=key.find(':');if(colon==std::string::npos||colon==0||colon+1==key.size()||key.size()>256)return false;
    for(size_t i=0;i<key.size();++i) {
        const char c=key[i];if(i==colon)continue;
        if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||(i>colon&&c=='/')))return false;
    }
    return true;
}
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
inline void mutableContent(const std::string& key) {
    if(content().frozen)throw std::runtime_error("Content registry is frozen");
    if(!validKey(key))throw std::runtime_error("Invalid content key: "+key);
}
inline BlockId registerBlock(BlockContent value) {
    mutableContent(value.key);
    for(const auto& entry:content().blocks)if(entry.second.key==value.key)throw std::runtime_error("Duplicate block: "+value.key);
    uint32_t raw=FIRST_CONTENT_ID;
    const std::string prefix="minecraftc:block_";
    if(value.key.rfind(prefix,0)==0) {
        raw=static_cast<uint32_t>(std::stoul(value.key.substr(prefix.size())));
        if(raw>=static_cast<uint16_t>(BlockId::COUNT)||!heavenBlockSlot(static_cast<BlockId>(raw)))throw std::runtime_error("Unowned reserved block slot");
    } else while(content().blocks.count(static_cast<uint16_t>(raw)))++raw;
    if(content().blocks.count(static_cast<uint16_t>(raw)))throw std::runtime_error("Duplicate reserved block slot");
    if(raw>=INVALID_CONTENT_ID)throw std::runtime_error("Block registry capacity exceeded");
    auto id=static_cast<BlockId>(raw);value.properties.id=id;
    auto it=content().blocks.emplace(static_cast<uint16_t>(raw),std::move(value)).first;
    content().validBlocks[raw]=1;
    it->second.properties.name=it->second.name.c_str();return id;
}
inline ItemId registerItem(ItemContent value) {
    mutableContent(value.key);
    for(const auto& entry:content().items)if(entry.second.key==value.key)throw std::runtime_error("Duplicate item: "+value.key);
    uint32_t raw=FIRST_CONTENT_ID;
    const std::string prefix="minecraftc:item_";
    if(value.key.rfind(prefix,0)==0) {
        raw=static_cast<uint32_t>(std::stoul(value.key.substr(prefix.size())));
        if(raw>=static_cast<uint16_t>(ItemId::COUNT)||!heavenItemSlot(static_cast<ItemId>(raw)))throw std::runtime_error("Unowned reserved item slot");
    } else while(content().items.count(static_cast<uint16_t>(raw)))++raw;
    if(content().items.count(static_cast<uint16_t>(raw)))throw std::runtime_error("Duplicate reserved item slot");
    if(raw>=INVALID_CONTENT_ID)throw std::runtime_error("Item registry capacity exceeded");
    if(value.properties.maxStack==0||value.properties.maxStack>64)throw std::runtime_error("Invalid stack size");
    const auto id=static_cast<ItemId>(raw);
    content().items.emplace(static_cast<uint16_t>(raw),std::move(value));content().validItems[raw]=1;return id;
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
    // Hex encoding is injective and cannot introduce separators or traversal.
    std::string result="d_";const char* hex="0123456789abcdef";
    for(unsigned char c:dimensionKey(id)){result+=hex[c>>4];result+=hex[c&15];}
    for(size_t i=122;i<result.size();i+=123)result.insert(i,"/");
    return result;
}
inline void validateGeneration(const GenerationConfig& config,uint32_t dimension) {
    if(!validDimension(dimension)||config.parameters.size()>4096||config.modifiers.size()>64)
        throw std::runtime_error("Invalid generator configuration limits");
    if(dimension!=0&&config.generator.empty())throw std::runtime_error("Plugin dimension requires a generator");
    if(Json::parse(config.parameters).type!=Json::Type::Object)
        throw std::runtime_error("Generator parameters must be a JSON object");
    if(config.generator.empty()&&!config.revision.empty())throw std::runtime_error("Builtin generator has no plugin revision");
    std::set<std::string> selected;
    for(const auto& m:config.modifiers)if(!selected.insert(m.first).second)throw std::runtime_error("Duplicate generator modifier");
    if(!config.generator.empty()) {
        auto g=content().extensions.generators.find(config.generator);
        if(g==content().extensions.generators.end()||g->second.dimension!=dimension||g->second.revision!=config.revision)
            throw std::runtime_error("Missing/incompatible generator: "+config.generator);
    }
    for(const auto& m:config.modifiers) {
        auto it=content().extensions.modifiers.find(m.first);
        if(it==content().extensions.modifiers.end()||it->second.dimension!=dimension||it->second.revision!=m.second)
            throw std::runtime_error("Missing/incompatible modifier: "+m.first);
    }
}
inline void freezeContent() {
    for(auto& entry:content().blocks)entry.second.properties.name=entry.second.name.c_str();
    for(auto& category:content().categories)category.clear();
    for(const auto& entry:content().items)if(entry.first>=FIRST_CONTENT_ID)content().categories.at(static_cast<size_t>(entry.second.category)).push_back(static_cast<ItemId>(entry.first));
    ++content().revision;
    content().frozen=true;
}
} // namespace Plugins
