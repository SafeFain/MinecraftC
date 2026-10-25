#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "game/GameRules.h"
#include "game/Item.h"
#include "game/SurvivalRules.h"
#include "plugins/BuiltinOwnership.h"
#include "plugins/Extensions.h"
#include "world/Block.h"

namespace Plugins {
constexpr uint16_t FIRST_CONTENT_ID=4096;
constexpr uint16_t INVALID_CONTENT_ID=65535;
inline bool validKey(const std::string& key) {
    auto colon=key.find(':');if(colon==std::string::npos||colon==0||colon+1==key.size()||key.size()>256)return false;
    for(size_t i=0;i<key.size();++i) {
        const char c=key[i];if(i==colon)continue;
        if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||(i>colon&&c=='/')))return false;
    }
    return true;
}
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
// One process-wide immutable-after-freeze registry. Read access goes through
// ContentQueries.h; registration and freezing go through ContentMutation.h.
inline ContentState& contentStorage() {static ContentState state;return state;}
} // namespace Plugins
