#pragma once

namespace NetherCombatCases {
inline void run(const std::filesystem::path& assets) {
    using namespace EntityAiScenarios;
    std::cerr<<"Nether integration: native combat and provocation\n";
    for(bool resistant:{false,true}) {
        Scene s(assets);
        WorldMetadata::PersistedEntity fireball;fireball.type=static_cast<uint16_t>(EntityType::BlazeFireball);
        fireball.position={.5,2.2,2.5};fireball.velocity={0,0,-8};fireball.health=1;s.mobs.loadEntities({fireball});
        if(resistant)s.player.statusEffects().apply(StatusEffectType::FireResistance,0,200);
        advance(s,.4,60,true);
        check(s.player.survivalStats().health()==(resistant?20:15),"blaze fireball/fire resistance damage");
    }
    for(bool pvp:{false,true})for(bool nearbyImpact:{false,true})for(bool reflected:{false,true}) {
        Scene s(assets,PresentationMode::Null);
        Player guest(s.world);guest.configureRules(GameMode::Survival,Difficulty::Normal);
        guest.setPosition({nearbyImpact?1.4:.5,1,6.5});guest.setRoomPlayerId(7);
        const std::string owner(32,'b');s.player.setPersistentIdentity(owner);
        WorldMetadata::PersistedEntity fireball;fireball.type=static_cast<uint16_t>(EntityType::GhastFireball);
        fireball.position={.5,2.4,2.5};fireball.velocity={0,0,-8};fireball.health=1;s.mobs.loadEntities({fireball});
        if(reflected) {
            MeleeAttackRequest attack;attack.damage=1;attack.reach=4;
            check(s.mobs.attackRay(s.player.getEyePosition(),{0,0,1},attack,0,owner).foundTarget,"PvP fireball deflection fixture");
        } else {
            auto saved=s.mobs.saveEntities();saved[0].velocity={0,0,18};s.mobs.clear();s.mobs.loadEntities(saved);
        }
        if(nearbyImpact)s.add(EntityType::Zombie,{.5,1,6.5});
        else s.block(0,2,8,BlockId::STONE);
        const std::vector<EntityPlayerView> views{{0,&s.player,true,false,true},{7,&guest,true,false,true}};
        s.mobs.setPvpPlayers(pvp,views);
        for(int i=0;i<30;++i)s.mobs.update(views,1.f/60,false,false);
        const bool shouldHurt=pvp||!reflected;
        check((guest.survivalStats().health()<20)==shouldHurt,"reflected direct/explosion damage must obey PvP while hostile fireballs remain harmful");
        check(std::none_of(s.mobs.entities().begin(),s.mobs.entities().end(),[](const auto& e){return nativeProjectile(e.type);}),"fireball fixture did not reach its impact");
    }
    {
        Scene s(assets);s.add(EntityType::Ghast,{.5,1,8.5});
        auto saved=s.mobs.saveEntities();WorldMetadata::PersistedEntity fireball;
        fireball.type=static_cast<uint16_t>(EntityType::GhastFireball);fireball.position={.5,2.6,2.5};fireball.velocity={0,0,-8};fireball.health=1;
        saved.push_back(fireball);s.mobs.clear();s.mobs.loadEntities(saved);
        MeleeAttackRequest attack;attack.damage=1;attack.reach=4;
        check(s.mobs.attackRay(s.player.getEyePosition(),{0,0,1},attack,0).foundTarget,"ghast fireball deflection ray");
        advance(s,.5);
        check(std::none_of(s.mobs.entities().begin(),s.mobs.entities().end(),[](const auto& e){return e.type==EntityType::Ghast&&e.health>0;}),"deflected fireball cannot defeat fire-immune ghast");
    }
    {
        Scene s(assets);const auto piglin=s.add(EntityType::Piglin,{4.5,1,.5}),zombie=s.add(EntityType::ZombifiedPiglin,{8.5,1,.5});
        s.mobs.angerPiglins({.5,1,.5});
        check(s.mobs.entityById(piglin)->nether.angerTicks==600&&s.mobs.entityById(zombie)->nether.angerTicks==0,"container theft enrages unrelated zombified piglins");
        s.mobs.angerPiglins({.5,1,.5},true);check(s.mobs.entityById(zombie)->nether.angerTicks==600,"zombified piglin group retaliation");
    }
    {
        Scene s(assets);const auto brute=s.add(EntityType::PiglinBrute,{8.5,1,.5});
        s.player.inventory().armor()[0]={ItemId::GOLDEN_HELMET,1,0};advance(s,.3,60,true);
        check(s.mobs.entityById(brute)->ai.hasTarget,"gold armor incorrectly pacifies brute");
    }
    {
        Scene s(assets);s.add(EntityType::WitherSkeleton,{.5,1,1.6});advance(s,.8,60,true);
        check(s.player.statusEffects().level(StatusEffectType::Wither)>0&&s.player.survivalStats().health()<20,"wither skeleton melee effect");
    }
    {
        Scene s(assets);const auto enderman=s.add(EntityType::Enderman,{.5,1,8.5});
        const auto eye=s.player.getEyePosition();s.player.setOrientation(0,float(glm::degrees(std::atan2(3.65-eye.y,8.0))));
        advance(s,.2,60,true);check(s.mobs.entityById(enderman)->nether.angerTicks>0,"enderman gaze aggression");
        const auto before=s.mobs.entityById(enderman)->position;
        MeleeAttackRequest attack;attack.damage=1;attack.reach=6;
        check(s.mobs.attackRay(before+glm::dvec3(0,1,-2),{0,0,1},attack).primaryDamaged,"enderman hurt fixture");
        check(s.mobs.entityById(enderman)->position!=before,"hurt enderman did not teleport");
    }
    {
        Scene s(assets);
        for(int z=-24;z<=40;++z)for(int x=-32;x<=32;++x)s.block(x,-32,z,BlockId::STONE);
        const auto id=s.add(EntityType::Enderman,{.5,-31,8.5});
        const auto before=s.mobs.entityById(id)->position;MeleeAttackRequest attack;attack.damage=1;attack.reach=6;
        check(s.mobs.attackRay(before+glm::dvec3(0,1,-2),{0,0,1},attack).primaryDamaged,"negative-height enderman hit");
        check(s.mobs.entityById(id)->position!=before&&s.mobs.entityById(id)->position.y<0,"Overworld negative-height enderman teleport");
    }
    {
        Scene s(assets);const auto id=s.add(EntityType::ZombieVillager,{.5,1,2.5});
        auto saved=s.mobs.saveEntities();saved[0].effects={{StatusEffectType::Weakness,0,1200}};s.mobs.clear();s.mobs.loadEntities(saved);
        s.player.inventory().slot(0)={ItemId::GOLDEN_APPLE,1,0};
        check(s.mobs.useNativeMob(s.player,0)&&s.player.inventory().slot(0).empty(),"weakness/golden apple curing transaction");
        saved=s.mobs.saveEntities();check(saved[0].nether.cureTicks==3600,"curing countdown persistence");
        saved[0].nether.cureTicks=1;s.mobs.clear();s.mobs.loadEntities(saved);advance(s,.1);
        check(s.mobs.entities()[0].type==EntityType::Villager,"zombie villager cure completion");(void)id;
    }
}
}
