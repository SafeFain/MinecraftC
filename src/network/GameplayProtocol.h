#pragma once

#include "network/Protocol.h"
#include "game/InventoryCommands.h"
#include "game/GameRules.h"

namespace Lan {
enum class ActionKind : uint8_t { OpenWindow, Inventory, CloseWindow, PickBlock, Respawn, SleepChoice, Trade };
struct GameAction {
    uint64_t sequence = 0, epoch = 0, window = 0, target = 0;
    ActionKind kind = ActionKind::Inventory;
    InventoryWindowKind windowKind = InventoryWindowKind::Closed;
    glm::ivec3 position{0};
    InventoryAction inventory;
};
Bytes encodeGameAction(const GameAction& action);
GameAction decodeGameAction(const Bytes& bytes);

// Movement is intent, never a client displacement, time step or inventory state.
struct PlayerInput {
    DimensionId dimension = DimensionId::Overworld;
    float forward = 0;
    float strafe = 0;
    float yaw = 0;
    float pitch = 0;
    uint8_t selected = 0;
    uint8_t buttons = 0;
    uint8_t radius = 2;
    uint16_t lodRadius = 0;
};
namespace InputButtons {
constexpr uint8_t Jump = 1, Sneak = 2, Sprint = 4, Attack = 8, Use = 16;
constexpr uint8_t All = Jump | Sneak | Sprint | Attack | Use;
}
Bytes encodePlayerInput(const PlayerInput& input);
PlayerInput decodePlayerInput(const Bytes& bytes);
}
