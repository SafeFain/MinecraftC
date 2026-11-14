#pragma once
#include "world/NetherPortal.h"
#include "plugins/ContentQueries.h"
#include "plugins/ContentMutation.h"

namespace NetherSessionIntegration {
inline void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
inline void frame(World& world,glm::ivec3 origin,bool alongZ=false,int width=2,int height=3) {
    NetherPortalFrame gate{origin,width,height,alongZ};
    for(int v=-1;v<=height;++v)for(int u=-1;u<=width;++u) {
        const auto p=gate.at(u,v);world.setBlock(p.x,p.y,p.z,(v==-1||v==height||u==-1||u==width)?BlockId::OBSIDIAN:BlockId::AIR);
        auto* chunk=world.getChunk(World::worldToChunkX(p.x),World::worldToChunkZ(p.z));
        chunk->generated=true;chunk->lifecycle=Chunk::LifecycleState::Renderable;
    }
    check(NetherPortal::ignite(world,origin),"session gate ignition");
}
inline int run() {
    Plugins::mutableContentState()=Plugins::ContentState{};
    const auto root=std::filesystem::temp_directory_path()/"minecraftc-nether-session";
    std::filesystem::remove_all(root);Config::RENDER_DISTANCE=0;
    {
        ScenarioSession session(root/"saves");
        const auto id=session.createWorld("Native session",7,GameMode::Survival,Difficulty::Normal,true,WorldType::Superflat);
        session.startWorld(id,true,0);drainGeneration(session);
        auto& ow=SessionScenario::netherFlat(session,DimensionId::Overworld);
        auto& nether=SessionScenario::netherFlat(session,DimensionId::Nether);
        auto& player=SessionScenario::player(session);
        nether.setBlock(12,255,12,BlockId::SHROOMLIGHT);nether.setBlock(12,256,12,BlockId::GLOWSTONE);nether.setBlock(12,-1,12,BlockId::GLOWSTONE);
        check(nether.getBlock(12,255,12)==BlockId::SHROOMLIGHT&&nether.getBlock(12,256,12)==BlockId::AIR&&nether.getBlock(12,-1,12)==BlockId::AIR,"native construction height bounds");
        frame(ow,{-8,65,-8});frame(nether,{-1,65,-1},true);
        player.experience().addPoints(1537);
        player.setPosition({-7.5,65.01,-7.5});
        SessionScenario::netherReady(session);
        check(!SessionScenario::tickPortals(session,3.95f)&&session.activeDimension()==DimensionId::Overworld,"survival gate waits 80 ticks");
        check(SessionScenario::tickPortals(session,.05f)&&session.activeDimension()==DimensionId::Nether,"survival gate transfer");
        check(player.experience().level()==31&&player.experience().points()==30,"experience survives native dimension entry");
        check(glm::distance(player.getPosition(),glm::dvec3(-.5,65.01,-.5))<.01,"negative scale uses existing native gate");
        SessionScenario::netherReady(session);
        check(!SessionScenario::tickPortals(session,4)&&session.activeDimension()==DimensionId::Nether,"portal cooldown held while inside");
        player.setPosition({5.5,65.01,5.5});SessionScenario::tickPortals(session,2);
        player.configureRules(GameMode::Creative,Difficulty::Normal);player.setPosition({-.5,65.01,-.5});
        SessionScenario::portalRule(session,false);
        check(!SessionScenario::tickPortals(session,.05f),"disabled portal rule");
        SessionScenario::portalRule(session,true);
        check(SessionScenario::tickPortals(session,.05f)&&session.activeDimension()==DimensionId::Overworld,"creative immediate return");
        SessionScenario::netherReady(session);
        check(player.experience().level()==31&&player.experience().points()==30,"experience survives creative mode and portal return");player.experience().clear();
        // Both equal-distance candidates must resolve to the same stable origin.
        frame(nether,{-8,65,8});frame(nether,{7,65,8});
        const auto landing=SessionScenario::portalLanding(session,DimensionId::Nether,{.5,65.01,8.5});
        check(landing&&*landing==glm::dvec3(-6.5,65.01,8.5),"existing gate stable three-dimensional tie");
        // Charge, spawn, consumption, and safe fallback use the actual session.
        SessionScenario::prepareActionScene(session,DimensionId::Nether,7);
        player.setPosition({.5,65.01,.5});player.configureRules(GameMode::Survival,Difficulty::Normal);
        const glm::ivec3 anchor{0,66,2};nether.setBlock(anchor.x,anchor.y,anchor.z,respawnAnchorBlock(0));
        player.inventory().slot(0)={ItemId::GLOWSTONE,5,0};player.setSelectedSlot(0);
        SessionScenario::restTime(session,false);
        int restCallbacks=0;
        session.setBedCallback([&](const glm::ivec3& p){++restCallbacks;session.interactRestBlock(p);});
        player.setMouseLocked(true);
        player.handleMouseButton(MouseButton::Right,ButtonAction::Press);
        player.handleMouseButton(MouseButton::Right,ButtonAction::Release);
        check(restCallbacks==1&&respawnAnchorCharge(nether.getBlock(0,66,2))==1,"daytime player use must reach native anchor charging");
        for(int i=1;i<4;++i)check(session.interactRestBlock(anchor)==GameSession::RestInteraction::Handled,"anchor charges");
        check(respawnAnchorCharge(nether.getBlock(0,66,2))==4&&player.inventory().slot(0).count==1,"anchor charge capped and consumes exactly four");
        player.inventory().slot(0).clear();session.interactRestBlock(anchor);
        check(session.metadata().dimensions.at(2).anchorSpawn==anchor&&!session.metadata().dimensions.at(2).bedSpawn,"daytime anchor sets anchor, not bed spawn");
        SessionScenario::die(session);session.respawn(0);
        check(session.activeDimension()==DimensionId::Nether&&!session.isPlayerDead()&&respawnAnchorCharge(nether.getBlock(0,66,2))==3,"anchor respawn consumes one charge");
        nether.setBlock(0,66,2,BlockId::AIR);SessionScenario::die(session);session.respawn(0);
        check(session.activeDimension()==DimensionId::Overworld&&!session.isPlayerDead(),"missing anchor falls back to Overworld");
        SessionScenario::netherReady(session);player.configureRules(GameMode::Creative,Difficulty::Normal);player.setPosition({.5,65.01,.5});
        const auto oldBed=session.metadata().bedSpawn;
        for(bool night:{false,true}) {
            SessionScenario::prepareActionScene(session,DimensionId::Overworld,7);player.configureRules(GameMode::Creative,Difficulty::Normal);
            SessionScenario::restTime(session,night);SessionScenario::anchorSpawn(session,anchor);
            ow.setBlock(0,66,2,respawnAnchorBlock(1));
            check(session.interactRestBlock({0,66,2})==GameSession::RestInteraction::Handled&&ow.getBlock(0,66,2)==BlockId::AIR,"Overworld anchor explodes regardless of time");
            check(session.metadata().dimensions.at(2).anchorSpawn==anchor&&session.metadata().bedSpawn==oldBed,"anchor use must not register a bed or clear another anchor");
            SessionScenario::prepareActionScene(session,DimensionId::Nether,7);player.configureRules(GameMode::Creative,Difficulty::Normal);
            SessionScenario::restTime(session,night);
            nether.setBlock(0,65,2,bedBlock(BedPart::Foot,BedDirection::North));
            check(session.interactRestBlock({0,65,2})==GameSession::RestInteraction::Handled&&nether.getBlock(0,65,2)==BlockId::AIR,"Nether bed explodes regardless of time");
        }
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
        SessionScenario::restoreNetherWorkers(session);
        std::optional<glm::dvec3> wide;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while(!wide&&std::chrono::steady_clock::now()<deadline) {
            wide=SessionScenario::portalLanding(session,DimensionId::Nether,{.5,65.01,-29.5});
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
    {
        ScenarioSession session(root/"cold-saves");
        const auto id=session.createWorld("Pending anchor",7,GameMode::Survival,Difficulty::Normal,true,WorldType::Superflat);
        // This edited save has a known Nether landing in the synthetic apron.
        SaveStore saved(root/"cold-saves"/id);auto metadata=saved.loadMetadata();
        metadata.dimensions[2].positioned=true;metadata.dimensions[2].playerPosition={.5,65.01,.5};saved.saveMetadata(metadata);
        session.startWorld(id,true,0);drainGeneration(session);
        SessionScenario::netherFlat(session,DimensionId::Overworld);
        auto& nether=SessionScenario::netherFlat(session,DimensionId::Nether);
        const glm::ivec3 anchor{15,66,2};
        nether.setBlock(anchor.x,anchor.y,anchor.z,respawnAnchorBlock(4));
        SessionScenario::anchorSpawn(session,anchor);
        auto* chunk=nether.getChunk(0,0);chunk->generated=false;
        SessionScenario::netherReady(session);SessionScenario::die(session);
        session.respawn(0);
        check(session.isPlayerDead()&&session.worldLoadingPending()&&session.metadata().dimensions.at(2).anchorSpawn==anchor,"cold anchor must remain pending without losing its record");
        check(session.loadingSnapshot().reason==GameSession::LoadingReason::Dimension,"pending anchor must present destination loading");
        check(respawnAnchorCharge(chunk->getBlock(15,66,2))==4,"pending respawn consumed a charge");
        chunk->generated=true;
        session.advanceLoading(nullptr,0);
        check(!session.isPlayerDead()&&session.activeDimension()==DimensionId::Nether&&respawnAnchorCharge(nether.getBlock(15,66,2))==3,"loading must automatically finish anchor respawn exactly once");
        SessionScenario::prepareActionScene(session,DimensionId::Overworld,7);
        auto& guest=SessionScenario::anchorGuest(session,anchor);
        // Every loaded candidate is obstructed. Only the cold neighboring chunk
        // can supply a landing, so missing neighbors cannot mean invalid anchor.
        for(int z=0;z<=4;++z)for(int x=13;x<=15;++x)for(int y=65;y<=68;++y)
            if(glm::ivec3(x,y,z)!=anchor)nether.setBlock(x,y,z,BlockId::OBSIDIAN);
        auto* neighbor=nether.getChunk(1,0);neighbor->generated=false;
        check(!SessionScenario::respawnGuest(session,guest)&&guest.pendingRespawn&&guest.profile.netherAnchor==anchor&&guest.dead,"LAN respawn must retain an anchor while neighboring terrain is cold");
        check(respawnAnchorCharge(nether.getBlock(15,66,2))==3,"pending LAN respawn consumed a charge");
        neighbor->generated=true;
        SessionScenario::tickLanPlayers(session,0);
        check(!guest.dead&&!guest.pendingRespawn&&guest.profile.dimension==DimensionId::Nether&&respawnAnchorCharge(nether.getBlock(15,66,2))==2,"LAN anchor respawn did not retry automatically");
        auto& overworld=SessionScenario::netherFlat(session,DimensionId::Overworld);
        const glm::ivec3 bed{0,65,8},head=bed+bedDirectionOffset(BedDirection::North);
        overworld.setBlock(bed.x,bed.y,bed.z,bedBlock(BedPart::Foot,BedDirection::North));
        overworld.setBlock(head.x,head.y,head.z,bedBlock(BedPart::Head,BedDirection::North));
        SessionScenario::restTime(session,false);
        check(session.interactRestBlock(bed)==GameSession::RestInteraction::SpawnSet&&session.metadata().bedSpawn==bed&&!session.metadata().dimensions.at(2).anchorSpawn,"ordinary daytime bed must still set spawn and replace the anchor");
        SessionScenario::restTime(session,true);
        check(session.interactRestBlock(bed)==GameSession::RestInteraction::Sleeping&&session.isSleeping(),"ordinary nighttime bed must still begin sleep");
        session.leaveWorld();
    }
    // Invalid saved anchors are independent fault inputs. Do not reload the live
    // cold-neighbor scenario or require travel to its now-obstructed old landing.
    for(bool heightLimit:{true,false}) {
        const auto saves=root/(heightLimit?"invalid-height":"invalid-border");
        ScenarioSession session(saves);
        const auto id=session.createWorld("Invalid saved anchor",7,GameMode::Survival,Difficulty::Normal,true,WorldType::Superflat);
        const glm::ivec3 highAnchor{5,255,5};
        const glm::ivec3 borderAnchor{29999999,66,2};
        const auto anchor=heightLimit?highAnchor:borderAnchor;
        SaveStore saved(saves/id);auto metadata=saved.loadMetadata();metadata.dimensions[2].anchorSpawn=anchor;saved.saveMetadata(metadata);
        session.startWorld(id,true,0);drainGeneration(session);
        SessionScenario::netherFlat(session,DimensionId::Overworld);
        auto& nether=SessionScenario::netherFlat(session,DimensionId::Nether);
        if(heightLimit) {
            for(int z=3;z<=7;++z)for(int x=3;x<=7;++x)for(int y=253;y<=254;++y)nether.setBlock(x,y,z,BlockId::OBSIDIAN);
            nether.setBlock(5,255,5,respawnAnchorBlock(4));
        } else {
            auto* border=nether.getChunk(World::worldToChunkX(borderAnchor.x),0);
            for(int z=0;z<16;++z)for(int x=0;x<16;++x)for(int y=64;y<=68;++y)
                border->setBlock(x,y,z,BlockId::OBSIDIAN);
            border->setBlock(15,66,2,respawnAnchorBlock(4));border->generated=true;border->lifecycle=Chunk::LifecycleState::Renderable;
        }
        SessionScenario::netherReady(session);SessionScenario::die(session);session.respawn(0);
        check(!session.metadata().dimensions.at(2).anchorSpawn&&!session.isPlayerDead()&&
            respawnAnchorCharge(nether.getBlock(anchor.x,anchor.y,anchor.z))==4,
            heightLimit?"anchor landing must not put a player's head above the Nether build limit":
                        "out-of-border landing cells must not be treated as safe or perpetually pending");
        session.leaveWorld();
    }
    std::filesystem::remove_all(root);
    std::cout<<"Native Nether session portals and anchors passed\n";return 0;
}
}
