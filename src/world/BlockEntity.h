#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "game/InventoryModel.h"

enum class BlockEntityType : uint8_t {
    Chest = 0, Furnace = 1, Button = 2, Resource = 3,
    BrewingStand = 4, SmithingTable = 5, Spawner = 6
};

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
    uint16_t buttonRemaining = 0;
    std::string lootKey, lootRevision, structureKey, structureRevision;
    uint64_t lootSeed=0;
    bool lootInitialized=false;
    uint32_t resourceRemaining=0;
    uint16_t resourceBlock=0;
};

struct PersistedBlockEntity {
    uint32_t localIndex = 0;
    BlockEntity value;
};
