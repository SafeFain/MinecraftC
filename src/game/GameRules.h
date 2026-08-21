#pragma once

#include <cstdint>

enum class GameMode : uint8_t {
    Creative = 0,
    Survival = 1,
    Spectator = 2
};

enum class Difficulty : uint8_t {
    Peaceful = 0,
    Easy = 1,
    Normal = 2,
    Hard = 3
};

// Terrain preset selected when a world is created.  This is persisted with
// the world metadata so reopening a world uses the same generator path.
enum class WorldType : uint8_t {
    Normal = 0,
    Superflat = 1
};

constexpr uint32_t SURVIVAL_RULESET_VERSION = 2602;
constexpr uint32_t SAVE_FORMAT_VERSION = 9;
