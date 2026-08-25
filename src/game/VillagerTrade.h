#pragma once

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

#include "game/InventoryModel.h"
#include "world/Block.h"

enum class VillagerProfession : uint8_t {
    Unemployed = 0,
    Farmer,
    Fletcher,
    Shepherd,
    Leatherworker,
    Armorer,
    Toolsmith,
    Weaponsmith,
    Count
};

struct TradeOffer {
    ItemStack input;
    ItemStack output;
    uint8_t maximumUses = 12;
    uint8_t experience = 0;
};

struct VillagerData {
    VillagerProfession profession = VillagerProfession::Unemployed;
    uint8_t level = 1;
    uint16_t experience = 0;
    uint32_t offerSeed = 0;
    std::array<uint8_t, 5> uses{};
    glm::ivec3 claimedBed{0};
    glm::ivec3 claimedWorkstation{0};
    bool hasBed = false;
    bool hasWorkstation = false;
    bool professionLocked = false;
    uint32_t lastRestockDay = 0;
    uint8_t restocksToday = 0;
};

enum class TradeResult : uint8_t {
    Success,
    InvalidOffer,
    LockedOffer,
    Exhausted,
    MissingInput,
    OutputFull
};

VillagerProfession professionForWorkstation(BlockId block);
BlockId workstationForProfession(VillagerProfession profession);
const std::array<TradeOffer, 5>& villagerOffers(VillagerProfession profession);
uint8_t unlockedTradeCount(const VillagerData& villager);
TradeResult executeVillagerTrade(VillagerData& villager, uint8_t offerIndex,
                                 InventoryModel& inventory);
bool restockVillager(VillagerData& villager, uint32_t day,
                     bool reachedWorkstation);

