#include "game/InventoryInteraction.h"
#include <algorithm>
#include <utility>
namespace InventoryInteraction {
void click(ItemStack& cursor, ItemStack& slot, bool right) {
    if (cursor.empty()) {
        if (slot.empty()) return;
        if (right && slot.count > 1) { const uint8_t n=(slot.count+1)/2;
            cursor=slot.withCount(n); slot.count-=n; }
        else std::swap(cursor,slot);
    } else if (slot.empty()) {
        if (right) { slot=cursor.withCount(1); if(!--cursor.count)cursor.clear(); }
        else std::swap(cursor,slot);
    } else if (slot.sameItem(cursor)) {
        const int n=std::min<int>(right?1:cursor.count,getItemProps(slot.id).maxStack-slot.count);
        slot.count+=n; cursor.count-=n; if(!cursor.count)cursor.clear();
    } else if (!right) std::swap(cursor,slot);
}
uint32_t transfer(ItemStack& source,const std::vector<ItemStack*>& targets) {
    if(source.empty()) return 0;
    const uint8_t original=source.count;
    const uint8_t maximum=getItemProps(source.id).maxStack;
    for(auto* target:targets){if(!target||target==&source||!target->sameItem(source)||target->count>=maximum)continue;
        const uint8_t n=std::min<int>(source.count,maximum-target->count);target->count+=n;source.count-=n;if(!source.count){source.clear();return original;}}
    for(auto* target:targets){if(!target||target==&source||!target->empty())continue;const uint8_t n=std::min(source.count,maximum);
        *target=source.withCount(n);source.count-=n;if(!source.count){source.clear();return original;}}
    return original-source.count;
}
void gather(ItemStack& cursor,const std::vector<ItemStack*>& sources){if(cursor.empty())return;const uint8_t maximum=getItemProps(cursor.id).maxStack;
    for(auto* source:sources){if(!source||!source->sameItem(cursor))continue;const uint8_t n=std::min<int>(source->count,maximum-cursor.count);
        cursor.count+=n;source->count-=n;if(!source->count)source->clear();if(cursor.count==maximum)return;}}
void distribute(ItemStack& cursor,const std::vector<ItemStack*>& targets,bool oneEach){
    if(cursor.empty()||targets.empty()) return;
    const uint8_t maximum=getItemProps(cursor.id).maxStack;
    const int share=oneEach?1:std::max(1,static_cast<int>(cursor.count)/static_cast<int>(targets.size()));
    for(auto* target:targets){if(!target||(!target->empty()&&(!target->sameItem(cursor))))continue;
        const int room=target->empty()?maximum:maximum-target->count;const int n=std::min<int>({share,room,cursor.count});if(n<=0)continue;
        if(target->empty())*target=cursor.withCount(static_cast<uint8_t>(n));else target->count+=n;
        cursor.count-=n;if(!cursor.count){cursor.clear();return;}}
}
void setCreativeItem(ItemStack& slot,ItemId item){
    if(!isValidItemId(item)||item==ItemId::EMPTY){slot.clear();return;}
    slot={item,getItemProps(item).maxStack,0};
}
ItemStack takeOne(ItemStack& slot){
    if(slot.empty())return {};
    const ItemStack result=slot.withCount(1);
    if(--slot.count==0)slot.clear();
    return result;
}
}
