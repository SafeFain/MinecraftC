#pragma once
#include "world/BlockEntity.h"
#include "world/Structure.h"
#include "world/WorldGenContext.h"
#include <vector>
namespace FrozenHeavenReference {
struct LootEntry {
    ItemId item;
    uint8_t minimum;
    uint8_t maximum;
    uint8_t chance;
};

inline std::vector<LootEntry> lootEntries(StructureLootProfile profile) {
    using P=StructureLootProfile;
    switch(profile) {
        case P::AbandonedFarmstead:
        case P::Village: return {{ItemId::BREAD,1,4,90},{ItemId::WHEAT,2,7,80},
            {ItemId::WHEAT_SEEDS,2,8,75},{ItemId::COAL,1,4,45},
            {ItemId::OAK_SAPLING,1,3,40},{ItemId::RAW_IRON,1,2,20}};
        case P::SwampHut:
        case P::TravelerHut: return {{ItemId::BREAD,1,3,80},{ItemId::STICK,2,8,90},
            {ItemId::COAL,1,4,65},{ItemId::LEATHER,1,3,45},
            {ItemId::OAK_SAPLING,1,2,55}};
        case P::AbandonedCamp: return {{ItemId::STICK,2,7,90},{ItemId::STRING,1,4,70},
            {ItemId::ARROW,2,8,75},{ItemId::BONE,1,4,60},
            {ItemId::ROTTEN_FLESH,1,3,35},{ItemId::BREAD,1,2,30}};
        case P::Igloo: return {{ItemId::COAL,2,6,85},{ItemId::BREAD,1,3,75},
            {ItemId::LEATHER,1,3,55}};
        case P::DesertTemple:
        case P::JungleRuins:
        case P::MountainWatchtower:
        case P::StoneCircle:
        case P::RuinedTower: return {{ItemId::ARROW,3,10,90},{ItemId::BONE,2,6,80},
            {ItemId::COAL,2,5,65},{ItemId::RAW_IRON,1,3,35},
            {ItemId::GOLD_INGOT,1,2,12}};
        case P::LumberCamp: return {{ItemId::SPRUCE_LOG,3,10,95},{ItemId::STICK,3,12,90},
            {ItemId::SPRUCE_SAPLING,1,4,75},{ItemId::WOODEN_AXE,1,1,25}};
        case P::XiguangRuin: return {{ItemId::SUNSTONE,3,9,100},
            {ItemId::STAR_CRYSTAL,3,7,90},{ItemId::STARFLOWER,1,4,70},
            {ItemId::CLOUD_BLOOM,1,3,50}};
        case P::StarCrystalGeode: return {{ItemId::STAR_CRYSTAL,2,6,100},
            {ItemId::CLOUDSTONE,2,7,80},{ItemId::STARFLOWER,1,4,60}};
        case P::CloudspireTower: return {{ItemId::CLOUDSTONE,2,6,100},
            {ItemId::SUNSTONE,1,4,70},{ItemId::STAR_CRYSTAL,1,2,35},
            {ItemId::CLOUD_BLOOM,1,3,45}};
        case P::SkywayShrine: return {{ItemId::STARSTEP_SCEPTER,1,1,25},
            {ItemId::STAR_CRYSTAL,2,5,100},{ItemId::SUNSTONE,2,6,90},
            {ItemId::CLOUDSTONE,3,8,100}};
        case P::OriginSkywayShrine: return {{ItemId::STARSTEP_SCEPTER,1,1,100},
            {ItemId::STAR_CRYSTAL,2,5,100},{ItemId::SUNSTONE,2,6,100},
            {ItemId::CLOUDSTONE,3,8,100}};
        default:return{};
    }
}

inline void populateGeneratedLoot(BlockEntity& entity, StructureLootProfile profile,
                           uint64_t seed) {
    if(entity.type!=BlockEntityType::Chest||profile==StructureLootProfile::None)
        return;
    std::vector<LootEntry> entries=lootEntries(profile);
    if (profile >= StructureLootProfile::XiguangRuin &&
        profile <= StructureLootProfile::OriginSkywayShrine) {
        entries.push_back({ItemId::STAR_CRYSTAL_SHARD, 2, 5, 85});
        entries.push_back({ItemId::CLOUDBERRY, 2, 4, 70});
        const ItemId brick = profile == StructureLootProfile::XiguangRuin
            ? ItemId::SUNSTONE_BRICKS : profile == StructureLootProfile::StarCrystalGeode
            ? ItemId::MOONSTONE_BRICKS : ItemId::CLOUDSTONE_BRICKS;
        entries.push_back({brick, 3, 8, 80});
    }
    for(size_t i=0;i<entries.size();++i) {
        const LootEntry& entry=entries[i];
        const uint64_t h=WorldGenContext::mix(seed^
            (static_cast<uint64_t>(i)+1u)*0x9E3779B97F4A7C15ULL);
        if(h%100u>=entry.chance)continue;
        const uint8_t count=static_cast<uint8_t>(entry.minimum+
            ((h>>8)%(entry.maximum-entry.minimum+1u)));
        size_t slot=static_cast<size_t>((h>>24)%entity.chest.size());
        for(size_t probe=0;probe<entity.chest.size();++probe) {
            ItemStack& target=entity.chest[(slot+probe)%entity.chest.size()];
            if(target.empty()){target={entry.item,count,0};break;}
        }
    }
}
}
