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
    Fisherman, Librarian, Cartographer, Cleric, Butcher, Mason,
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
    std::array<ItemStack,8> food{};
    float growthSeconds = 0.0f;
    float breedingCooldown = 0.0f;
    float defenseCooldown = 0.0f;
    int32_t reputation = 0;
    std::array<uint8_t,5> demand{};
    uint32_t reputationDay = 0;
    uint8_t tradesToday = 0;
    bool adult() const { return growthSeconds <= 0.0f; }
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


TradeOffer villagerQuote(const VillagerData& villager, uint8_t offerIndex);
const char* villagerProfessionKey(VillagerProfession profession);
int villagerFoodCount(const VillagerData& villager, ItemId item);
int addVillagerFood(VillagerData& villager, ItemStack stack);
bool consumeVillagerFood(VillagerData& villager, ItemId item, int count);
bool willingToBreed(const VillagerData& villager);
void consumeBreedingFood(VillagerData& villager);
void advanceVillagerLife(VillagerData& villager, float dt, uint32_t day);

// Bind a request to the complete displayed quote, including stock and level.
inline uint64_t villagerQuoteRevision(const VillagerData& villager) {
    uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&hash](uint64_t value) {
        for (int i = 0; i < 8; ++i) { hash ^= static_cast<uint8_t>(value); hash *= 1099511628211ULL; value >>= 8; }
    };
    mix(static_cast<uint8_t>(villager.profession)); mix(villager.level);
    mix(villager.experience); mix(villager.offerSeed); mix(static_cast<uint32_t>(villager.reputation));
    mix(villager.adult());
    for (auto value : villager.uses) mix(value);
    for (auto value : villager.demand) mix(value);
    return hash;
}
