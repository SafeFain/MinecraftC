#pragma once
#include "world/NetherPortal.h"

namespace NetherSessionIntegration {
inline void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
inline void frame(World& world,glm::ivec3 origin,bool alongZ=false,int width=2,int height=3) {
    NetherPortalFrame gate{origin,width,height,alongZ};
    for(int v=-1;v<=height;++v)for(int u=-1;u<=width;++u) {
        const auto p=gate.at(u,v);world.setBlock(p.x,p.y,p.z,(v==-1||v==height||u==-1||u==width)?BlockId::OBSIDIAN:BlockId::AIR);
    }
    check(NetherPortal::ignite(world,origin),"session gate ignition");
}
inline int run() {
    Plugins::content()=Plugins::ContentState{};
    const auto root=std::filesystem::temp_directory_path()/"minecraftc-nether-session";
    std::filesystem::remove_all(root);Config::RENDER_DISTANCE=0;
    {
        GameSession session(root/"saves");
        const auto id=session.createWorld("Native session",7,GameMode::Survival,Difficulty::Normal,true,WorldType::Superflat);
        session.startWorld(id,true,0);drainGeneration(session);
        auto& ow=GameSessionTestAccess::netherFlat(session,DimensionId::Overworld);
        auto& nether=GameSessionTestAccess::netherFlat(session,DimensionId::Nether);
        auto& player=GameSessionTestAccess::player(session);
        nether.setBlock(12,255,12,BlockId::SHROOMLIGHT);nether.setBlock(12,256,12,BlockId::GLOWSTONE);nether.setBlock(12,-1,12,BlockId::GLOWSTONE);
        check(nether.getBlock(12,255,12)==BlockId::SHROOMLIGHT&&nether.getBlock(12,256,12)==BlockId::AIR&&nether.getBlock(12,-1,12)==BlockId::AIR,"native construction height bounds");
        frame(ow,{-8,65,-8});frame(nether,{-1,65,-1},true);
        player.setPosition({-7.5,65.01,-7.5});
        GameSessionTestAccess::netherReady(session);
        check(!GameSessionTestAccess::tickPortals(session,3.95f)&&session.activeDimension()==DimensionId::Overworld,"survival gate waits 80 ticks");
        check(GameSessionTestAccess::tickPortals(session,.05f)&&session.activeDimension()==DimensionId::Nether,"survival gate transfer");
        check(glm::distance(player.getPosition(),glm::dvec3(-.5,65.01,-.5))<.01,"negative scale uses existing native gate");
        GameSessionTestAccess::netherReady(session);
        check(!GameSessionTestAccess::tickPortals(session,4)&&session.activeDimension()==DimensionId::Nether,"portal cooldown held while inside");
        player.setPosition({5.5,65.01,5.5});GameSessionTestAccess::tickPortals(session,2);
        player.configureRules(GameMode::Creative,Difficulty::Normal);player.setPosition({-.5,65.01,-.5});
        GameSessionTestAccess::portalRule(session,false);
        check(!GameSessionTestAccess::tickPortals(session,.05f),"disabled portal rule");
        GameSessionTestAccess::portalRule(session,true);
        check(GameSessionTestAccess::tickPortals(session,.05f)&&session.activeDimension()==DimensionId::Overworld,"creative immediate return");
        GameSessionTestAccess::netherReady(session);
        // Both equal-distance candidates must resolve to the same stable origin.
        frame(nether,{-8,65,8});frame(nether,{7,65,8});
        const auto landing=GameSessionTestAccess::portalLanding(session,DimensionId::Nether,{.5,65.01,8.5});
        check(landing&&*landing==glm::dvec3(-6.5,65.01,8.5),"existing gate stable three-dimensional tie");
        // Charge, spawn, consumption, and safe fallback use the actual session.
        GameSessionTestAccess::prepareActionScene(session,DimensionId::Nether,7);
        player.setPosition({.5,65.01,.5});player.configureRules(GameMode::Survival,Difficulty::Normal);
        const glm::ivec3 anchor{0,66,2};nether.setBlock(anchor.x,anchor.y,anchor.z,respawnAnchorBlock(0));
        player.inventory().slot(0)={ItemId::GLOWSTONE,5,0};player.setSelectedSlot(0);
        for(int i=0;i<4;++i)check(GameSessionTestAccess::anchorUse(session,anchor),"anchor charges");
        check(respawnAnchorCharge(nether.getBlock(0,66,2))==4&&player.inventory().slot(0).count==1,"anchor charge capped and consumes exactly four");
        player.inventory().slot(0).clear();check(GameSessionTestAccess::anchorUse(session,anchor),"anchor sets respawn");
        GameSessionTestAccess::die(session);session.respawn(0);
        check(session.activeDimension()==DimensionId::Nether&&!session.isPlayerDead()&&respawnAnchorCharge(nether.getBlock(0,66,2))==3,"anchor respawn consumes one charge");
        nether.setBlock(0,66,2,BlockId::AIR);GameSessionTestAccess::die(session);session.respawn(0);
        check(session.activeDimension()==DimensionId::Overworld&&!session.isPlayerDead(),"missing anchor falls back to Overworld");
        GameSessionTestAccess::netherReady(session);player.configureRules(GameMode::Creative,Difficulty::Normal);player.setPosition({.5,65.01,.5});
        ow.setBlock(0,66,2,respawnAnchorBlock(1));check(GameSessionTestAccess::anchorUse(session,{0,66,2})&&ow.getBlock(0,66,2)==BlockId::AIR,"Overworld anchor explodes");
        GameSessionTestAccess::prepareActionScene(session,DimensionId::Nether,7);player.configureRules(GameMode::Creative,Difficulty::Normal);
        nether.setBlock(0,65,2,bedBlock(BedPart::Foot,BedDirection::North));
        check(!session.beginSleepAtBed({0,65,2})&&nether.getBlock(0,65,2)==BlockId::AIR,"Nether bed explodes");
        // Only the far edge is within radius16. The indexed origin is in a
        // cold chunk beyond the initial five-chunk landing window.
        std::vector<StreamingInterest> staging;
        for(int i=0;i<9;++i)staging.push_back({i,0,2});
        nether.setAdditionalStreamingInterests(staging);
        staging.push_back({9,0,2});bool bounded=false;
        try{nether.setAdditionalStreamingInterests(staging);}catch(const std::invalid_argument&){bounded=true;}
        check(bounded,"portal staging interests are not bounded");
        nether.setAdditionalStreamingInterests({{-2,-2,2}});
        for(int z=-4;z<=0;++z)for(int x=-4;x<=0;++x) {
            auto* c=nether.getChunk(x,z);
            for(int bz=0;bz<16;++bz)for(int bx=0;bx<16;++bx)for(int y=64;y<=69;++y)c->setBlock(bx,y,bz,y==64?BlockId::OBSIDIAN:BlockId::AIR);
            c->generated=true;c->lifecycle=Chunk::LifecycleState::Renderable;
        }
        nether.update({-20.5,65,-29.5},0);
        for(int x=-37;x<=-15;++x)nether.setBlock(x,64,-30,BlockId::AIR);
        frame(nether,{-36,65,-30},false,21,3);nether.flushModifiedChunks();
        nether.setAdditionalStreamingInterests({});nether.update({2000.5,65,2000.5},0);
        GameSessionTestAccess::restoreNetherWorkers(session);
        std::optional<glm::dvec3> wide;
        for(int attempt=0;attempt<8&&!wide;++attempt) {
            wide=GameSessionTestAccess::portalLanding(session,DimensionId::Nether,{.5,65.01,-29.5});
            if(!wide)drainGeneration(session);
        }
        if(!wide||*wide!=glm::dvec3(-15.5,65.01,-29.5)) {
            if(wide)std::cerr<<"Wide landing "<<wide->x<<','<<wide->y<<','<<wide->z<<'\n';else std::cerr<<"Wide landing not ready\n";
            for(const auto p:nether.savedNetherPortalsNear({.5,65.01,-29.5},37))std::cerr<<"Indexed "<<p.x<<','<<p.y<<','<<p.z<<'\n';
            std::cerr<<"Origin generated "<<nether.isGeneratedAt(-36,-30)<<" live "<<bool(nether.getLoadedBlock(-36,65,-30))<<" block "<<int(nether.getBlock(-36,65,-30))<<'\n';
            int missing=0;
            const auto liveFrame=NetherPortal::find([&](glm::ivec3 q){const auto b=nether.getLoadedBlock(q.x,q.y,q.z);if(!b&&missing++<8)std::cerr<<"Missing "<<q.x<<','<<q.y<<','<<q.z<<'\n';return b.value_or(BlockId::AIR);},{-36,65,-30},false,true);
            const auto rawFrame=NetherPortal::find([&](glm::ivec3 q){return nether.getBlock(q.x,q.y,q.z);},{-36,65,-30},false,true);
            std::cerr<<"Frame live/raw "<<bool(liveFrame)<<'/'<<bool(rawFrame)<<'\n';
            for(int x=-36;x<=-16;++x)std::cerr<<"Boundary "<<x<<" bottom/top "<<int(nether.getBlock(x,64,-30))<<'/'<<int(nether.getBlock(x,68,-30))<<'\n';
        }
        check(wide&&*wide==glm::dvec3(-15.5,65.01,-29.5),"cold wide gate edge was missed by the persistent index");
        drainGeneration(session);session.leaveWorld();
    }
    std::filesystem::remove_all(root);
    std::cout<<"Native Nether session portals and anchors passed\n";return 0;
}
}
