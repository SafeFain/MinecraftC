#pragma once
#include <map>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "game/InventoryModel.h"

enum class BlockEntityType : uint8_t {
    Chest = 0, Furnace = 1, Button = 2, Resource = 3,
    BrewingStand = 4, SmithingTable = 5, Spawner = 6, EnchantingTable = 7, Anvil = 8, Grindstone = 9, Redstone = 10
};

struct BlockEntity {
    static constexpr std::size_t MAX_FURNACE_EXPERIENCE_RECIPES = 1024;

    BlockEntityType type = BlockEntityType::Chest;
    std::array<ItemStack, 27> chest{};
    ItemStack input;
    ItemStack fuel;
    ItemStack output;
    uint16_t burnRemaining = 0;
    uint16_t burnTotal = 0;
    uint16_t cookProgress = 0;
    uint16_t cookTotal = 200;
    std::map<std::string,uint64_t> furnaceExperience;
    // Only session-owned workstation windows use this preview state.
    uint32_t workCost = 0;
    bool workTooExpensive = false;
    uint8_t workFailure = 0;
    std::array<uint8_t,3> offerFailure{};
    std::array<uint8_t, 3> offerRequired{}, offerClue{}, offerClueLevel{};
    std::optional<std::string> rename;
    uint16_t buttonRemaining = 0;
    std::vector<uint8_t> circuitState;
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
