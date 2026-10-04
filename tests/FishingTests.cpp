#include "game/FishingSystem.h"
#include "game/SurvivalRules.h"
#include "game/InventoryModel.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
struct Pond {
    bool loaded=true, water=true, sky=true, rain=false, flow=false, wall=false;
    FishingEnvironment environment() {
        return {[this](glm::ivec3 p) -> std::optional<BlockId> {
            if (!loaded) return std::nullopt;
            if (wall && p.z == -1 && p.y >= 1) return BlockId::STONE;
            if (p.y <= -2) return BlockId::STONE;
            if (p.y <= 0 && water) return flow ? BlockId::FLOWING_WATER_3 : BlockId::WATER;
            return BlockId::AIR;
        }, [this](glm::ivec3) { return sky; }, [this](glm::ivec3) { return rain; }};
    }
};
const glm::dvec3 eye(-.5,2,-.5);
void cast(FishingSystem& fishing,Pond& pond) {
    fishing.use(eye,{0,0,-1},{0,0,0},pond.environment());
}
void advanceTo(FishingSystem& fishing,Pond& pond,FishingPhase phase,float dt=1.0f/60) {
    for (int i=0;i<12000 && fishing.view().phase!=phase;++i)
        fishing.update(dt,eye,pond.environment());
    require(fishing.view().phase==phase,"expected phase reached");
}
}
int main() {
    static_assert(static_cast<uint16_t>(ItemId::ROASTED_GLOWSHROOM)==281);
    static_assert(static_cast<uint16_t>(ItemId::FISHING_ROD)==282);
    require(getItemProps(ItemId::FISHING_ROD).maxDurability==64 &&
        getItemProps(ItemId::FISHING_ROD).maxStack==1,"rod durability and stacking");
    std::array<ItemId,9> grid={ItemId::EMPTY,ItemId::EMPTY,ItemId::STICK,
        ItemId::EMPTY,ItemId::STICK,ItemId::STRING,ItemId::STICK,ItemId::EMPTY,ItemId::STRING};
    const auto* recipe=findCraftingRecipe(grid,3,3);
    require(recipe && recipe->output.id==ItemId::FISHING_ROD,"rod workbench recipe");
    require(!findCraftingRecipe(grid,2,2),"rod needs workbench");
    std::swap(grid[0],grid[2]);std::swap(grid[3],grid[5]);std::swap(grid[6],grid[8]);
    require(findCraftingRecipe(grid,3,3)->output.id==ItemId::FISHING_ROD,"mirrored recipe");
    require(findSmeltingRecipe(ItemId::RAW_COD)->output.id==ItemId::COOKED_COD &&
        findSmeltingRecipe(ItemId::RAW_SALMON)->output.id==ItemId::COOKED_SALMON,"fish cooking");
    require(getItemProps(ItemId::RAW_COD).food==2 && getItemProps(ItemId::RAW_SALMON).saturation==.4f &&
        getItemProps(ItemId::COOKED_COD).food==5 && getItemProps(ItemId::COOKED_SALMON).saturation==9.6f,
        "fish nourishment");
    Pond pond;
    require(FishingSystem::openWater({-1,0,-1},pond.environment()),"negative-coordinate open water");
    pond.flow=true;
    require(!FishingSystem::openWater({-1,0,-1},pond.environment()),"flowing water excludes treasure");
    pond.flow=false;pond.loaded=false;
    require(!FishingSystem::openWater({-1,0,-1},pond.environment()),"unloaded water excludes treasure");
    pond.loaded=true;
    require(FishingSystem::loot(true,.84f,.59f).id==ItemId::RAW_COD &&
        FishingSystem::loot(true,.84f,.60f).id==ItemId::RAW_SALMON,"fish weights");
    const ItemId junk[]={ItemId::STICK,ItemId::STRING,ItemId::BONE,ItemId::ROTTEN_FLESH};
    for(int i=0;i<4;++i) require(FishingSystem::loot(true,.85f,i*.25f).id==junk[i],"junk table");
    const ItemId treasure[]={ItemId::BOW,ItemId::FISHING_ROD,ItemId::EMERALD};
    for(int i=0;i<3;++i) require(FishingSystem::loot(true,.95f,(i+.01f)/3).id==treasure[i],"treasure table");
    require(FishingSystem::loot(false,.89f,0).id==ItemId::RAW_COD &&
        FishingSystem::loot(false,1,1).id==ItemId::ROTTEN_FLESH,"closed water weights and endpoint bounds");
    for(float frame:{1.0f/30,1.0f/60,1.0f/144,.25f}) {
        FishingSystem fishing([]{return 0.0f;});
        cast(fishing,pond);
        fishing.use(eye,{0,0,-1},{0,0,0},pond.environment());
        require(fishing.view().phase==FishingPhase::Flying,"same-frame duplicate use ignored");
        advanceTo(fishing,pond,FishingPhase::Waiting,frame);
        const auto position=fishing.view().position;
        require(position.x<0 && position.z< -1,"cast travels across negative chunk boundaries");
        advanceTo(fishing,pond,FishingPhase::Bite,frame);
        require(fishing.view().biteSeconds>.7f,"one-second bite window");
        fishing.update(0,eye,pond.environment());
        require(fishing.view().phase==FishingPhase::Bite,"paused simulation freezes");
        fishing.use(eye,{0,0,-1},{0,0,0},pond.environment());
        auto events=fishing.takeEvents();
        require(std::count_if(events.begin(),events.end(),[](const auto& e){return !e.catchItem.empty();})==1,
            "exactly one catch");
        require(events.back().wear==1 && events.back().catchItem.id==ItemId::RAW_COD,"catch durability");
        fishing.use(eye,{0,0,-1},{0,0,0},pond.environment());
        require(fishing.takeEvents().empty(),"repeated input cannot duplicate catch");
    }
    FishingSystem missed([]{return 0.0f;});cast(missed,pond);
    advanceTo(missed,pond,FishingPhase::Bite);
    missed.update(1.1f,eye,pond.environment());
    require(missed.view().phase==FishingPhase::Waiting,"missed bite restarts waiting");
    missed.use(eye,{0,0,-1},{0,0,0},pond.environment());
    require(missed.takeEvents().back().wear==0,"empty reel has no wear");
    for(int kind=0;kind<4;++kind) {
        FishingSystem canceled([]{return 0.0f;});cast(canceled,pond);
        advanceTo(canceled,pond,FishingPhase::Waiting);
        if(kind==0)pond.water=false;
        if(kind==1)pond.loaded=false;
        if(kind==2)canceled.cancel();
        canceled.update(.02f,kind==3 ? eye+glm::dvec3(100,0,0) : eye,pond.environment());
        require(!canceled.view().active(),"water loss/unload/cancel/range cancellation");
        pond.water=true;pond.loaded=true;
    }
    pond.wall=true;
    FishingSystem stuck;cast(stuck,pond);advanceTo(stuck,pond,FishingPhase::Stuck);
    stuck.update(.3f,eye,pond.environment());stuck.use(eye,{0,0,-1},{0,0,0},pond.environment());
    require(stuck.takeEvents().back().wear==2,"stuck reel costs two durability");
    pond.wall=false;
    float waitTimes[3]{};
    for(int i=0;i<3;++i) {
        pond.sky=i!=2;pond.rain=i==1;
        FishingSystem f([]{return 0.0f;});cast(f,pond);advanceTo(f,pond,FishingPhase::Waiting);
        while(f.view().phase==FishingPhase::Waiting) { f.update(.01f,eye,pond.environment());waitTimes[i]+=.01f; }
    }
    require(waitTimes[1]<waitTimes[0] && waitTimes[2]>waitTimes[0]*1.9f,"rain and covered waiting rates");
    pond.sky=true;pond.rain=false;pond.flow=true;
    FishingSystem flow;cast(flow,pond);advanceTo(flow,pond,FishingPhase::Waiting);
    require(std::abs(flow.view().position.y-fluidSurfaceHeight(BlockId::FLOWING_WATER_3))<.03,
        "bobber follows actual flowing water surface");
    FishingEnvironment lava=pond.environment();lava.block=[](glm::ivec3 p)->std::optional<BlockId>{
        return p.y<=0 ? BlockId::LAVA : BlockId::AIR;};
    FishingSystem burned;burned.use(eye,{0,0,-1},{0,0,0},lava);burned.update(1,eye,lava);
    require(!burned.view().active(),"lava cannot be fished");
    std::cout << "Fishing simulation and survival rules passed\n";
}
