#pragma once
#include "EntityAiScenarios.h"
#include <cmath>

namespace EntityAiScenarios {
inline void check(bool condition,const char* message) {
    if (!condition) { std::cerr<<"FAILED AI: "<<message<<'\n'; std::exit(1); }
}
inline void advance(Scene& s,double seconds,int fps=60,bool targetable=false,uint64_t tick=6000) {
    for(int i=0;i<static_cast<int>(std::round(seconds*fps));++i) {
        s.step(1.0f/fps,targetable,tick);
        check(s.mobs.aiStats().pathNodes<=Config::AI_PATH_NODES_PER_FRAME,"global path budget exceeded");
        check(s.mobs.aiStats().activeSearches<=Config::AI_ACTIVE_SEARCH_LIMIT,"too many live search contexts");
        check(s.mobs.aiStats().poiBlocks<=Config::AI_POI_BLOCKS_PER_FRAME,"POI scan budget exceeded");
    }
}
inline uint64_t worker(Scene& s,glm::dvec3 position,glm::ivec3 workstation) {
    s.block(workstation.x,workstation.y,workstation.z,BlockId::COMPOSTER);
    s.add(EntityType::Villager,position);
    auto saved=s.mobs.saveEntities();
    saved.back().villager.hasWorkstation=true;
    saved.back().villager.claimedWorkstation=workstation;
    saved.back().villager.profession=VillagerProfession::Farmer;
    s.mobs.clear(); s.mobs.loadEntities(saved);
    return s.mobs.entities().back().id;
}
inline int integration(const std::filesystem::path& assets) {
    {
        Scene s(assets);
        const auto id=s.add(EntityType::Zombie,{.5,1,.5});
        s.player.setPosition({8.5,1,.5});
        advance(s,.3,60,true);
        const auto* mob=s.mobs.entityById(id);
        check(mob && mob->ai.hasTarget && mob->ai.targetVisible,"visible player was not acquired");
        const auto seen=mob->ai.lastSeen;
        for(int z=-48;z<64;++z) for(int y=1;y<=4;++y) s.block(4,y,z,BlockId::STONE);
        s.player.setPosition({8.5,1,5.5});
        advance(s,.3,60,true);
        mob=s.mobs.entityById(id);
        check(mob && mob->ai.hasTarget && !mob->ai.targetVisible && mob->ai.lastSeen==seen,
              "occluded target leaked its new position");
        advance(s,3.1,60,true);
        check(!s.mobs.entityById(id)->ai.hasTarget,"last-seen memory failed to expire");
    }
    {
        Scene s(assets);
        const auto id=s.add(EntityType::Zombie,{.5,1,.5});
        s.player.setPosition({1.65,1,.5});
        advance(s,.15,60,true);
        check(s.mobs.entityById(id)->attackPending,"close zombie did not begin attack animation");
        const float health=s.player.survivalStats().health();
        s.step(1.0f/60,false);
        check(!s.mobs.entityById(id)->attackPending && !s.mobs.entityById(id)->ai.hasTarget,
              "game mode switch did not immediately cancel attack");
        advance(s,1,60,false);
        check(s.player.survivalStats().health()==health,"cancelled attack still damaged player");
    }
    {
        Scene s(assets);
        const auto id=s.add(EntityType::Zombie,{.5,1,.5});
        s.player.setPosition({1.65,1,.5});
        advance(s,.15,60,true);
        const float health=s.player.survivalStats().health();
        s.block(1,1,0,BlockId::STONE); s.block(1,2,0,BlockId::STONE);
        advance(s,1,60,true);
        check(s.player.survivalStats().health()==health,"attack impact passed through a new wall");
        check(s.mobs.entityById(id)!=nullptr,"attacker disappeared unexpectedly");
    }
    {
        Scene s(assets);
        const auto id=s.add(EntityType::Cow,{.5,1,.5});
        advance(s,.2);
        MeleeAttackRequest hit; hit.damage=1; hit.reach=6;
        const auto result=s.mobs.attackRay({-2,1.6,.5},{1,0,0},hit);
        check(result.primaryDamaged,"test hit did not damage cow");
        advance(s,.2);
        check(s.mobs.entityById(id)->ai.behavior==EntityBehavior::Flee,"injured animal did not flee");
        const double start=s.mobs.entityById(id)->position.x;
        advance(s,1.5);
        check(s.mobs.entityById(id)->position.x>start+.5,"animal did not move away from attacker");
        advance(s,2);
        check(s.mobs.entityById(id)->ai.behavior!=EntityBehavior::Flee,"panic did not expire");
        check(s.mobs.aiStats().sightQueries==0,"idle animals still query combat sight");
    }
    {
        Scene s(assets);
        const auto id=s.add(EntityType::Skeleton,{.5,1,.5});
        s.player.setPosition({3.5,1,.5});
        advance(s,3,60,true);
        const auto* skeleton=s.mobs.entityById(id);
        check(skeleton && glm::distance(skeleton->position,s.player.getPosition())>4.5,
              "skeleton did not retreat from a nearby target");
    }
    {
        Scene s(assets);
        const auto id=worker(s,{.5,1,.5},{7,1,0});
        for(int z=-2;z<=2;++z) for(int y=1;y<=3;++y) s.block(3,y,z,BlockId::STONE);
        advance(s,20,60,false,3000);
        const auto* villager=s.mobs.entityById(id);
        check(villager && villager->position.x>5.5,"villager did not follow detour to workplace");
        check(villager->villager.restocksToday==1,"arrived worker did not restock");
        const auto saved=s.mobs.saveEntities();
        s.mobs.clear(); s.mobs.loadEntities(saved);
        check(s.mobs.entities()[0].ai.path.empty() && !s.mobs.entities()[0].ai.initialized,
              "runtime navigation leaked into save data");
    }
    {
        Scene s(assets);
        const auto villager=s.add(EntityType::Villager,{.5,1,.5});
        s.add(EntityType::Zombie,{5.5,1,.5});
        advance(s,.3);
        check(s.mobs.entityById(villager)->ai.behavior==EntityBehavior::Flee,"villager ignored visible hostile");
        // Reload just the villager, simulating removal/unload of the hostile.
        auto saved=s.mobs.saveEntities(); saved.resize(1);
        s.mobs.clear(); s.mobs.loadEntities(saved);
        advance(s,.2);
        check(s.mobs.entities()[0].ai.behavior!=EntityBehavior::Flee,"reload retained stale threat target");
    }
    {
        Scene s(assets);
        const auto zombie=s.add(EntityType::Zombie,{.5,1,.5});
        const auto villager=s.add(EntityType::Villager,{5.5,1,.5});
        advance(s,.2);
        check(s.mobs.entityById(zombie)->ai.targetId==villager,"zombie did not select visible villager");
        MeleeAttackRequest hit; hit.damage=100; hit.reach=6;
        const auto position=s.mobs.entityById(villager)->position;
        s.mobs.attackRay(position+glm::dvec3(0,1,2),{0,0,-1},hit);
        s.step();
        check(!s.mobs.entityById(zombie)->ai.hasTarget,"dead target remained active for throttled AI");
    }
    {
        std::vector<glm::dvec3> final;
        for(int fps:{30,60,120}) {
            Scene s(assets);
            const auto id=worker(s,{.5,1,.5},{6,2,0});
            // A full-width one-block platform forces a real jump.
            for(int x=2;x<10;++x) for(int z=-4;z<=4;++z) s.block(x,1,z,BlockId::STONE);
            double maxRise=0;
            for(int i=0;i<fps*12;++i) {
                const double y=s.mobs.entityById(id)->position.y;
                s.step(1.0f/fps,false,3000);
                maxRise=std::max(maxRise,s.mobs.entityById(id)->position.y-y);
            }
            const auto* mob=s.mobs.entityById(id);
            check(mob->position.x>4.5 && mob->position.y>1.9,"worker failed one-block platform jump");
            check(maxRise<.5,"one-block traversal teleported instead of jumping");
            final.push_back(mob->position);
        }
        check(glm::distance(final[0],final[1])<.3 && glm::distance(final[1],final[2])<.3,
              "navigation arrival depends on render frame rate");
    }
    {
        Scene s(assets);
        const auto id=worker(s,{.5,1,.5},{8,1,0});
        advance(s,1,60,false,3000);
        for(int z=-1;z<=1;++z) for(int y=1;y<=3;++y) s.block(4,y,z,BlockId::STONE);
        advance(s,20,60,false,3000);
        check(s.mobs.entityById(id)->position.x>6.5,"new wall failed to invalidate and replan route");
    }
    {
        Scene s(assets);
        s.block(6,1,0,BlockId::COMPOSTER);
        s.add(EntityType::Villager,{.5,1,.5});
        s.add(EntityType::Villager,{.5,1,3.5});
        advance(s,12,60,false,3000);
        size_t claims=0;
        for(const auto& entity:s.mobs.entities()) if(entity.villager.hasWorkstation) ++claims;
        check(claims==1,"POI claim was lost or duplicated under incremental search");
    }
    {
        Scene s(assets);
        for(int i=0;i<100;++i) s.add(EntityType::Cow,{-18.5+4*(i%10),1,-18.5+4*(i/10)});
        advance(s,5);
        check(s.mobs.entities().size()==100,"stress test reduced mob count");
        size_t progressed=0;
        for(const auto& entity:s.mobs.entities())
            if(entity.ai.waypoint>0 || entity.ai.sequence>1) ++progressed;
        check(progressed>=90,"navigation queue starved ordinary mobs");
    }
    {
        Scene s(assets);
        s.block(4,1,0,bedBlock(BedPart::Foot,BedDirection::East));
        s.block(5,1,0,bedBlock(BedPart::Head,BedDirection::East));
        const auto id=worker(s,{.5,1,.5},{0,1,4});
        (void)id;
        auto saved=s.mobs.saveEntities();
        saved[0].villager.hasBed=true; saved[0].villager.claimedBed={4,1,0};
        s.mobs.clear(); s.mobs.loadEntities(saved);
        advance(s,8,60,false,14000);
        check(s.mobs.entities()[0].sleeping,"villager did not reach and use bed at night");
        for(int i=0;i<30;++i)
            s.mobs.update(s.player,1.0f/60,true,false,false,false,true,true,3000);
        check(!s.mobs.entities()[0].sleeping &&
              s.mobs.entities()[0].ai.behavior==EntityBehavior::Work,
              "daytime thunderstorm incorrectly sent villager to sleep");
    }
    {
        Scene s(assets);
        const auto id=worker(s,{.5,1,.5},{4,1,0});
        // The closest approach is sealed; another side remains reachable.
        for(int y=1;y<=4;++y) {
            s.block(2,y,0,BlockId::STONE);
            s.block(3,y,-1,BlockId::STONE); s.block(3,y,1,BlockId::STONE);
        }
        advance(s,15,60,false,3000);
        check(s.mobs.entityById(id)->villager.restocksToday==1,
              "workplace search ignored reachable alternative approach");
    }
    {
        Scene s(assets);
        const auto id=worker(s,{.5,1,.5},{8,1,0});
        for(int z=-48;z<64;++z) for(int y=1;y<=4;++y) s.block(4,y,z,BlockId::STONE);
        advance(s,1,60,false,3000);
        for(int z=-1;z<=1;++z) for(int y=1;y<=4;++y) s.block(4,y,z,BlockId::AIR);
        advance(s,16,60,false,3000);
        check(s.mobs.entityById(id)->villager.restocksToday==1,
              "removed wall failed to resume route to workplace");
    }
    {
        Scene s(assets);
        const auto id=s.add(EntityType::Spider,{.5,1,.5});
        s.player.setPosition({4.5,1,.5});
        for(int i=0;i<30;++i) s.mobs.update(s.player,1.0f/60,true,false,true,false);
        check(!s.mobs.entityById(id)->ai.hasTarget,"daytime spider became hostile without provocation");
        MeleeAttackRequest hit; hit.damage=1; hit.reach=6;
        s.mobs.attackRay({-2,1.2,.5},{1,0,0},hit);
        for(int i=0;i<20;++i) s.mobs.update(s.player,1.0f/60,true,false,true,false);
        check(s.mobs.entityById(id)->ai.hasTarget,"provoked daytime spider did not retaliate");
    }
    {
        // The same panic deadline holds at each supported presentation cadence.
        for(int fps:{30,60,120}) {
            Scene s(assets);
            const auto id=s.add(EntityType::Pig,{.5,1,.5});
            advance(s,.2,fps);
            MeleeAttackRequest hit; hit.damage=1; hit.reach=6;
            s.mobs.attackRay({-2,1.5,.5},{1,0,0},hit);
            advance(s,2.8,fps);
            check(s.mobs.entityById(id)->ai.behavior==EntityBehavior::Flee,"panic expired early");
            advance(s,.4,fps);
            check(s.mobs.entityById(id)->ai.behavior!=EntityBehavior::Flee,"panic depends on frame count");
        }
    }
    {
        Scene s(assets);
        const auto root=std::filesystem::temp_directory_path()/"minecraftc-ai-unload-test";
        std::filesystem::remove_all(root);
        SaveStore store(root);
        const auto zombie=s.add(EntityType::Zombie,{.5,1,.5});
        const auto villager=s.add(EntityType::Villager,{16.5,1,.5});
        s.mobs.setSaveStore(&store);
        s.mobs.syncChunks();
        advance(s,.2);
        check(s.mobs.entityById(zombie)->ai.targetId==villager,"unload fixture did not acquire villager");
        const int previous=Config::RENDER_DISTANCE;
        Config::RENDER_DISTANCE=0;
        for (int i=0;i<64;++i) {
            s.world.update({.5,1,.5},0);
            s.mobs.syncChunks();
        }
        Config::RENDER_DISTANCE=previous;
        check(!s.mobs.entityById(villager),"target chunk was not unloaded by streaming fixture");
        s.step();
        check(!s.mobs.entityById(zombie)->ai.hasTarget,"unloaded entity ID retained an active target");
        s.mobs.setSaveStore(nullptr);
        std::filesystem::remove_all(root);
    }
    std::cout<<"Entity AI integration tests passed\n";
    return 0;
}
}
