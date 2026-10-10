#include "game/InventoryTransaction.h"
#include "game/InventoryInteraction.h"
#include "game/SurvivalRules.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace {
bool armorAccepts(size_t slot,ItemId id) {
    if (id == ItemId::EMPTY) return true;
    if (slot >= 4 || getItemProps(id).kind != ItemKind::Armor) return false;
    return static_cast<size_t>((static_cast<uint16_t>(id) - static_cast<uint16_t>(ItemId::LEATHER_HELMET)) % 4) == slot;
}
bool same(const ItemStack& a,const ItemStack& b) {return a.id==b.id && a.count==b.count && a.damage==b.damage;}
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
    ItemStack* slot(InventorySlot reference) {
        switch(reference.area) {
        case InventoryArea::Storage: return reference.index<36?&inventory.slot(reference.index):nullptr;
        case InventoryArea::Armor: return reference.index<4?&inventory.armor()[reference.index]:nullptr;
        case InventoryArea::Offhand: return reference.index==0?&inventory.offhand():nullptr;
        case InventoryArea::Crafting: return reference.index<(table?9:4)?&crafting[reference.index]:nullptr;
        case InventoryArea::Container:
            if(!container) return nullptr;
            if(container->type==BlockEntityType::Chest) return reference.index<27?&container->chest[reference.index]:nullptr;
            if(container->type==BlockEntityType::Furnace) {
                if(reference.index==0) return &container->input;
                if(reference.index==1) return &container->fuel;
                if(reference.index==2) return &container->output;
            }
            return nullptr;
        case InventoryArea::Output: return nullptr;
        }
        return nullptr;
    }
    bool accepts(InventorySlot reference,ItemId id) const {
        if(id==ItemId::EMPTY) return true;
        if(reference.area==InventoryArea::Armor) return armorAccepts(reference.index,id);
        if(reference.area==InventoryArea::Container && container && container->type==BlockEntityType::Furnace) {
            if(reference.index==0) return findSmeltingRecipe(id)!=nullptr;
            if(reference.index==1) return fuelTicks(id)>0;
            return false;
        }
        return reference.area!=InventoryArea::Output;
    }
    std::vector<ItemStack*> storage() {
        std::vector<ItemStack*> result;
        for(size_t i=0;i<36;++i) result.push_back(&inventory.slot(i));
        return result;
    }
    bool click(InventorySlot reference,bool right) {
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
        if(!cursor.empty() && (cursor.id!=output.id || cursor.damage!=output.damage)) return false;
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
    bool accepted=false;
    switch(action.operation) {
    case InventoryOperation::Click:
        accepted=action.slot.area==InventoryArea::Output?next.craft():next.click(action.slot,action.alternate);break;
    case InventoryOperation::Craft: accepted=next.craft();break;
    case InventoryOperation::QuickMove: {
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
            targets.clear();const int count=next.container->type==BlockEntityType::Chest?27:2;
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
        auto* source=next.slot(action.slot);if(!source || (action.operation==InventoryOperation::SwapHotbar && action.argument>=9)) break;
        InventorySlot targetReference{action.operation==InventoryOperation::SwapHotbar?InventoryArea::Storage:InventoryArea::Offhand,static_cast<uint8_t>(action.operation==InventoryOperation::SwapHotbar?action.argument:0)};
        auto* target=next.slot(targetReference);
        if(!next.accepts(action.slot,target->id) || !next.accepts(targetReference,source->id)) break;
        std::swap(*source,*target);accepted=true;break;
    }
    case InventoryOperation::Drop: {
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
        InventoryInteraction::setCreativeItem(*target,item);accepted=true;break;
    }
    case InventoryOperation::CreativeClone: {
        if (!context.creative) break;
        const auto* source = next.slot(action.slot); if (!source || source->empty()) break;
        InventoryInteraction::setCreativeItem(next.cursor, source->id); accepted = true; break;
    }
    case InventoryOperation::Close:
        next.returnStack(next.cursor);for(auto& stack:next.crafting) next.returnStack(stack);accepted=true;break;
    }
    if(!accepted) return {};
    const bool changed=context.container && !sameContainer(*context.container,*next.container);
    inventory=next.inventory;m_cursor=next.cursor;m_crafting=next.crafting;++m_revision;
    if(context.container) *context.container=*next.container;
    return {true,changed,std::move(next.drops)};
}

std::vector<ItemStack> InventoryTransaction::close(InventoryModel& inventory) {
    Working next{inventory,m_cursor,m_crafting,std::nullopt,false,{}};
    next.returnStack(next.cursor);
    for (auto& stack:next.crafting) next.returnStack(stack);
    inventory=next.inventory;m_cursor=next.cursor;m_crafting=next.crafting;++m_revision;
    return std::move(next.drops);
}
