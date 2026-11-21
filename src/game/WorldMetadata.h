#pragma once

#include "entity/EntityRecord.h"
#include "game/Experience.h"
#include "game/GameRules.h"
#include "game/GameRuleRegistry.h"
#include "game/InventoryModel.h"
#include "game/StatusEffects.h"
#include "game/Weather.h"
#include "plugins/GenerationConfig.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

struct WorldMetadata {
    struct DimensionState {
        glm::dvec3 playerPosition{0.0, 120.0, 0.0};
        glm::ivec3 safePosition{0, 120, 0}; // builtin inspection prefix
        glm::dvec3 safePlayerPosition{0.5,120.01,0.5};
        bool hasSafePosition = false;
        bool positioned = false;
        WeatherSaveState weather;
        std::optional<glm::ivec3> bedSpawn;
        std::optional<glm::ivec3> anchorSpawn;
        uint64_t worldTicks = 0;
        // DayNightCycle deliberately stores a plain phase here so the save
        // layer does not depend on renderer headers.
        float dayPhase = 0.04f;
    };

    std::map<uint32_t,Plugins::GenerationConfig> generation{{0,{}}};
    std::map<uint32_t,std::pair<uint32_t,uint32_t>> timePolicies; // cycle override, advancing policy
    std::string displayName;
    std::vector<std::pair<std::string,std::string>> pluginRequirements;
    std::string pluginCompatibilityError;
    bool needsRecoverySpawn=false; // Decode-only: validate landing after terrain loads.
    std::map<uint32_t,std::string> savedDimensionKeys; // Decode-only; never serialized as runtime IDs.
    uint64_t seed = 0;
    uint32_t generationVersion = 0;
    uint32_t rulesetVersion = SURVIVAL_RULESET_VERSION;
    GameMode gameMode = GameMode::Survival;
    Difficulty difficulty = Difficulty::Normal;
    bool cheatsEnabled = false;
    uint64_t worldTicks = 0;
    uint32_t dayNightDurationSeconds = DEFAULT_DAY_NIGHT_DURATION_SECONDS;
    GameRuleSet gameRules;
    WeatherSaveState weather;
    glm::dvec3 playerPosition{0.0, 50.0, 0.0};
    glm::ivec3 worldSpawn{0, 50, 0};
    std::optional<glm::ivec3> bedSpawn;
    float health = 20.0f;
    uint8_t hunger = 20;
    float saturation = 5.0f;
    float exhaustion = 0.0f;
    uint32_t foodTickTimer = 0;
    std::vector<ActiveStatusEffect> effects;
    InventoryModel inventory;
    Experience experience;
    uint32_t enchantmentSeed = 1;
    std::array<ItemStack,12> heldItems{};
    // Durable entity partition shared with chunk persistence and transfers.
    // The record contract lives in entity/EntityRecord.h; this alias preserves
    // every existing WorldMetadata::PersistedEntity spelling and the save format.
    using PersistedEntity = EntityRecord;
    std::vector<PersistedEntity> entities;
    WorldType worldType = WorldType::Normal;
    DimensionId activeDimension = DimensionId::Overworld;
    float overworldDayPhase = 0.04f;
    DimensionState heaven;
    std::map<uint32_t,DimensionState> dimensions;
};
