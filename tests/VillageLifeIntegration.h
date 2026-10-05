#pragma once
#include "EntityAiIntegration.h"

namespace EntityAiScenarios {
inline void villageDay(Scene& s,float seconds,uint64_t tick=3000) {
    for(int frame=0;frame<static_cast<int>(seconds*60);++frame) {
        s.mobs.update(s.player,1.0f/60,true,false,false,false,false,false,tick);
        check(s.mobs.aiStats().pathNodes<=Config::AI_PATH_NODES_PER_FRAME,"village exceeded navigation budget");
        check(s.mobs.aiStats().villageActions<=Config::VILLAGE_ACTIONS_PER_SECOND &&
            s.mobs.aiStats().farmBlocks<=Config::VILLAGE_ACTIONS_PER_SECOND*Config::VILLAGE_FARM_SCAN_SLICE,
            "village lifecycle work budget exceeded");
        check(s.mobs.aiStats().poiBlocks<=Config::AI_POI_BLOCKS_PER_FRAME,"village exceeded POI budget");
    }
}
inline void bed(Scene& s,int x,int z) {
    s.block(x,1,z,bedBlock(BedPart::Foot,BedDirection::North));
    s.block(x,1,z-1,bedBlock(BedPart::Head,BedDirection::North));
}
inline void villageLife(const std::filesystem::path& assets) {
    {
        Scene s(assets);bed(s,2,0);bed(s,6,0);bed(s,2,4);
        s.add(EntityType::Villager,{.5,1,.5});s.add(EntityType::Villager,{1.5,1,.5});
        auto saved=s.mobs.saveEntities();
        for(size_t i=0;i<2;++i) {
            auto& v=saved[i].villager;v.hasBed=true;v.claimedBed={i?6:2,1,0};
            v.food[0]={ItemId::BREAD,3,0};
        }
        s.mobs.clear();s.mobs.loadEntities(saved);villageDay(s,3);
        check(s.mobs.entities().size()==3,"food and reachable spare bed did not produce a child");
        int children=0,food=0;std::set<std::tuple<int,int,int>> beds;
        for(const auto& e:s.mobs.entities()) {
            children+=!e.villager.adult();food+=villagerFoodCount(e.villager,ItemId::BREAD);
            if(e.villager.hasBed)check(beds.insert({e.villager.claimedBed.x,e.villager.claimedBed.y,e.villager.claimedBed.z}).second,
                "birth duplicated a claimed bed");
        }
        check(children==1 && food==0,"birth duplicated children or failed to consume parent food");
        saved=s.mobs.saveEntities();s.mobs.clear();s.mobs.loadEntities(saved);
        check(s.mobs.entities().back().villager.growthSeconds>1196,"reload lost child growth progress");
        villageDay(s,3);check(s.mobs.entities().size()==3,"reload bypassed breeding cooldown or spare bed limit");
    }
    {
        Scene s(assets);bed(s,2,0);bed(s,6,0);bed(s,2,4);
        for(int x=1;x<=3;++x)for(int z=2;z<=5;++z)for(int y=1;y<=3;++y)
            if(!(x==2 && (z==3 || z==4) && y==1))s.block(x,y,z,BlockId::STONE);
        s.add(EntityType::Villager,{.5,1,.5});s.add(EntityType::Villager,{1.5,1,.5});
        auto saved=s.mobs.saveEntities();
        for(size_t i=0;i<2;++i) {saved[i].villager.hasBed=true;saved[i].villager.claimedBed={i?6:2,1,0};
            saved[i].villager.food[0]={ItemId::BREAD,3,0};}
        s.mobs.clear();s.mobs.loadEntities(saved);villageDay(s,3);
        check(s.mobs.entities().size()==2,"blocked spare bed allowed birth");
        for(const auto& e:s.mobs.entities())check(villagerFoodCount(e.villager,ItemId::BREAD)==3,
            "failed birth consumed food");
    }
    {
        Scene s(assets);const auto id=worker(s,{.5,1,.5},{0,1,-2});
        s.block(1,0,0,BlockId::FARMLAND_7);s.block(1,1,0,BlockId::WHEAT_7);
        villageDay(s,20);
        const auto* farmer=s.mobs.entityById(id);
        check(farmer && s.world.getBlock(1,1,0)==BlockId::WHEAT_0 &&
            villagerFoodCount(farmer->villager,ItemId::WHEAT)==1 &&
            villagerFoodCount(farmer->villager,ItemId::WHEAT_SEEDS)==1,
            "farmer harvest/replant did not conserve wheat and seeds");
        auto rules=s.world.gameRules();rules.set(GameRuleId::MobGriefing,GameRuleValue::boolean(false));s.world.setGameRules(rules);
        s.block(1,1,0,BlockId::WHEAT_7);villageDay(s,20);
        check(s.world.getBlock(1,1,0)==BlockId::WHEAT_7,"farmer ignored mob_griefing=false");
        auto saved=s.mobs.saveEntities();saved[0].villager.food={};saved[0].villager.food[0]={ItemId::WHEAT,3,0};
        s.mobs.clear();s.mobs.loadEntities(saved);villageDay(s,2);
        check(villagerFoodCount(s.mobs.entities()[0].villager,ItemId::WHEAT)==0 &&
            villagerFoodCount(s.mobs.entities()[0].villager,ItemId::BREAD)==1,
            "mob_griefing disabled baking food already stored");
    }
    {
        Scene s(assets);bed(s,2,0);bed(s,6,0);
        s.add(EntityType::Villager,{.5,1,.5});s.add(EntityType::Villager,{4.5,1,.5});
        auto saved=s.mobs.saveEntities();
        for(size_t i=0;i<2;++i) {saved[i].villager.hasBed=true;saved[i].villager.claimedBed={i?6:2,1,0};}
        s.mobs.clear();s.mobs.loadEntities(saved);villageDay(s,.3);
        MeleeAttackRequest hit;hit.damage=1;hit.reach=6;
        check(s.mobs.attackRay({-2,1.5,.5},{1,0,0},hit).primaryDamaged,"reputation fixture missed villager");
        for(const auto& e:s.mobs.entities())check(e.villager.reputation==-10,
            "player injury did not affect victim and visible village witness");
        hit.damage=200;
        check(s.mobs.attackRay({-2,1.5,.5},{1,0,0},hit).primaryDamaged,"reputation kill fixture missed");
        for(const auto& e:s.mobs.entities())check(e.villager.reputation==-35,
            "player kill reputation did not accumulate for witnesses");
    }
    {
        Scene s(assets);
        for(int i=0;i<40;++i)s.add(EntityType::Villager,{-18.5+4*(i%10),1,-6.5+4*(i/10)});
        villageDay(s,5);
        check(s.mobs.entities().size()==40,"large village simulation changed unfed population");
    }
    {
        Scene s(assets);
        for(int i=0;i<5;++i) {bed(s,i*2-4,5);s.add(EntityType::Villager,{i*2-3.5,1,7.5});}
        auto saved=s.mobs.saveEntities();
        for(int i=0;i<5;++i) {saved[i].villager.hasBed=true;saved[i].villager.claimedBed={i*2-4,1,5};}
        s.mobs.clear();s.mobs.loadEntities(saved);s.mobs.setNaturalSpawningEnabled(true);
        villageDay(s,2);
        const auto golems=[&] {return std::count_if(s.mobs.entities().begin(),s.mobs.entities().end(),
            [](const Entity& e){return e.type==EntityType::IronGolem && e.health>0;});};
        check(golems()==1,"eligible loaded village did not receive one defender");
        saved=s.mobs.saveEntities();s.mobs.clear();s.mobs.loadEntities(saved);villageDay(s,2);
        check(golems()==1,"reload duplicated the village defender");
        s.mobs.setNaturalSpawningEnabled(false);
        auto golem=std::find_if(s.mobs.entities().begin(),s.mobs.entities().end(),[](const auto& e){return e.type==EntityType::IronGolem;});
        const auto id=golem->id;const auto p=golem->position;
        const auto zombie=s.add(EntityType::Zombie,p+glm::dvec3(3,0,0));villageDay(s,4);
        const auto* enemy=s.mobs.entityById(zombie);
        check(!enemy || enemy->health<20,"golem did not attack visible hostile");
        const auto* defender=s.mobs.entityById(id);
        check(defender && defender->health>0,"defender unexpectedly disappeared");
        MeleeAttackRequest hit;hit.damage=200;hit.reach=6;
        const auto position=defender->position;
        check(s.mobs.attackRay(position+glm::dvec3(0,2,2),{0,0,-1},hit).primaryDamaged,
            "defender death fixture missed");
        s.mobs.setNaturalSpawningEnabled(true);villageDay(s,2);
        check(golems()==0,"defender death bypassed regeneration cooldown");
        for(const auto& e:s.mobs.entities())if(e.type==EntityType::Villager)
            check(e.villager.defenseCooldown>590,"defender death lost resident cooldown");
    }
    std::cout<<"Village lifecycle integration tests passed\n";
}
}
