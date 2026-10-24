#pragma once
#include "EntityAiIntegration.h"
#include "NetherCombatCases.h"
#include "game/NetherProgression.h"
#include "network/EntityProtocol.h"

namespace NetherMobIntegration {
inline int run(const std::filesystem::path& assets) {
    using namespace EntityAiScenarios;
    std::cerr<<"Nether integration: registry and persistence\n";
    {
        Scene s(assets);int i=0;
#define SPAWN(type,egg,key,label,h,x,y,z,speed,d,monster,fire) check(s.mobs.spawnMob(EntityType::type,{double(i++*4-20),1,10}),"native mob spawn");
        MC_NETHER_MOBS(SPAWN)
#undef SPAWN
        auto saved=s.mobs.saveEntities();check(saved.size()==11,"native mob count");
        saved[3].nether.magmaSize=1;saved[4].nether.barterTicks=75;saved[9].nether.saddled=1;
        saved[10].effects={{StatusEffectType::Weakness,1,1200}};
        const auto path=std::filesystem::temp_directory_path()/"minecraftc-nether-mobs-v22";
        std::filesystem::remove_all(path);SaveStore store(path);store.saveChunkEntities(0,0,saved);
        const auto restored=store.loadChunkEntities(0,0);
        check(restored.size()==11 && restored[4].nether.barterTicks==75 && restored[9].nether.saddled==1 && restored[10].effects[0].remaining==1200,"native state save roundtrip");
        s.mobs.clear();s.mobs.loadEntities(restored);
        check(s.mobs.entities()[3].nether.magmaSize==1 && s.mobs.entities()[9].riderId==UINT64_MAX,"native durable state or transient rider");
        Lan::EntityBatch batch;batch.dimension=DimensionId::Nether;batch.epoch=1;
        EntitySnapshot snapshot;snapshot.id=1;snapshot.type=EntityType::Strider;snapshot.health=20;snapshot.nether.saddled=1;snapshot.riderId=7;
        snapshot.effects={{StatusEffectType::FireResistance,0,300}};batch.entities.push_back(snapshot);
        const auto decoded=Lan::decodeEntityBatch(Lan::encodeEntityBatch(batch));
        check(decoded.entities[0].nether.saddled && decoded.entities[0].riderId==7 && decoded.entities[0].effects[0].remaining==300,"native state wire roundtrip");
        std::filesystem::remove_all(path);
    }
    {
        std::cerr<<"Nether integration: piglin barter\n";
        Scene s(assets);const auto id=s.add(EntityType::Piglin,{.5,1,-1.5});
        s.player.inventory().armor()[0]={ItemId::GOLDEN_HELMET,1,0};advance(s,.3,60,true);
        check(!s.mobs.entityById(id)->ai.hasTarget,"gold armor neutrality");
        s.player.setPosition(s.mobs.entityById(id)->position+glm::dvec3(0,0,-2));
        s.player.inventory().slot(0)={ItemId::GOLD_INGOT,2,0};
        check(s.mobs.useNativeMob(s.player,0),"barter payment");
        check(s.player.inventory().slot(0).count==1 && !s.mobs.useNativeMob(s.player,0),"barter double payment");
        advance(s,3);auto saved=s.mobs.saveEntities();s.mobs.clear();s.mobs.loadEntities(saved);advance(s,3.1);
        const auto drops=std::count_if(s.mobs.entities().begin(),s.mobs.entities().end(),[](const auto& e){return e.type==EntityType::Item;});
        check(drops==1,"barter resume emitted exactly once");
    }
    {
        std::cerr<<"Nether integration: conversion\n";
        Scene s(assets);const auto id=s.add(EntityType::Hoglin,{8.5,1,.5});advance(s,15.2);
        check(s.mobs.entityById(id)->type==EntityType::Zoglin,"hoglin Overworld conversion");
    }
    {
        std::cerr<<"Nether integration: riding\n";
        Scene s(assets);s.add(EntityType::Strider,{.5,1,2.5});s.player.inventory().slot(0)={ItemId::SADDLE,1,0};
        check(s.mobs.useNativeMob(s.player,0)&&s.player.inventory().slot(0).empty(),"strider saddle");
        s.world.setBlock(0,4,2,BlockId::STONE);
        check(!s.mobs.useNativeMob(s.player,0)&&!s.mobs.riding(0),"strider cannot mount through a low ceiling");
        s.world.setBlock(0,4,2,BlockId::AIR);
        check(s.mobs.useNativeMob(s.player,0)&&s.mobs.riding(0),"strider mount");
        s.player.inventory().slot(0)={ItemId::WARPED_FUNGUS_ON_A_STICK,1,0};
        check(s.mobs.useNativeMob(s.player,0)&&s.player.inventory().slot(0).damage==1,"strider boost durability");
        s.mobs.dismount(0);check(!s.mobs.riding(0),"strider dismount");
    }
    {
        std::cerr<<"Nether integration: splitting\n";
        Scene s(assets);s.add(EntityType::MagmaCube,{4.5,1,.5});auto saved=s.mobs.saveEntities();saved[0].health=0;
        s.mobs.clear();s.mobs.loadEntities(saved);s.step();
        check(s.mobs.entities().size()==4 && std::all_of(s.mobs.entities().begin(),s.mobs.entities().end(),[](const auto& e){return e.type==EntityType::MagmaCube&&e.nether.magmaSize==1;}),"magma cube split");
    }
    {
        std::cerr<<"Nether integration: spawners\n";
        Scene s(assets);s.world.setBlock(5,1,5,BlockId::BLAZE_SPAWNER);
        check(s.world.getBlockEntity({5,1,5})!=nullptr,"placed spawner state");
        advance(s,1.2,60,true);
        check(std::any_of(s.mobs.entities().begin(),s.mobs.entities().end(),[](const auto& e){return e.type==EntityType::Blaze;}),"active blaze spawner");
    }
    {
        std::cerr<<"Nether integration: splash effects\n";
        Scene s(assets);s.add(EntityType::Zombie,{.5,1,-2.5});
        s.mobs.spawnNativeProjectile(EntityType::ThrownPotion,{.5,2,-.5},{0,0,-8},0,{ItemId::SPLASH_HEALING_POTION,1,0});
        advance(s,.5);
        const auto zombie=std::find_if(s.mobs.entities().begin(),s.mobs.entities().end(),[](const auto& e){return e.type==EntityType::Zombie;});
        check(zombie!=s.mobs.entities().end()&&zombie->health<20,"undead splash healing inversion");
    }
    {
        std::cerr<<"Nether integration: native entity transfer\n";
        Scene source(assets),target(assets);
        target.world.resetForNewSeed(7,WorldType::Superflat,DimensionId::Nether);
        for(int z=-3;z<=3;++z)for(int x=-3;x<=3;++x) {
            auto* chunk=target.world.getChunk(x,z);
            for(int bz=0;bz<16;++bz)for(int bx=0;bx<16;++bx)chunk->setBlock(bx,0,bz,BlockId::OBSIDIAN);
            chunk->generated=true;
        }
        const int previous=Config::RENDER_DISTANCE;Config::RENDER_DISTANCE=3;target.world.update({.5,1,.5},0);Config::RENDER_DISTANCE=previous;
        const auto root=std::filesystem::temp_directory_path()/"minecraftc-nether-runtime-transfer";
        std::filesystem::remove_all(root);WorldMetadata metadata;metadata.generationVersion=WorldGenContext::GENERATION_VERSION;metadata.seed=7;
        SaveStore overworld(root),nether(root/"dimensions/nether");overworld.saveMetadata(metadata);
        source.mobs.setSaveStore(&overworld);target.mobs.setSaveStore(&nether);source.mobs.syncChunks();target.mobs.syncChunks();
        source.mobs.spawnItem({.5,1.05,.5},{ItemId::GOLD_INGOT,3,0});const auto id=source.mobs.entities().back().id;
        check(source.mobs.transferTo(id,target.mobs,{.5,1.01,.5},root),"item dimension transfer");
        check(source.mobs.entities().empty()&&target.mobs.entities().size()==1&&target.mobs.entities()[0].item.count==3,"item transfer conservation");
        check(!source.mobs.transferTo(id,target.mobs,{.5,1.01,.5},root),"replayed runtime transfer");
        check(overworld.loadChunkEntities(0,0).empty()&&nether.loadChunkEntities(0,0).size()==1,"transfer durable partitions");
        const auto mob=source.add(EntityType::Piglin,{4.5,1,.5});
        check(!source.mobs.transferTo(mob,target.mobs,{.5,-10,.5},root)&&source.mobs.entityById(mob),"unsafe transfer preserves source");
        check(source.mobs.transferTo(mob,target.mobs,{4.5,1.01,.5},root),"mob dimension transfer");
        const auto back=target.mobs.entities().back().id;
        check(target.mobs.transferTo(back,source.mobs,{4.5,1.01,.5},root),"mob return transfer");
        source.mobs.setSaveStore(nullptr);target.mobs.setSaveStore(nullptr);std::filesystem::remove_all(root);
    }
    {
        std::cerr<<"Nether integration: reconnected pearl owner\n";
        Scene s(assets);Player guest(s.world);guest.configureRules(GameMode::Survival,Difficulty::Normal);
        const std::string key(32,'a');guest.setPersistentIdentity(key);guest.setRoomPlayerId(77);guest.setPosition({8.5,1,.5});
        const auto hostPosition=s.player.getPosition();
        s.mobs.spawnNativeProjectile(EntityType::EnderPearl,{8.5,2,1.5},{0,-3,2},7,{ItemId::ENDER_PEARL,1,0},key);
        const auto saved=s.mobs.saveEntities();s.mobs.clear();s.mobs.loadEntities(saved);
        for(int i=0;i<30;++i)s.mobs.update(std::vector<EntityPlayerView>{{0,&s.player,true,false,true},{77,&guest,true,false,true}},1.f/60,false,false);
        check(s.player.getPosition()==hostPosition&&s.player.survivalStats().health()==20,"pearl selected the host instead of its stable owner");
        check(guest.getPosition().z>1 && guest.survivalStats().health()==15,"reconnected pearl owner was not teleported");
    }
    NetherCombatCases::run(assets);
    std::cout<<"Native Nether mob, barter, riding, spawner and persistence tests passed\n";return 0;
}
}
