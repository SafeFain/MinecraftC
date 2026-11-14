#include "world/BlockEntityLogic.h"
#include "game/NetherProgression.h"
#include "game/InventoryTransaction.h"
#include "game/InventoryInteraction.h"
#include "game/SurvivalRules.h"
#include "game/EnchantingRules.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace {
bool armorAccepts(size_t slot,ItemId id) {
    if (id == ItemId::EMPTY) return true;
    if (slot >= 4 || getItemProps(id).kind != ItemKind::Armor) return false;
    if(id>=ItemId::NETHERITE_HELMET&&id<=ItemId::NETHERITE_BOOTS)return static_cast<uint16_t>(id)-static_cast<uint16_t>(ItemId::NETHERITE_HELMET)==slot;
    return static_cast<size_t>((static_cast<uint16_t>(id) - static_cast<uint16_t>(ItemId::LEATHER_HELMET)) % 4) == slot;
}
bool same(const ItemStack& a,const ItemStack& b) {return a == b;}
bool sameContainer(const BlockEntity& a,const BlockEntity& b) {
    for(size_t i=0;i<a.chest.size();++i) if(!same(a.chest[i],b.chest[i])) return false;
    return same(a.input,b.input) && same(a.fuel,b.fuel) && same(a.output,b.output);
}
struct Working {
    InventoryModel inventory;
    ItemStack cursor;
    std::array<ItemStack,9> crafting;
    std::optional<BlockEntity> container;
    bool table;
    std::vector<ItemStack> drops;
    Experience experience{};
    uint32_t seed = 1, awardedExperience = 0;
    bool creative = false, anvilUsed = false;
    unsigned bookshelves = 0;
    uint64_t randomState = 0x6a09e667f3bcc909ULL;
    bool takeWorkstation(ItemStack& destination) {
        if (!container || !destination.empty() || container->type == BlockEntityType::EnchantingTable) return false;
        const auto result = container->type == BlockEntityType::Anvil ?
            EnchantingRules::anvil(container->chest[0], container->chest[1], container->rename, creative) :
            EnchantingRules::grindstone(container->chest[0], container->chest[1]);
        if (result.item.empty() || result.tooExpensive || (!creative && static_cast<uint32_t>(experience.level()) < result.cost)) return false;
        destination = result.item;
        if (!creative) experience.addLevels(-static_cast<int32_t>(result.cost));
        if (container->type == BlockEntityType::Anvil) {
            container->chest[0].clear();
            if (result.consumed) { container->chest[1].count -= static_cast<uint8_t>(result.consumed); if (!container->chest[1].count) container->chest[1].clear(); }
            container->rename.reset(); anvilUsed = true;
        } else {
            container->chest[0].clear(); container->chest[1].clear();
            if (result.experienceMinimum) { const auto half = (result.experienceMinimum + 1) / 2; awardedExperience += half + EquipmentRules::random(randomState) % half; }
        }
        container->output.clear();
        return true;
    }
    ItemStack* slot(InventorySlot reference) {
        switch(reference.area) {
        case InventoryArea::Storage: return reference.index<36?&inventory.slot(reference.index):nullptr;
        case InventoryArea::Armor: return reference.index<4?&inventory.armor()[reference.index]:nullptr;
        case InventoryArea::Offhand: return reference.index==0?&inventory.offhand():nullptr;
        case InventoryArea::Crafting: return reference.index<(table?9:4)?&crafting[reference.index]:nullptr;
        case InventoryArea::Container:
            if(!container) return nullptr;
            if (isWorkstation(container->type) && reference.index == 2) return nullptr;
            if(container->type==BlockEntityType::SmithingTable&&reference.index==3)return nullptr;
            return containerStack(*container,reference.index);
        case InventoryArea::Output: return nullptr;
        }
        return nullptr;
    }
    bool accepts(InventorySlot reference,ItemId id) const {
        if(id==ItemId::EMPTY) return true;
        if(reference.area==InventoryArea::Armor) return armorAccepts(reference.index,id);
        if(reference.area==InventoryArea::Container&&container)return containerAccepts(*container,reference.index,id);
        return reference.area!=InventoryArea::Output;
    }
    std::vector<ItemStack*> storage() {
        std::vector<ItemStack*> result;
        for(size_t i=0;i<36;++i) result.push_back(&inventory.slot(i));
        return result;
    }
    bool click(InventorySlot reference,bool right) {
        if (reference.area == InventoryArea::Container && container && isWorkstation(container->type) && reference.index == 2) return takeWorkstation(cursor);
        if(reference.area==InventoryArea::Container&&container&&container->type==BlockEntityType::SmithingTable&&reference.index==3)
            return takeSmithingResult(*container,cursor);
        auto* target=slot(reference);if(!target) return false;
        if(reference.area==InventoryArea::Container && container && container->type==BlockEntityType::Furnace && reference.index==2) {
            if (!cursor.empty()) return false;
            InventoryInteraction::click(cursor,*target,right);
            return true;
        }
        if(!accepts(reference,cursor.id)) return false;
        InventoryInteraction::click(cursor,*target,right);return true;
    }
    bool craft() {
        std::array<ItemId,9> grid{};for(size_t i=0;i<grid.size();++i) grid[i]=crafting[i].id;
        const auto* recipe=findCraftingRecipe(grid,table?3:2,table?3:2);if(!recipe) return false;
        const auto output=recipe->output;
        if(!cursor.empty() && (!cursor.sameItem(output))) return false;
        if(static_cast<int>(cursor.count)+output.count>getItemProps(output.id).maxStack) return false;
        if(cursor.empty()) cursor=output;else cursor.count+=output.count;
        for(size_t i=0;i<(table?9u:4u);++i) if(!crafting[i].empty() && --crafting[i].count==0) crafting[i].clear();
        return true;
    }
    void returnStack(ItemStack& stack) {
        if(stack.empty()) return;
        const auto remaining=inventory.add(stack);
        if(remaining) {stack.count=static_cast<uint8_t>(remaining);drops.push_back(stack);}
        stack.clear();
    }
};
}
InventoryOutcome InventoryTransaction::apply(InventoryModel& inventory,const InventoryAction& action,const InventoryContext& context) {
    if(!action.sequence || action.sequence<=m_sequence) return {};
    m_sequence=action.sequence;
    if(action.revision!=m_revision || (context.container && action.containerRevision!=context.containerRevision) || action.targets.size()>64) return {};
    Working next{inventory,m_cursor,m_crafting,std::nullopt,context.craftingTable,{}};
    if(context.container) next.container=*context.container;
    if (context.experience) next.experience = *context.experience;
    if (context.enchantmentSeed) next.seed = *context.enchantmentSeed;
    if (context.randomState) next.randomState = *context.randomState;
    next.creative = context.creative; next.bookshelves = context.bookshelves;
    bool accepted=false;
    switch(action.operation) {
    case InventoryOperation::Click:
        accepted=action.slot.area==InventoryArea::Output?next.craft():next.click(action.slot,action.alternate);break;
    case InventoryOperation::Craft: accepted=next.craft();break;
    case InventoryOperation::Enchant: {
        if (!next.container || next.container->type != BlockEntityType::EnchantingTable || action.argument > 2 || !context.experience || !context.enchantmentSeed) break;
        const auto offers = EnchantingRules::tableOffers(next.container->chest[0], next.bookshelves, next.seed);
        const auto& offer = offers[action.argument];
        if (offer.result.empty() || (!context.creative && (static_cast<uint32_t>(next.experience.level()) < offer.requiredLevel || next.container->chest[1].id != ItemId::LAPIS_LAZULI || next.container->chest[1].count < offer.cost))) break;
        // A book stack must be split first; equipment is already unstackable.
        if (next.container->chest[0].count != 1) break;
        next.container->chest[0] = offer.result;
        if (!context.creative) { next.experience.addLevels(-static_cast<int32_t>(offer.cost)); next.container->chest[1].count -= static_cast<uint8_t>(offer.cost); if (!next.container->chest[1].count) next.container->chest[1].clear(); }
        uint64_t random = next.seed; next.seed = EquipmentRules::random(random); accepted = true; break;
    }
    case InventoryOperation::Rename:
        if (next.container && next.container->type == BlockEntityType::Anvil && ItemComponents::validName(action.text)) { next.container->rename = action.text; accepted = true; }
        break;
    case InventoryOperation::QuickMove: {
        if (action.slot.area == InventoryArea::Container && next.container && isWorkstation(next.container->type) && action.slot.index == 2) {
            ItemStack output;
            if (next.takeWorkstation(output) && next.inventory.add(output) == 0) accepted = true;
            break;
        }
        if (action.slot.area == InventoryArea::Output) {
            for (int iteration = 0; iteration < 64; ++iteration) {
                std::array<ItemId, 9> grid{}; for (size_t i = 0; i < 9; ++i) grid[i] = next.crafting[i].id;
                const auto* recipe = findCraftingRecipe(grid, next.table ? 3 : 2, next.table ? 3 : 2);
                if (!recipe) break;
                auto inventory = next.inventory; if (inventory.add(recipe->output)) break;
                next.inventory = inventory;
                for (size_t i = 0; i < (next.table ? 9u : 4u); ++i)
                    if (!next.crafting[i].empty() && --next.crafting[i].count == 0) next.crafting[i].clear();
                accepted = true;
            }
            break;
        }
        if(action.slot.area==InventoryArea::Container&&next.container&&next.container->type==BlockEntityType::SmithingTable&&action.slot.index==3) {
            const auto result=smithingResult(next.container->chest[0],next.container->chest[1],next.container->chest[2]);
            auto inventory=next.inventory;
            if(!result.empty()&&inventory.add(result)==0) {ItemStack taken;accepted=takeSmithingResult(*next.container,taken);if(accepted)next.inventory=std::move(inventory);}
            break;
        }
        auto* source=next.slot(action.slot);if(!source) break;
        if (action.slot.area == InventoryArea::Storage && !next.container && getItemProps(source->id).kind == ItemKind::Armor) {
            bool equipped = false;
            for (size_t i = 0; i < 4; ++i) if (next.inventory.armor()[i].empty() && armorAccepts(i, source->id)) {
                next.inventory.armor()[i] = *source; source->clear(); equipped = true; break;
            }
            if (equipped) { accepted = true; break; }
        }
        auto targets=next.storage();
        if(action.slot.area==InventoryArea::Storage && next.container) {
            targets.clear();const int count=containerStackCount(next.container->type);
            for(int i=0;i<count;++i) {
                InventorySlot reference{InventoryArea::Container,static_cast<uint8_t>(i)};
                if(next.accepts(reference,source->id)) targets.push_back(next.slot(reference));
            }
        } else if(action.slot.area==InventoryArea::Storage) {
            targets.clear();const size_t start=action.slot.index<9?9:0,end=action.slot.index<9?36:9;
            for(size_t i=start;i<end;++i) targets.push_back(&next.inventory.slot(i));
        }
        InventoryInteraction::transfer(*source,targets);accepted=true;break;
    }
    case InventoryOperation::SwapHotbar:
    case InventoryOperation::SwapOffhand: {
        if (action.slot.area == InventoryArea::Container && next.container && isWorkstation(next.container->type) && action.slot.index == 2) {
            if (action.operation == InventoryOperation::SwapHotbar && action.argument >= 9) break;
            auto& destination = action.operation == InventoryOperation::SwapHotbar ? next.inventory.slot(action.argument) : next.inventory.offhand();
            accepted = next.takeWorkstation(destination); break;
        }
        if(action.slot.area==InventoryArea::Container&&next.container&&next.container->type==BlockEntityType::SmithingTable&&action.slot.index==3) {
            if(action.operation==InventoryOperation::SwapHotbar&&action.argument>=9)break;
            auto& destination=action.operation==InventoryOperation::SwapHotbar?next.inventory.slot(action.argument):next.inventory.offhand();
            accepted=takeSmithingResult(*next.container,destination);break;
        }
        auto* source=next.slot(action.slot);if(!source || (action.operation==InventoryOperation::SwapHotbar && action.argument>=9)) break;
        InventorySlot targetReference{action.operation==InventoryOperation::SwapHotbar?InventoryArea::Storage:InventoryArea::Offhand,static_cast<uint8_t>(action.operation==InventoryOperation::SwapHotbar?action.argument:0)};
        auto* target=next.slot(targetReference);
        if(!next.accepts(action.slot,target->id) || !next.accepts(targetReference,source->id)) break;
        std::swap(*source,*target);accepted=true;break;
    }
    case InventoryOperation::Drop: {
        if (action.slot.area == InventoryArea::Container && next.container && isWorkstation(next.container->type) && action.slot.index == 2) {
            ItemStack output; accepted = next.takeWorkstation(output); if (accepted) next.drops.push_back(output); break;
        }
        if(action.slot.area==InventoryArea::Container&&next.container&&next.container->type==BlockEntityType::SmithingTable&&action.slot.index==3) {
            ItemStack output;accepted=takeSmithingResult(*next.container,output);if(accepted)next.drops.push_back(output);break;
        }
        auto* source=next.slot(action.slot);if(!source) break;
        if(!source->empty()) {
            if(action.alternate) {next.drops.push_back(*source);source->clear();}
            else next.drops.push_back(InventoryInteraction::takeOne(*source));
        }
        accepted=true;break;
    }
    case InventoryOperation::Gather:
    case InventoryOperation::Distribute: {
        std::vector<ItemStack*> targets;
        bool valid=true;
        for(size_t i=0;i<action.targets.size();++i) {
            const auto reference=action.targets[i];auto* target=next.slot(reference);
            if(!target || std::find(targets.begin(),targets.end(),target)!=targets.end() ||
                (action.operation==InventoryOperation::Distribute && !next.accepts(reference,next.cursor.id))) {valid=false;break;}
            targets.push_back(target);
        }
        if(!valid) break;
        if(action.operation==InventoryOperation::Gather) InventoryInteraction::gather(next.cursor,targets);
        else InventoryInteraction::distribute(next.cursor,targets,action.alternate);
        accepted=true;break;
    }
    case InventoryOperation::FillRecipe: {
        const auto& recipes=craftingRecipes();if(action.argument>=recipes.size()) break;
        accepted=fillCraftingRecipe(recipes[action.argument],next.inventory,next.crafting,context.craftingTable?3:2,context.craftingTable?3:2);break;
    }
    case InventoryOperation::CreativeGrant: {
        if(!context.creative || (action.slot.area!=InventoryArea::Storage && action.slot.area!=InventoryArea::Crafting)) break;
        auto* target=next.slot(action.slot);const auto item=static_cast<ItemId>(action.argument);
        if(!target || !isValidItemId(item) || item==ItemId::EMPTY) break;
        InventoryInteraction::setCreativeItem(*target,item);
        if (item == ItemId::ENCHANTED_BOOK && !action.text.empty()) {
            bool found = false;
            for (size_t i = 0; i < ENCHANTMENTS.size(); ++i) if (ENCHANTMENTS[i].key == action.text) { target->components.storedEnchantments.set(static_cast<EnchantmentId>(i),ENCHANTMENTS[i].maximumLevel); found = true; break; }
            if (!found) break;
        }
        accepted=true;break;
    }
    case InventoryOperation::CreativeClone: {
        if (!context.creative) break;
        const auto* source = next.slot(action.slot); if (!source || source->empty()) break;
        next.cursor = source->withCount(getItemProps(source->id).maxStack); accepted = true; break;
    }
    case InventoryOperation::Close:
        next.returnStack(next.cursor);for(auto& stack:next.crafting) next.returnStack(stack);accepted=true;break;
    }
    if (!context.creative) for (size_t i = 0; i < 4; ++i)
        if (inventory.armor()[i].enchantment(EnchantmentId::BindingCurse) && inventory.armor()[i] != next.inventory.armor()[i]) return {};
    if(!accepted) return {};
    if (context.container && context.container->type == BlockEntityType::Anvil && action.operation != InventoryOperation::Rename && context.container->chest[0] != next.container->chest[0]) next.container->rename.reset();
    std::map<std::string,uint64_t> experience;
    if(context.container && context.container->type==BlockEntityType::Furnace &&
       next.container->output.count < context.container->output.count)
        experience.swap(next.container->furnaceExperience);
    const bool changed=context.container && (!sameContainer(*context.container,*next.container) || !experience.empty());
    if (context.experience) *context.experience = next.experience;
    if (context.enchantmentSeed) *context.enchantmentSeed = next.seed;
    if (context.randomState) *context.randomState = next.randomState;
    inventory=next.inventory;m_cursor=next.cursor;m_crafting=next.crafting;++m_revision;
    if(context.container) *context.container=*next.container;
    return {true,changed,std::move(next.drops),std::move(experience),next.awardedExperience,next.anvilUsed};
}

std::vector<ItemStack> InventoryTransaction::close(InventoryModel& inventory) {
    Working next{inventory,m_cursor,m_crafting,std::nullopt,false,{}};
    for (auto& stack : recoveredWorkstationInputs) next.returnStack(stack);
    if (workstation) { next.returnStack(workstation->chest[0]); next.returnStack(workstation->chest[1]); workstation.reset(); }
    next.returnStack(next.cursor);
    for (auto& stack:next.crafting) next.returnStack(stack);
    inventory=next.inventory;m_cursor=next.cursor;m_crafting=next.crafting;++m_revision;
    return std::move(next.drops);
}
