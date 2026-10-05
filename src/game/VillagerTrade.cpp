#include "game/VillagerTrade.h"
#include "Config.h"

#include <algorithm>
#include <cmath>

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
constexpr std::array<TradeOffer,5> FISHERMAN{{
    offer(ItemId::RAW_COD, 12, ItemId::EMERALD, 1, 2),
    offer(ItemId::EMERALD, 1, ItemId::COOKED_COD, 6, 7),
    offer(ItemId::RAW_SALMON, 10, ItemId::EMERALD, 1, 12),
    offer(ItemId::EMERALD, 2, ItemId::COOKED_SALMON, 6, 17),
    offer(ItemId::EMERALD, 8, ItemId::FISHING_ROD, 1, 22),
}};
constexpr std::array<TradeOffer,5> LIBRARIAN{{
    offer(ItemId::REEDS, 24, ItemId::EMERALD, 1, 2),
    offer(ItemId::EMERALD, 1, ItemId::GLASS, 8, 7),
    offer(ItemId::FEATHER, 16, ItemId::EMERALD, 1, 12),
    offer(ItemId::EMERALD, 2, ItemId::TORCH, 1, 17),
    offer(ItemId::EMERALD, 3, ItemId::LECTERN, 1, 22),
}};
constexpr std::array<TradeOffer,5> CARTOGRAPHER{{
    offer(ItemId::GLASS, 16, ItemId::EMERALD, 1, 2),
    offer(ItemId::EMERALD, 2, ItemId::SANDSTONE, 12, 7),
    offer(ItemId::CLAY_BALL, 24, ItemId::EMERALD, 1, 12),
    offer(ItemId::EMERALD, 3, ItemId::BLUE_WOOL, 8, 17),
    offer(ItemId::EMERALD, 4, ItemId::CARTOGRAPHY_TABLE, 1, 22),
}};
constexpr std::array<TradeOffer,5> CLERIC{{
    offer(ItemId::ROTTEN_FLESH, 32, ItemId::EMERALD, 1, 2),
    offer(ItemId::EMERALD, 2, ItemId::GLOW_FERN, 4, 7),
    offer(ItemId::GOLD_INGOT, 3, ItemId::EMERALD, 1, 12),
    offer(ItemId::EMERALD, 3, ItemId::RESONANT_CRYSTAL, 4, 17),
    offer(ItemId::EMERALD, 5, ItemId::BREWING_STAND, 1, 22),
}};
constexpr std::array<TradeOffer,5> BUTCHER{{
    offer(ItemId::RAW_CHICKEN, 14, ItemId::EMERALD, 1, 2),
    offer(ItemId::EMERALD, 1, ItemId::COOKED_CHICKEN, 5, 7),
    offer(ItemId::RAW_PORKCHOP, 10, ItemId::EMERALD, 1, 12),
    offer(ItemId::EMERALD, 2, ItemId::COOKED_PORKCHOP, 5, 17),
    offer(ItemId::EMERALD, 3, ItemId::STEAK, 6, 22),
}};
constexpr std::array<TradeOffer,5> MASON{{
    offer(ItemId::CLAY_BALL, 10, ItemId::EMERALD, 1, 2),
    offer(ItemId::EMERALD, 1, ItemId::BRICK, 12, 7),
    offer(ItemId::STONE, 20, ItemId::EMERALD, 1, 12),
    offer(ItemId::EMERALD, 2, ItemId::STONE_BRICKS, 16, 17),
    offer(ItemId::EMERALD, 3, ItemId::POLISHED_GRANITE, 16, 22),
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
        case BlockId::BARREL: return VillagerProfession::Fisherman;
        case BlockId::LECTERN: return VillagerProfession::Librarian;
        case BlockId::CARTOGRAPHY_TABLE: return VillagerProfession::Cartographer;
        case BlockId::BREWING_STAND: return VillagerProfession::Cleric;
        case BlockId::SMOKER: return VillagerProfession::Butcher;
        case BlockId::STONECUTTER: return VillagerProfession::Mason;
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
        case VillagerProfession::Fisherman: return BlockId::BARREL;
        case VillagerProfession::Librarian: return BlockId::LECTERN;
        case VillagerProfession::Cartographer: return BlockId::CARTOGRAPHY_TABLE;
        case VillagerProfession::Cleric: return BlockId::BREWING_STAND;
        case VillagerProfession::Butcher: return BlockId::SMOKER;
        case VillagerProfession::Mason: return BlockId::STONECUTTER;
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
        case VillagerProfession::Fisherman: return FISHERMAN;
        case VillagerProfession::Librarian: return LIBRARIAN;
        case VillagerProfession::Cartographer: return CARTOGRAPHER;
        case VillagerProfession::Cleric: return CLERIC;
        case VillagerProfession::Butcher: return BUTCHER;
        case VillagerProfession::Mason: return MASON;
        default: return EMPTY;
    }
}

uint8_t unlockedTradeCount(const VillagerData& villager) {
    return !villager.adult() || villager.profession == VillagerProfession::Unemployed
        ? 0 : std::clamp<uint8_t>(villager.level, 1, 5);
}

TradeResult executeVillagerTrade(VillagerData& villager, uint8_t offerIndex,
                                 InventoryModel& inventory) {
    if (villager.profession == VillagerProfession::Unemployed || offerIndex >= 5)
        return TradeResult::InvalidOffer;
    if (offerIndex >= unlockedTradeCount(villager)) return TradeResult::LockedOffer;
    const TradeOffer selected = villagerQuote(villager, offerIndex);
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
    villager.demand[offerIndex]=std::min<uint8_t>(25,villager.demand[offerIndex]+2);
    if(villager.tradesToday<5) {
        ++villager.tradesToday;
        villager.reputation=std::min<int16_t>(100,villager.reputation+1);
    }
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
    for(auto& demand:villager.demand)demand=demand>5 ? demand-5 : 0;
    ++villager.restocksToday;
    return true;
}

TradeOffer villagerQuote(const VillagerData& v,uint8_t index) {
    if(index>=5)return {};
    auto offer=villagerOffers(v.profession)[index];
    if(offer.input.empty())return offer;
    const int percent=100-std::clamp<int>(v.reputation,-100,100)/4+
        std::min<int>(25,v.demand[index]);
    offer.input.count=static_cast<uint8_t>(std::clamp<int>(
        (offer.input.count*percent+99)/100,1,getItemProps(offer.input.id).maxStack));
    return offer;
}
const char* villagerProfessionKey(VillagerProfession profession) {
    static constexpr const char* keys[]={"unemployed","farmer","fletcher","shepherd",
        "leatherworker","armorer","toolsmith","weaponsmith","fisherman","librarian",
        "cartographer","cleric","butcher","mason"};
    const auto index=static_cast<size_t>(profession);
    return index<std::size(keys)?keys[index]:keys[0];
}
int villagerFoodCount(const VillagerData& v,ItemId item) {
    int total=0;for(const auto& stack:v.food)if(stack.id==item)total+=stack.count;
    return total;
}
int addVillagerFood(VillagerData& v,ItemStack stack) {
    if(stack.id!=ItemId::WHEAT && stack.id!=ItemId::WHEAT_SEEDS && stack.id!=ItemId::BREAD)
        return stack.count;
    int left=stack.count;
    for(auto& slot:v.food)if(slot.id==stack.id && slot.count<64) {
        const int count=std::min(left,64-slot.count);slot.count+=count;left-=count;
    }
    for(auto& slot:v.food)if(slot.empty() && left>0) {
        const int count=std::min(left,64);slot={stack.id,static_cast<uint8_t>(count),0};left-=count;
    }
    return left;
}
bool consumeVillagerFood(VillagerData& v,ItemId item,int count) {
    if(count<0 || villagerFoodCount(v,item)<count)return false;
    for(auto& slot:v.food)if(slot.id==item) {
        const int take=std::min<int>(slot.count,count);slot.count-=take;count-=take;
        if(slot.count==0)slot.clear();
    }
    return true;
}
bool willingToBreed(const VillagerData& v) {
    return v.adult() && v.breedingCooldown<=0 &&
        (villagerFoodCount(v,ItemId::BREAD)>=3 || villagerFoodCount(v,ItemId::WHEAT)>=12);
}
void consumeBreedingFood(VillagerData& v) {
    if(!consumeVillagerFood(v,ItemId::BREAD,3))consumeVillagerFood(v,ItemId::WHEAT,12);
    v.breedingCooldown=Config::VILLAGER_BREEDING_COOLDOWN;
}
void advanceVillagerLife(VillagerData& v,float dt,uint32_t day) {
    if(!std::isfinite(dt) || dt<=0)return;
    v.growthSeconds=std::max(0.0f,v.growthSeconds-dt);
    v.breedingCooldown=std::max(0.0f,v.breedingCooldown-dt);
    v.defenseCooldown=std::max(0.0f,v.defenseCooldown-dt);
    if(v.reputationDay!=day) {
        v.reputationDay=day;v.tradesToday=0;
        if(v.reputation>0)--v.reputation;else if(v.reputation<0)++v.reputation;
    }
}
