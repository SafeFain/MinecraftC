#pragma once
#include "game/EnchantingRules.h"
#include "game/InventoryInteraction.h"
#include "game/SurvivalSession.h"

inline void runEquipmentProgressionCases() {
    const std::array<ItemId,9> equipment{{ItemId::IRON_PICKAXE,ItemId::IRON_SWORD,ItemId::IRON_AXE,ItemId::BOW,ItemId::FISHING_ROD,ItemId::IRON_HELMET,ItemId::IRON_CHESTPLATE,ItemId::IRON_LEGGINGS,ItemId::IRON_BOOTS}};
    for (size_t i=0;i<ENCHANTMENTS.size();++i) {
        const auto id=static_cast<EnchantmentId>(i);
        require(std::any_of(equipment.begin(),equipment.end(),[&](ItemId item){return EnchantingRules::supports(item,id);}),"Every enchantment has a supported native equipment consumer");
        require(EnchantingRules::minimumCost(id,1)>0,"Positive minimum enchantment cost");
    }
    {
        InventoryModel armor;
        for (auto& slot : armor.armor()) { slot={ItemId::IRON_CHESTPLATE,1}; slot.components.enchantments.set(EnchantmentId::Protection,4); }
        require(EquipmentRules::protection(armor,EnchantmentId::Protection)==16,"General armor protection sums all four equipped pieces");
        armor.armor()[3].components.enchantments={}; armor.armor()[3].components.enchantments.set(EnchantmentId::FireProtection,4);
        require(EquipmentRules::protection(armor,EnchantmentId::FireProtection)==20,"Specialized protection respects the twenty-point cap");
        require(!compatibleEnchantments(EnchantmentId::Mending,EnchantmentId::Infinity) &&
                !compatibleEnchantments(EnchantmentId::Fortune,EnchantmentId::SilkTouch),"Native exclusion pairs are enforced symmetrically");
        for (uint32_t seed=1;seed<=64;++seed) {
            const auto bare=EnchantingRules::tableOffers({ItemId::IRON_PICKAXE,1},0,seed);
            const auto full=EnchantingRules::tableOffers({ItemId::IRON_PICKAXE,1},15,seed);
            require(bare[2].requiredLevel<=8 && full[2].requiredLevel==30 && full[2].cost==3,"Bookcase power differs from actual three-level fee");
        }
    }
    {
        ItemStack named{ItemId::BOOK,12}; named.components.setCustomName("Book");
        ItemStack cursor, empty;
        InventoryInteraction::click(cursor,named,true);
        require(cursor.count==6&&named.count==6&&cursor.components==named.components,"Split preserves components");
        InventoryInteraction::click(cursor,empty,true);
        require(empty.count==1&&empty.components==named.components,"Right place preserves components");
        InventoryModel inventory;inventory.add(empty);inventory.add({ItemId::BOOK,1});
        require(inventory.slot(0).count==1&&inventory.slot(1).count==1,"Named and unnamed stacks never merge");
        const auto encoded=[&]{std::vector<uint8_t> bytes;writeItemComponents(named.components,[&](uint8_t byte){bytes.push_back(byte);});return bytes;}();
        size_t cursorByte=0;const auto decoded=readItemComponents([&]{return encoded.at(cursorByte++);});
        require(decoded==named.components&&cursorByte==encoded.size(),"Bounded component roundtrip");
    }
    {
        InventoryModel inventory;inventory.slot(0)={ItemId::IRON_PICKAXE,1,9};inventory.slot(0).components.enchantments.set(EnchantmentId::Mending,1);
        inventory.slot(1)=inventory.slot(0);inventory.offhand()=inventory.slot(0).withCount(1);inventory.offhand().damage=1;
        uint64_t random=123;
        const auto remainder=EquipmentRules::repairWithExperience(inventory,0,7,random);
        require(inventory.slot(0).damage==0&&inventory.offhand().damage==0&&inventory.slot(1).damage==9&&remainder==3,"Mending repairs equipped slots, rounds odd damage and returns unused XP");
        inventory.slot(0).damage=8;
        require(EquipmentRules::repairWithExperience(inventory,0,2,random)==0&&inventory.slot(0).damage==4,"Mending consumes XP at two durability per point");
        ItemStack pick{ItemId::IRON_PICKAXE,1};pick.components.enchantments.set(EnchantmentId::Unbreaking,3);
        unsigned breaks=0; for(unsigned trial=0;trial<10000;++trial){pick.damage=0;EquipmentRules::damage(pick,1,random);breaks+=pick.damage;}
        require(breaks>2200&&breaks<2800,"Unbreaking III tool wear distribution");
    }
    {
        BlockEntity station;station.type=BlockEntityType::EnchantingTable;station.chest[0]={ItemId::IRON_PICKAXE,1};station.chest[1]={ItemId::LAPIS_LAZULI,3};
        Experience xp;xp.setLevel(29);uint32_t seed=123;
        InventoryContext context;context.container=&station;context.experience=&xp;context.enchantmentSeed=&seed;context.bookshelves=15;
        InventoryTransaction transaction;InventoryModel inventory;
        const auto original=station.chest[0];
        auto enchant=action(transaction,1,InventoryOperation::Enchant);enchant.argument=2;
        require(!transaction.apply(inventory,enchant,context).accepted&&seed==123&&station.chest[0]==original,"Insufficient levels cannot reroll or consume inputs");
        xp.setLevel(30);enchant.sequence=2;
        require(transaction.apply(inventory,enchant,context).accepted&&xp.level()==27&&station.chest[1].empty()&&!station.chest[0].components.enchantments.empty()&&seed!=123,"Tier three consumes three levels/lapis and advances seed");
        const auto result=station.chest[0];
        require(!transaction.apply(inventory,enchant,context).accepted&&station.chest[0]==result&&xp.level()==27,"Enchant replay never charges twice");
        for(unsigned value=0;value<128;++value){const auto offers=EnchantingRules::tableOffers({ItemId::BOOK,1},15,value);for(const auto& offer:offers)for(size_t i=0;i<ENCHANTMENTS.size();++i)if(ENCHANTMENTS[i].treasure)require(!offer.result.components.storedEnchantments.level(static_cast<EnchantmentId>(i)),"Table excludes every treasure enchantment");}
    }
    {
        ItemStack left{ItemId::IRON_SWORD,1};left.components.enchantments.set(EnchantmentId::Sharpness,1);
        ItemStack book{ItemId::ENCHANTED_BOOK,1};book.components.storedEnchantments.set(EnchantmentId::Sharpness,1);
        const auto result=EnchantingRules::anvil(left,book,std::nullopt,false);
        require(result.item.enchantment(EnchantmentId::Sharpness)==2&&result.cost==2&&result.item.components.repairCost==1,"Anvil same-level upgrade, book multiplier and prior-work penalty");
        left.components.repairCost=63;
        require(EnchantingRules::anvil(left,book,std::nullopt,false).tooExpensive,"Survival rejects expensive work");
        const auto rename=EnchantingRules::anvil(left,{},std::string("Name"),false);
        require(!rename.tooExpensive&&rename.cost==39&&rename.item.components.repairCost==63,"Pure rename capped at 39 without increasing penalty");
        const auto stackRename=EnchantingRules::anvil({ItemId::BOOK,64},{},std::string("Named"),false);
        require(stackRename.item.count==64,"Renaming stack preserves every item");
    }
    for(const auto operation:{InventoryOperation::Click,InventoryOperation::QuickMove,InventoryOperation::SwapHotbar,InventoryOperation::SwapOffhand,InventoryOperation::Drop}) {
        BlockEntity station;station.type=BlockEntityType::Grindstone;station.chest[0]={ItemId::IRON_PICKAXE,1,50};station.chest[0].components.enchantments.set(EnchantmentId::Unbreaking,3);station.chest[0].components.enchantments.set(EnchantmentId::VanishingCurse,1);station.chest[0].components.setCustomName("Pick");
        InventoryModel inventory;InventoryTransaction transaction;Experience xp;uint32_t seed=9;
        InventoryContext context;context.container=&station;context.experience=&xp;context.enchantmentSeed=&seed;
        auto take=action(transaction,1,operation,{InventoryArea::Container,2});take.argument=0;
        const auto outcome=transaction.apply(inventory,take,context);
        require(outcome.accepted&&station.chest[0].empty()&&outcome.workstationExperience>=11&&outcome.workstationExperience<=21,"Every grindstone output gesture settles XP once");
        require(seed==9,"Grinding never refreshes enchanting-table offers");
        ItemStack result=operation==InventoryOperation::Click?transaction.cursor():operation==InventoryOperation::SwapOffhand?inventory.offhand():operation==InventoryOperation::Drop?outcome.drops.at(0):inventory.slot(0);
        require(result.enchantment(EnchantmentId::VanishingCurse)==1&&!result.enchantment(EnchantmentId::Unbreaking)&&result.components.customName()=="Pick","Grinding retains curse and name only");
        require(!transaction.apply(inventory,take,context).accepted,"Result gesture replay rejected");
    }
    {
        InventoryModel inventory;inventory.armor()[0]={ItemId::IRON_HELMET,1};inventory.armor()[0].components.enchantments.set(EnchantmentId::BindingCurse,1);
        InventoryTransaction transaction;InventoryContext context;
        auto take=action(transaction,1,InventoryOperation::QuickMove,{InventoryArea::Armor,0});
        require(!transaction.apply(inventory,take,context).accepted&&!inventory.armor()[0].empty(),"Binding prevents quick-move armor removal");
    }
}
