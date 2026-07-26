#include "world/BlockEntityLogic.h"
#include "game/SurvivalRules.h"

bool tickFurnace(BlockEntity& entity){
    if(entity.type!=BlockEntityType::Furnace) return false;
    bool changed=false;
    const SmeltingRecipe* recipe=entity.input.empty()?nullptr:findSmeltingRecipe(entity.input.id);
    const bool fits=recipe&&(entity.output.empty()||(entity.output.id==recipe->output.id&&
        entity.output.count+recipe->output.count<=getItemProps(entity.output.id).maxStack));
    if(entity.burnRemaining==0&&fits&&!entity.fuel.empty()){const uint16_t burn=fuelTicks(entity.fuel.id);
        if(burn){entity.burnRemaining=entity.burnTotal=burn;if(!--entity.fuel.count)entity.fuel.clear();changed=true;}}
    if(entity.burnRemaining>0){--entity.burnRemaining;changed=true;}
    if(entity.burnRemaining>0&&fits){entity.cookTotal=recipe->cookTicks;if(++entity.cookProgress>=entity.cookTotal){entity.cookProgress=0;
        if(!--entity.input.count)entity.input.clear();
        if(entity.output.empty())entity.output=recipe->output;else entity.output.count+=recipe->output.count;}changed=true;}
    else if(entity.cookProgress){entity.cookProgress=0;changed=true;}
    return changed;
}
