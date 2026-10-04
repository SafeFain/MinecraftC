#pragma once
#include "plugins/PluginManager.h"
#include <stdexcept>

namespace PluginSessionIntegration {
inline void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
inline std::string readBytesForPluginTest(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
inline int run(const std::filesystem::path& assets) {
    const auto root=std::filesystem::temp_directory_path()/"minecraftc-plugin-session";
    std::filesystem::remove_all(root);std::filesystem::create_directories(root);
    {std::ofstream config(root/"plugins.json");config<<"{\"official_content\":true}";}
    RuntimePaths paths{assets,root};Plugins::PluginManager plugins(paths);plugins.initialize();
    const auto block=Plugins::resolveBlock("official_content:crystal_block");
    const auto item=Plugins::resolveItem("official_content:crystal_block");
    const int distance=Config::RENDER_DISTANCE;Config::RENDER_DISTANCE=0;
    {
        GameSession session(root/"saves");EntityAiTestRenderer renderer;
        const auto id=session.createWorld("Plugin session",123,GameMode::Survival,Difficulty::Normal,true,WorldType::Superflat);
        session.startWorld(id,true,0);
        RuntimeClock clock;bool ready=false;
        for(int i=0;i<4000&&!ready;++i){ready=session.advanceLoading(&renderer,clock.now());if(!ready)std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        check(ready,"modded world reaches normal loading gate");
        check(session.pluginGiveItem(static_cast<uint16_t>(item),4),"session host grants registered content");
        auto& player=GameSessionTestAccess::player(session);auto& world=GameSessionTestAccess::world(session);
        const int floor=Config::WORLD_MIN_Y+4;player.setPosition({.5,floor+.01,.5});player.setMouseLocked(true);player.setSelectedSlot(0);
        const glm::ivec3 target{0,floor+1,3},placement{0,floor+1,2};world.setBlock(target.x,target.y,target.z,BlockId::STONE);
        session.handleMouseButton(MouseButton::Right,ButtonAction::Press);
        check(world.getBlock(placement.x,placement.y,placement.z)==block&&player.inventory().slot(0).count==3,"registered block placed and inventory debited");
        world.setBlock(placement.x,placement.y,placement.z,BlockId::AIR);
        auto original=Plugins::dispatcher();Plugins::dispatcher()=[](MC_Event& e){if(e.kind==MC_PLACE_PRE||e.kind==MC_BREAK_PRE||e.kind==MC_DAMAGE_PRE)e.cancelled=1;};
        session.handleMouseButton(MouseButton::Right,ButtonAction::Press);
        check(world.getBlock(placement.x,placement.y,placement.z)==BlockId::AIR&&player.inventory().slot(0).count==3,"cancelled placement leaves world/inventory unchanged");
        player.inventory().slot(1)={ItemId::IRON_PICKAXE,1,0};player.setSelectedSlot(1);
        session.handleMouseButton(MouseButton::Left,ButtonAction::Press);player.update(2);
        check(world.getBlock(target.x,target.y,target.z)==BlockId::STONE&&player.inventory().slot(1).damage==0,"cancelled mining does not remove block or wear tool");
        const float health=player.survivalStats().health();check(player.takeDamage(5).appliedDamage==0&&player.survivalStats().health()==health,"cancelled damage has no survival effects");
        Plugins::dispatcher()=original;session.handleMouseButton(MouseButton::Left,ButtonAction::Release);
        player.setPosition({.5,floor+.01,.5});player.setSelectedSlot(0);session.handleMouseButton(MouseButton::Right,ButtonAction::Press);
        check(world.getBlock(placement.x,placement.y,placement.z)==block,"registered edit exists before saving");
        bool saveFailed=false;session.saveNow([&]{saveFailed=true;});check(!saveFailed,"modded session saves through normal pipeline");
        const auto worldPath=root/"saves"/id;drainGeneration(session);session.leaveWorld();
        session.startWorld(id,false,clock.now());ready=false;
        for(int i=0;i<4000&&!ready;++i){ready=session.advanceLoading(&renderer,clock.now());if(!ready)std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        check(ready,"modded session reloads through normal pipeline");
        check(session.inventory().slot(0).id==item&&session.inventory().slot(0).count==2,"modded inventory reloads");
        check(session.worldState().getBlock(placement.x,placement.y,placement.z)==block,"modded world edit reloads");
        drainGeneration(session);session.leaveWorld();
        const auto saved=readBytesForPluginTest(worldPath/"level.bin");
        auto requirements=Plugins::content().requirements;Plugins::content().requirements.clear();bool refused=false;
        try{session.startWorld(id,false,clock.now());}catch(const std::exception&){refused=true;}
        check(refused&&readBytesForPluginTest(worldPath/"level.bin")==saved,"missing plugin refuses session without rewrite");
        Plugins::content().requirements=std::move(requirements);
    }
    plugins.shutdown();Config::RENDER_DISTANCE=distance;std::filesystem::remove_all(root);
    std::cout<<"Plugin game session integration passed\n";return 0;
}
}
