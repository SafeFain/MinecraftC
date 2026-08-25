#include "game/VillagerTrade.h"

#include <algorithm>

namespace {

constexpr TradeOffer offer(ItemId input, uint8_t inputCount, ItemId output,
                           uint8_t outputCount, uint8_t experience) {
    return {{input, inputCount, 0}, {output, outputCount, 0}, 12, experience};
}

constexpr std::array<TradeOffer, 5> FARMER{{
    offer(ItemId::WHEAT, 20, ItemId::EMERALD, 1, 2),
    offer(ItemId::EMERALD, 1, ItemId::BREAD, 6, 5),
    offer(ItemId::WHEAT_SEEDS, 16, ItemId::EMERALD, 1, 10),
    offer(ItemId::EMERALD, 3, ItemId::STEAK, 4, 15),
    offer(ItemId::EMERALD, 3, ItemId::COOKED_CHICKEN, 6, 20),
}};
constexpr std::array<TradeOffer, 5> FLETCHER{{
    offer(ItemId::STICK, 32, ItemId::EMERALD, 1, 2),
    offer(ItemId::EMERALD, 1, ItemId::ARROW, 16, 5),
    offer(ItemId::FLINT, 10, ItemId::EMERALD, 1, 10),
    offer(ItemId::EMERALD, 2, ItemId::BOW, 1, 15),
    offer(ItemId::STRING, 14, ItemId::EMERALD, 1, 20),
}};
constexpr std::array<TradeOffer, 5> SHEPHERD{{
    offer(ItemId::WHITE_WOOL, 18, ItemId::EMERALD, 1, 2),
    offer(ItemId::EMERALD, 2, ItemId::WHITE_WOOL, 3, 5),
    offer(ItemId::STRING, 12, ItemId::EMERALD, 1, 10),
    offer(ItemId::EMERALD, 3, ItemId::WHITE_BED, 1, 15),
    offer(ItemId::EMERALD, 4, ItemId::WHITE_WOOL, 16, 20),
}};
constexpr std::array<TradeOffer, 5> LEATHERWORKER{{
    offer(ItemId::LEATHER, 6, ItemId::EMERALD, 1, 2),
    offer(ItemId::EMERALD, 3, ItemId::LEATHER_BOOTS, 1, 5),
    offer(ItemId::EMERALD, 5, ItemId::LEATHER_LEGGINGS, 1, 10),
    offer(ItemId::EMERALD, 7, ItemId::LEATHER_CHESTPLATE, 1, 15),
    offer(ItemId::EMERALD, 5, ItemId::LEATHER_HELMET, 1, 20),
}};
constexpr std::array<TradeOffer, 5> ARMORER{{
    offer(ItemId::COAL, 15, ItemId::EMERALD, 1, 2),
    offer(ItemId::EMERALD, 5, ItemId::IRON_HELMET, 1, 5),
    offer(ItemId::EMERALD, 9, ItemId::IRON_CHESTPLATE, 1, 10),
    offer(ItemId::EMERALD, 13, ItemId::DIAMOND_LEGGINGS, 1, 15),
    offer(ItemId::EMERALD, 17, ItemId::DIAMOND_CHESTPLATE, 1, 20),
}};
constexpr std::array<TradeOffer, 5> TOOLSMITH{{
    offer(ItemId::COAL, 15, ItemId::EMERALD, 1, 2),
    offer(ItemId::EMERALD, 1, ItemId::STONE_PICKAXE, 1, 5),
    offer(ItemId::EMERALD, 4, ItemId::IRON_AXE, 1, 10),
    offer(ItemId::EMERALD, 6, ItemId::IRON_PICKAXE, 1, 15),
    offer(ItemId::EMERALD, 18, ItemId::DIAMOND_PICKAXE, 1, 20),
}};
constexpr std::array<TradeOffer, 5> WEAPONSMITH{{
    offer(ItemId::COAL, 15, ItemId::EMERALD, 1, 2),
    offer(ItemId::EMERALD, 3, ItemId::IRON_SWORD, 1, 5),
    offer(ItemId::EMERALD, 7, ItemId::IRON_AXE, 1, 10),
    offer(ItemId::EMERALD, 12, ItemId::DIAMOND_AXE, 1, 15),
    offer(ItemId::EMERALD, 15, ItemId::DIAMOND_SWORD, 1, 20),
}};
constexpr std::array<TradeOffer, 5> EMPTY{};
constexpr std::array<uint16_t, 4> LEVEL_THRESHOLDS{{10, 70, 150, 250}};

} // namespace

VillagerProfession professionForWorkstation(BlockId block) {
    switch (block) {
        case BlockId::COMPOSTER: return VillagerProfession::Farmer;
        case BlockId::FLETCHING_TABLE: return VillagerProfession::Fletcher;
        case BlockId::LOOM: return VillagerProfession::Shepherd;
        case BlockId::CAULDRON: return VillagerProfession::Leatherworker;
        case BlockId::BLAST_FURNACE: return VillagerProfession::Armorer;
        case BlockId::SMITHING_TABLE: return VillagerProfession::Toolsmith;
        case BlockId::GRINDSTONE: return VillagerProfession::Weaponsmith;
        default: return VillagerProfession::Unemployed;
    }
}

BlockId workstationForProfession(VillagerProfession profession) {
    switch (profession) {
        case VillagerProfession::Farmer: return BlockId::COMPOSTER;
        case VillagerProfession::Fletcher: return BlockId::FLETCHING_TABLE;
        case VillagerProfession::Shepherd: return BlockId::LOOM;
        case VillagerProfession::Leatherworker: return BlockId::CAULDRON;
        case VillagerProfession::Armorer: return BlockId::BLAST_FURNACE;
        case VillagerProfession::Toolsmith: return BlockId::SMITHING_TABLE;
        case VillagerProfession::Weaponsmith: return BlockId::GRINDSTONE;
        default: return BlockId::AIR;
    }
}

const std::array<TradeOffer, 5>& villagerOffers(VillagerProfession profession) {
    switch (profession) {
        case VillagerProfession::Farmer: return FARMER;
        case VillagerProfession::Fletcher: return FLETCHER;
        case VillagerProfession::Shepherd: return SHEPHERD;
        case VillagerProfession::Leatherworker: return LEATHERWORKER;
        case VillagerProfession::Armorer: return ARMORER;
        case VillagerProfession::Toolsmith: return TOOLSMITH;
        case VillagerProfession::Weaponsmith: return WEAPONSMITH;
        default: return EMPTY;
    }
}

uint8_t unlockedTradeCount(const VillagerData& villager) {
    return villager.profession == VillagerProfession::Unemployed
        ? 0 : std::clamp<uint8_t>(villager.level, 1, 5);
}

TradeResult executeVillagerTrade(VillagerData& villager, uint8_t offerIndex,
                                 InventoryModel& inventory) {
    if (villager.profession == VillagerProfession::Unemployed || offerIndex >= 5)
        return TradeResult::InvalidOffer;
    if (offerIndex >= unlockedTradeCount(villager)) return TradeResult::LockedOffer;
    const TradeOffer& selected = villagerOffers(villager.profession)[offerIndex];
    if (villager.uses[offerIndex] >= selected.maximumUses)
        return TradeResult::Exhausted;
    if (inventory.count(selected.input.id) < selected.input.count)
        return TradeResult::MissingInput;
    InventoryModel candidate = inventory;
    if (!candidate.remove(selected.input.id, selected.input.count))
        return TradeResult::MissingInput;
    if (candidate.add(selected.output) != 0) return TradeResult::OutputFull;
    inventory = std::move(candidate);
    ++villager.uses[offerIndex];
    villager.professionLocked = true;
    villager.experience = static_cast<uint16_t>(std::min<uint32_t>(
        65535u, villager.experience + selected.experience));
    while (villager.level < 5 &&
           villager.experience >= LEVEL_THRESHOLDS[villager.level - 1])
        ++villager.level;
    return TradeResult::Success;
}

bool restockVillager(VillagerData& villager, uint32_t day,
                     bool reachedWorkstation) {
    if (!reachedWorkstation || !villager.hasWorkstation) return false;
    if (villager.lastRestockDay != day) {
        villager.lastRestockDay = day;
        villager.restocksToday = 0;
    }
    if (villager.restocksToday >= 2) return false;
    villager.uses.fill(0);
    ++villager.restocksToday;
    return true;
}
