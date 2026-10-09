#pragma once

#include "network/Session.h"
#include "game/GameRules.h"
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
    std::array<glm::dvec3,2> positions{{{.5,65,.5},{.5,120,.5}}};
    std::array<bool,2> positioned{{false,false}};
    std::optional<glm::ivec3> bedSpawn;
    float health=20;
    uint8_t hunger=20;
    float saturation=5;
    float exhaustion=0;
    uint32_t foodTickTimer=0;
    InventoryModel inventory;
};
Bytes encodeProfile(const PlayerProfile& profile);
PlayerProfile decodeProfile(const Bytes& bytes);
// Sidecars are independent of the v18 world format and generation output.
class ProfileStore {
public:
    explicit ProfileStore(std::filesystem::path directory) : m_directory(std::move(directory)) {}
    static Identity localIdentity(const std::filesystem::path& dataDirectory);
    std::optional<PlayerProfile> load(const Identity& identity) const;
    bool accepts(const Identity& identity) const;
    void save(const PlayerProfile& profile) const;
private:
    std::filesystem::path m_directory;
    std::filesystem::path path(const std::string& id) const;
};
}
