#pragma once

#include "network/Protocol.h"
#include "game/InventoryCommands.h"
#include "game/GameRuleRegistry.h"
#include "game/GameRules.h"
#include "game/InventoryModel.h"
#include "game/Weather.h"
#include "game/FishingSystem.h"
#include "player/PlayerVisual.h"

namespace Lan {
struct WorldInfo {
    std::string name;
    uint64_t seed = 0;
    uint32_t generationVersion = 0;
    WorldType type = WorldType::Normal;
    Difficulty difficulty = Difficulty::Normal;
    GameRuleSet rules;
};
struct PlayerView {
    uint64_t id = 0;
    std::string nickname;
    glm::dvec3 position{0};
    float yaw = 0, pitch = 0;
    glm::vec3 sleepFacing{0};
    GameMode mode = GameMode::Survival;
    bool flying = false;
    uint8_t selected = 0;
    PlayerVisualState visual;
    ItemStack mainhand, offhand;
    FishingView fishing;
};
struct RoomPlayer { uint64_t id = 0; std::string nickname; DimensionId dimension = DimensionId::Overworld; };
struct AuthorityState {
    DimensionId dimension = DimensionId::Overworld;
    uint64_t epoch = 1, tick = 0, inputAcknowledged = 0;
    float phase = 0;
    uint32_t dayDuration = 1200;
    Difficulty difficulty = Difficulty::Normal;
    GameRuleSet rules;
    WeatherSaveState weather;
    bool dead = false, loading = true;
    uint8_t sleepState = 0;
    glm::ivec3 bed{0};
    glm::vec3 sleepFacing{0, 0, -1};
    float miningProgress = 0;
    std::optional<glm::ivec3> miningTarget;
    PlayerView self;
    std::vector<PlayerView> others;
    std::vector<RoomPlayer> roster;
    InventoryModel inventory;
    float health = 20, saturation = 5, exhaustion = 0;
    uint8_t hunger = 20;
    uint32_t foodTimer = 0;
    uint64_t actionAcknowledged = 0;
    InventoryWindowView window;
    uint64_t inventoryRevision = 0;
    ItemStack cursor;
    std::array<ItemStack, 9> crafting{};
};
Bytes encodeWorldInfo(const WorldInfo& value);
WorldInfo decodeWorldInfo(const Bytes& bytes);
Bytes encodeAuthorityState(const AuthorityState& value);
AuthorityState decodeAuthorityState(const Bytes& bytes);
}
