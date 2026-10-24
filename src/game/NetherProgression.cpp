#include "game/NetherProgression.h"

std::optional<PotionDefinition> potionDefinition(ItemId item) {
    switch(item) {
#define NETHER_POTION(id,key,label,kind,level,ticks,isSplash) case ItemId::id: return PotionDefinition{{StatusEffectType::kind,level,ticks},bool(isSplash)};
        MC_NETHER_POTIONS(NETHER_POTION)
#undef NETHER_POTION
        default:return {};
    }
}

bool isBrewingBottle(ItemId item) {
    return item==ItemId::WATER_BOTTLE||item==ItemId::AWKWARD_POTION||potionDefinition(item).has_value();
}

bool isBrewingIngredient(ItemId item) {
    switch(item) {
        case ItemId::NETHER_WART:case ItemId::MAGMA_CREAM:case ItemId::BLAZE_POWDER:
        case ItemId::GHAST_TEAR:case ItemId::GLISTERING_MELON:case ItemId::FERMENTED_SPIDER_EYE:
        case ItemId::REDSTONE_DUST:case ItemId::GLOWSTONE_DUST:case ItemId::GUNPOWDER:return true;
        default:return false;
    }
}

std::optional<ItemId> brewPotion(ItemId bottle,ItemId ingredient) {
    const auto potion=potionDefinition(bottle);
    const bool splash=potion&&potion->splash;
    if(splash) {
        const auto normal=static_cast<ItemId>(static_cast<uint16_t>(bottle)-
            (static_cast<uint16_t>(ItemId::SPLASH_FIRE_RESISTANCE_POTION)-static_cast<uint16_t>(ItemId::FIRE_RESISTANCE_POTION)));
        const auto result=brewPotion(normal,ingredient);
        if(!result)return {};
        const auto definition=potionDefinition(*result);
        if(!definition||definition->splash)return {};
        return static_cast<ItemId>(static_cast<uint16_t>(*result)+
            (static_cast<uint16_t>(ItemId::SPLASH_FIRE_RESISTANCE_POTION)-static_cast<uint16_t>(ItemId::FIRE_RESISTANCE_POTION)));
    }
    if(ingredient==ItemId::GUNPOWDER&&potion)
        return static_cast<ItemId>(static_cast<uint16_t>(bottle)+
            (static_cast<uint16_t>(ItemId::SPLASH_FIRE_RESISTANCE_POTION)-static_cast<uint16_t>(ItemId::FIRE_RESISTANCE_POTION)));
    if(bottle==ItemId::WATER_BOTTLE) {
        if(ingredient==ItemId::NETHER_WART)return ItemId::AWKWARD_POTION;
        if(ingredient==ItemId::FERMENTED_SPIDER_EYE)return ItemId::WEAKNESS_POTION;
    }
    if(bottle==ItemId::AWKWARD_POTION) {
        switch(ingredient) {
            case ItemId::MAGMA_CREAM:return ItemId::FIRE_RESISTANCE_POTION;
            case ItemId::BLAZE_POWDER:return ItemId::STRENGTH_POTION;
            case ItemId::GHAST_TEAR:return ItemId::REGENERATION_POTION;
            case ItemId::GLISTERING_MELON:return ItemId::HEALING_POTION;
            default:break;
        }
    }
    if(ingredient==ItemId::FERMENTED_SPIDER_EYE) {
        if(bottle==ItemId::HEALING_POTION)return ItemId::HARMING_POTION;
        if(bottle==ItemId::STRONG_HEALING_POTION)return ItemId::STRONG_HARMING_POTION;
    }
    if(ingredient==ItemId::REDSTONE_DUST) {
        switch(bottle) {
            case ItemId::FIRE_RESISTANCE_POTION:return ItemId::LONG_FIRE_RESISTANCE_POTION;
            case ItemId::STRENGTH_POTION:return ItemId::LONG_STRENGTH_POTION;
            case ItemId::REGENERATION_POTION:return ItemId::LONG_REGENERATION_POTION;
            case ItemId::WEAKNESS_POTION:return ItemId::LONG_WEAKNESS_POTION;
            default:break;
        }
    }
    if(ingredient==ItemId::GLOWSTONE_DUST) {
        switch(bottle) {
            case ItemId::STRENGTH_POTION:return ItemId::STRONG_STRENGTH_POTION;
            case ItemId::REGENERATION_POTION:return ItemId::STRONG_REGENERATION_POTION;
            case ItemId::HEALING_POTION:return ItemId::STRONG_HEALING_POTION;
            case ItemId::HARMING_POTION:return ItemId::STRONG_HARMING_POTION;
            default:break;
        }
    }
    return {};
}

bool tickBrewingStand(BlockEntity& entity) {
    auto& ingredient=entity.chest[3];auto& fuel=entity.chest[4];
    std::array<std::optional<ItemId>,3> results;
    bool valid=false;
    for(size_t i=0;i<3;++i)if(!entity.chest[i].empty()) {
        results[i]=brewPotion(entity.chest[i].id,ingredient.id);valid=valid||results[i].has_value();
    }
    if(!valid||(entity.burnRemaining==0&&(fuel.id!=ItemId::BLAZE_POWDER||fuel.empty()))) {
        const bool changed=entity.cookProgress!=0;entity.cookProgress=0;entity.input.clear();return changed;
    }
    if(entity.input.id!=ingredient.id){entity.input={ingredient.id,1,0};entity.cookProgress=0;}
    entity.cookTotal=400;
    if(++entity.cookProgress<400)return true;
    if(!entity.burnRemaining){entity.burnRemaining=20;if(--fuel.count==0)fuel.clear();}
    --entity.burnRemaining;entity.cookProgress=0;
    for(size_t i=0;i<3;++i)if(results[i])entity.chest[i]={*results[i],1,0};
    if(--ingredient.count==0)ingredient.clear();
    entity.input.clear();return true;
}

bool isNetheriteEquipment(ItemId item) {
    return item>=ItemId::NETHERITE_PICKAXE&&item<=ItemId::NETHERITE_BOOTS;
}

ItemStack smithingResult(const ItemStack& pattern,const ItemStack& equipment,const ItemStack& material) {
    if(pattern.empty()||equipment.empty()||material.empty()||pattern.id!=ItemId::NETHERITE_UPGRADE_TEMPLATE||material.id!=ItemId::NETHERITE_INGOT)return {};
    switch(equipment.id) {
#define NETHER_UPGRADE(id,key,label,base,durability) case ItemId::base:return {ItemId::id,1,equipment.damage};
        MC_NETHER_EQUIPMENT(NETHER_UPGRADE)
#undef NETHER_UPGRADE
        default:return {};
    }
}

bool takeSmithingResult(BlockEntity& entity,ItemStack& destination) {
    const auto result=smithingResult(entity.chest[0],entity.chest[1],entity.chest[2]);
    if(result.empty()||!destination.empty())return false;
    destination=result;
    for(size_t i=0;i<3;++i)if(--entity.chest[i].count==0)entity.chest[i].clear();
    return true;
}
