#include "game/SurvivalBlockLogic.h"

BlockId nextFarmlandState(BlockId farmland,BlockId above,bool water,uint64_t random){
    if(!isFarmland(farmland)||random%20!=0)return farmland;
    const uint8_t moisture=farmlandMoisture(farmland);
    if(water)return farmlandForMoisture(7);
    if(moisture>0)return farmlandForMoisture(moisture-1);
    return above>=BlockId::WHEAT_0&&above<=BlockId::WHEAT_7?farmland:BlockId::DIRT;
}
BlockId nextCropState(BlockId crop,BlockId soil,uint64_t random){
    if(crop<BlockId::WHEAT_0||crop>=BlockId::WHEAT_7||!isFarmland(soil))return crop;
    const uint64_t interval=farmlandMoisture(soil)>0?30:120;
    return random%interval==0?static_cast<BlockId>(static_cast<uint8_t>(crop)+1):crop;
}
