#pragma once

#include <array>
#include <cstdint>

#include "game/InventoryModel.h"

enum class BlockEntityType : uint8_t { Chest = 0, Furnace = 1 };

struct BlockEntity {
    BlockEntityType type = BlockEntityType::Chest;
    std::array<ItemStack, 27> chest{};
    ItemStack input;
    ItemStack fuel;
    ItemStack output;
    uint16_t burnRemaining = 0;
    uint16_t burnTotal = 0;
    uint16_t cookProgress = 0;
    uint16_t cookTotal = 200;
};

struct PersistedBlockEntity {
    uint32_t localIndex = 0;
    BlockEntity value;
};
