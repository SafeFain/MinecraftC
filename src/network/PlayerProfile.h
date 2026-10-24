#pragma once

#include "network/Session.h"
#include "plugins/ContentRegistry.h"
#include "game/GameRules.h"
#include "game/StatusEffects.h"
#include "game/InventoryModel.h"
#include <array>
#include <filesystem>
#include <optional>
#include <utility>
#include <glm/glm.hpp>

namespace Lan {
struct PlayerProfile {
    Identity identity;
    GameMode mode=GameMode::Survival;
    DimensionId dimension=DimensionId::Overworld;
    std::map<uint32_t,glm::dvec3> positions=[] {std::map<uint32_t,glm::dvec3> result;for(const auto& d:Plugins::content().extensions.dimensions)result[d.first]={.5,d.first==1?120.:65.,.5};return result;}();
    std::map<uint32_t,bool> positioned;
    std::map<uint32_t,glm::dvec3> safePositions;
    std::map<uint32_t,glm::ivec3> beds;
    std::optional<glm::ivec3> bedSpawn;
    std::optional<glm::ivec3> netherAnchor;
    float health=20;
    uint8_t hunger=20;
    float saturation=5;
    float exhaustion=0;
    uint32_t foodTickTimer=0;
    std::vector<ActiveStatusEffect> effects;
    InventoryModel inventory;
    ItemStack cursor;
    std::array<ItemStack, 9> crafting{};
};
Bytes encodeProfile(const PlayerProfile& profile);
PlayerProfile decodeProfile(const Bytes& bytes);
// Sidecars are independent of the v18 world format and generation output.
class ProfileStore {
public:
    explicit ProfileStore(std::filesystem::path directory) : m_directory(std::move(directory)) {}
    static Identity localIdentity(const std::filesystem::path& dataDirectory);
    static bool setLocalNickname(const std::filesystem::path& dataDirectory, const std::string& nickname);
    std::optional<PlayerProfile> load(const Identity& identity) const;
    bool accepts(const Identity& identity) const;
    void save(const PlayerProfile& profile) const;
private:
    std::filesystem::path m_directory;
    std::filesystem::path path(const std::string& id) const;
};
}
