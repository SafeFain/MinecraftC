#pragma once
#include "game/SurvivalStats.h"
#include "world/FluidLogic.h"
#include <stdexcept>

namespace GameRuleIntegration {
inline void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
inline void rule(World& world, GameRuleId id, GameRuleValue value) {
    GameRuleSet rules=world.gameRules(); check(rules.set(id,value),"test rule invalid"); world.setGameRules(rules);
}
inline int run(const std::filesystem::path& assets) {
    check(explosionDropSurvives(1717986918u,2.5f,true) && !explosionDropSurvives(1717986919u,2.5f,true),
        "fractional explosion probability truncated");
    check(explosionDropSurvives(UINT32_MAX,4.0f,false) && !explosionDropSurvives(UINT32_MAX,4.0f,true),
        "explosion decay toggle ignored");
    const auto root=std::filesystem::temp_directory_path()/"minecraftc-gamerule-integration";
    std::filesystem::remove_all(root);
    {
        GameSession session(root);
        Localization localization;
        auto run=[&](const char* text) { return runCommand(session,localization,text); };
        GameSessionTestAccess::setCheats(session,false);
        check(run("/gamerule keep_inventory true").messages[0]=="message.cheats_disabled","rule cheats gate missing");
        GameSessionTestAccess::setCheats(session,true);
        check(!session.metadata().gameRules.boolean(GameRuleId::KeepInventory),"denied mutation changed state");
        run("/gamerule keepInventory true");
        check(GameSessionTestAccess::world(session).gameRules().boolean(GameRuleId::KeepInventory),"live rule not synchronized");
        const auto before=session.daylightState().phase();
        run("/gamerule doDaylightCycle false"); session.updateDaylight(100,true);
        check(session.daylightState().phase()==before,"frozen time advances");
        run("/time set night"); check(session.daylightState().phase()!=before,"explicit time command blocked");
        run("/gamerule advance_time true"); const auto night=session.daylightState().phase(); session.updateDaylight(1,true);
        check(session.daylightState().phase()!=night,"time resume fails");
        run("/weather thunder"); run("/gamerule advance_weather false");
        const auto timer=session.weatherState().saveState().rainTicks;
        GameSessionTestAccess::tickWeather(session);
        check(session.weatherState().saveState().rainTicks==timer,"weather timer changed while disabled");
        run("/weather clear"); check(!session.weatherState().raining(),"explicit weather command blocked");
        run("/weather thunder"); run("/gamerule players_sleeping_percentage 101");
        const auto preSleep=session.daylightState().phase();
        GameSessionTestAccess::sleepChoice(session,{});
        check(session.daylightState().phase()==preSleep && session.weatherState().thundering(),"sleep threshold ignored");
        run("/gamerule players_sleeping_percentage 100"); run("/gamerule advance_time false");
        GameSessionTestAccess::sleepChoice(session,{});
        check(session.daylightState().phase()==preSleep && session.weatherState().thundering(),"sleep bypasses frozen rules");
        run("/gamerule advance_time true"); run("/gamerule advance_weather true");
        GameSessionTestAccess::sleepChoice(session,{});
        check(session.daylightState().phase()!=preSleep && !session.weatherState().raining(),"normal sleep no longer resets environment");
        const auto help=run("/help gamerule keep_inventory"); check(help.messages.size()==3,"single help not limited to rule");
        check(run("/gamerule raids false").messages.back()=="message.gamerule_unavailable","missing mechanism hidden");
        run("/gamerule send_command_feedback false");
        check(run("/gamerule keep_inventory").messages.size()==1,"query feedback suppressed");
        check(run("/gamerule keep_inventory false").messages.empty(),"silent setter reports feedback");
        check(!run("/help").messages.empty(),"help suppressed");
        const auto silentUnavailable=run("/gamerule raids true");
        check(silentUnavailable.messages.size()==1 && silentUnavailable.messages[0]=="message.gamerule_unavailable",
            "silent setter hides support warning or retains success feedback");
        check(!run("/gamerule send_command_feedback true").messages.empty(),"feedback restore has no acknowledgement");
        session.inventory().slot(0)={ItemId::DIAMOND,3,0};
        run("/gamerule keep_inventory true"); GameSessionTestAccess::die(session);
        check(session.inventory().slot(0).count==3 && GameSessionTestAccess::entities(session).entities().empty(),"kept inventory still drops");
        run("/gamerule keep_inventory false"); GameSessionTestAccess::die(session);
        check(session.inventory().slot(0).empty() && !GameSessionTestAccess::entities(session).entities().empty(),"normal death no longer drops");
    }
    {
        EntityAiScenarios::Scene scene(assets);
        for (auto pair : {std::pair{DamageCause::Fall,GameRuleId::FallDamage},
                          std::pair{DamageCause::Drowning,GameRuleId::DrowningDamage},
                          std::pair{DamageCause::Fire,GameRuleId::FireDamage}}) {
            rule(scene.world,pair.second,GameRuleValue::boolean(false));
            scene.player.survivalStats().set(20,20,5,0); scene.player.resetDamageImmunity();
            DamageSourceInfo damage; damage.cause=pair.first; damage.amount=4;
            check(scene.player.takeDamage(damage).appliedDamage==0,"disabled damage applied");
            damage.cause=DamageCause::Melee;
            check(scene.player.takeDamage(damage).appliedDamage>0,"other damage was also disabled");
            rule(scene.world,pair.second,GameRuleValue::boolean(true)); scene.player.resetDamageImmunity(); damage.cause=pair.first;
            check(scene.player.takeDamage(damage).appliedDamage>0,"damage enable failed");
        }
        rule(scene.world,GameRuleId::NaturalHealthRegeneration,GameRuleValue::boolean(false));
        scene.player.survivalStats().set(10,20,5,0); scene.player.setSleepingVisual(true,1); scene.player.update(1);
        check(scene.player.survivalStats().health()==10,"disabled regeneration heals");
        scene.player.survivalStats().set(10,0,0,0); scene.player.update(4);
        check(scene.player.survivalStats().health()<10,"disabling regeneration stops starvation");
        rule(scene.world,GameRuleId::NaturalHealthRegeneration,GameRuleValue::boolean(true));
        scene.player.survivalStats().set(10,20,5,0); scene.player.update(1);
        check(scene.player.survivalStats().health()>10,"regeneration does not resume");
    }
    {
        GameSession session(root);
        GameSessionTestAccess::flatRuleScene(session);
        GameRuleSet rules; rules.set(GameRuleId::KeepInventory,GameRuleValue::boolean(true));
        GameSessionTestAccess::setRules(session,rules);
        int deathScreens=0,deathMessages=0;
        GameSession::Feedback feedback; feedback.playerDied=[&]{++deathScreens;};
        feedback.playerDeathMessage=[&]{++deathMessages;};
        GameSessionTestAccess::player(session).survivalStats().damage(100);
        session.updatePlaying(0,nullptr,feedback);
        check(session.isPlayerDead() && deathScreens==1 && deathMessages==1,"normal death screen/message missing");
        session.respawn(0);
        const auto position=session.playerState().getPosition();
        check(std::abs(position.x-.5)<=10 && std::abs(position.z-.5)<=10 && position.y>0,
            "safe respawn escaped radius");
        rules.set(GameRuleId::ImmediateRespawn,GameRuleValue::boolean(true));
        rules.set(GameRuleId::ShowDeathMessages,GameRuleValue::boolean(false));
        rules.set(GameRuleId::RespawnRadius,GameRuleValue::integer(0));
        GameSessionTestAccess::setRules(session,rules);
        GameSessionTestAccess::player(session).survivalStats().damage(100);
        session.updatePlaying(0,nullptr,feedback);
        check(!session.isPlayerDead() && session.playerState().survivalStats().health()==20 && deathScreens==1 && deathMessages==1,
            "immediate respawn or death-message suppression ignored");
        check(session.playerState().getPosition().x==.5 && session.playerState().getPosition().z==.5,"zero respawn radius not exact");
    }
    for (bool enabled : {false,true}) {
        EntityAiScenarios::Scene scene(assets);
        scene.mobs.setNaturalSpawningEnabled(true);
        rule(scene.world,GameRuleId::SpawnMobs,GameRuleValue::boolean(enabled));
        for(int i=0;i<8;++i)scene.step(4.01f);
        check(scene.mobs.entities().empty()!=enabled,"natural spawning switch ignored");
        check(scene.mobs.spawnMob(EntityType::Cow,{.5,1,.5}),"natural spawning switch blocks spawn egg path");
        const auto saved=scene.mobs.saveEntities(); scene.mobs.clear();scene.mobs.loadEntities(saved);
        check(scene.mobs.entities().size()==saved.size(),"natural spawn rule blocks restored entities");
    }
    {
        EntityAiScenarios::Scene scene(assets);
        scene.mobs.setNaturalSpawningEnabled(true);
        rule(scene.world,GameRuleId::SpawnMonsters,GameRuleValue::boolean(false));
        for(int i=0;i<8;++i)scene.step(4.01f);
        check(scene.mobs.entities().empty(),"monster switch substitutes animals for blocked night monsters");
        for(int i=0;i<8;++i)scene.mobs.update(scene.player,4.01f,true,false,false,false,false,false,6000);
        check(!scene.mobs.entities().empty(),"monster switch prevents daytime passive spawning");
        for(const auto& entity:scene.mobs.entities())check(entity.type==EntityType::Cow || entity.type==EntityType::Pig ||
            entity.type==EntityType::Sheep || entity.type==EntityType::Chicken,"disabled natural monster spawned");
    }
    for (bool enabled : {false,true}) {
        EntityAiScenarios::Scene scene(assets);
        rule(scene.world,GameRuleId::MobDrops,GameRuleValue::boolean(enabled));
        scene.add(EntityType::Cow,{.5,1,.5}); MeleeAttackRequest hit; hit.reach=6; hit.damage=100;
        scene.mobs.attackRay({-2,1.6,.5},{1,0,0},hit); scene.step();
        const bool drops=std::any_of(scene.mobs.entities().begin(),scene.mobs.entities().end(),[](const Entity& e){return e.type==EntityType::Item;});
        check(drops==enabled,"mob loot switch ignored");
    }
    for (bool enabled : {false,true}) {
        EntityAiScenarios::Scene scene(assets);
        rule(scene.world,GameRuleId::BlockDrops,GameRuleValue::boolean(enabled));
        scene.player.setMouseLocked(true);scene.player.setEntityManager(&scene.mobs);
        scene.block(0,2,3,BlockId::DIRT);
        scene.player.handleMouseButton(MouseButton::Left,ButtonAction::Press);scene.player.update(5);
        check(scene.world.getBlock(0,2,3)==BlockId::AIR,"mining fixture did not break dirt");
        bool inventoryDrop=false;
        for(const auto& stack:scene.player.inventory().storage()) if(stack.id==ItemId::DIRT)inventoryDrop=true;
        check(inventoryDrop==enabled,"mined block drop switch ignored");
    }
    for (bool enabled : {false,true}) {
        EntityAiScenarios::Scene scene(assets);
        rule(scene.world,GameRuleId::MobGriefing,GameRuleValue::boolean(enabled));
        scene.add(EntityType::Blastling,{.5,1,.5});scene.player.setPosition({1.5,1,.5});
        for(int frame=0;frame<180 && scene.mobs.takeExplosionEvents().empty();++frame)scene.step(1.0f/60,true);
        check((scene.world.getBlock(0,0,0)==BlockId::AIR)==enabled,"mob explosion block griefing switch ignored");
        check(scene.player.survivalStats().health()<20,"mob griefing switch disabled explosion damage");
    }
    for (bool enabled : {false,true}) {
        EntityAiScenarios::Scene scene(assets);
        rule(scene.world,GameRuleId::TntExplodes,GameRuleValue::boolean(enabled));
        scene.block(0,1,0,BlockId::DIRT); scene.mobs.primeTnt({0,1,0},.05f,false); scene.step(.1f);
        check((scene.world.getBlock(0,1,0)==BlockId::AIR)==enabled,"TNT explosion toggle ignored");
    }
    for (bool enabled : {false,true}) {
        EntityAiScenarios::Scene scene(assets);
        rule(scene.world,GameRuleId::WaterSourceConversion,GameRuleValue::boolean(enabled));
        scene.world.setBlock(0,1,0,BlockId::WATER);scene.world.setBlock(2,1,0,BlockId::WATER);
        scene.world.setBlock(1,1,0,fluidBlockFromAmount(false,6,false));
        for (uint64_t tick=1;tick<30;++tick) scene.world.tickFluids(tick);
        const auto state=decodeFluidState(scene.world.getBlock(1,1,0));
        check(state && state->source==enabled,"water source conversion toggle ignored");
    }
    {
        EntityAiScenarios::Scene scene(assets);
        const auto village=scene.world.locateStructure(StructureType::Village,0,0);
        check(village.has_value(),"population fixture village missing");
        std::vector<WorldGenerator::VillageSpawnRequest> requests;
        std::pair<int,int> key;
        for(int dx=-2;dx<=2 && requests.empty();++dx)for(int dz=-2;dz<=2 && requests.empty();++dz) {
            key={World::worldToChunkX(village->x)+dx,World::worldToChunkZ(village->z)+dz};
            requests=scene.world.villageSpawnsForChunk(key.first,key.second);
        }
        check(!requests.empty(),"population fixture requests missing");
        Chunk* chunk=scene.world.getChunk(key.first,key.second);chunk->generated=true;
        for(const auto& request:requests)scene.block(static_cast<int>(std::floor(request.position.x)),
            static_cast<int>(std::floor(request.position.y))-1,static_cast<int>(std::floor(request.position.z)),BlockId::STONE);
        scene.world.update(requests.front().position,0);
        SaveStore store(root/"population");
        WorldMetadata::PersistedEntity cow; cow.type=static_cast<uint8_t>(EntityType::Cow); cow.health=10;
        cow.position=requests.front().position+glm::dvec3(0,4,0);store.saveChunkEntities(key.first,key.second,{cow});
        scene.mobs.setSaveStore(&store);scene.mobs.setNaturalSpawningEnabled(true);
        rule(scene.world,GameRuleId::SpawnMobs,GameRuleValue::boolean(false));scene.mobs.syncChunks();
        check(scene.mobs.entities().size()==1 && store.loadChunkEntityPopulationVersion(key.first,key.second)==0,
            "disabled population blocks restore or consumes population revision");
        rule(scene.world,GameRuleId::SpawnMobs,GameRuleValue::boolean(true));scene.mobs.syncChunks();
        const size_t populated=scene.mobs.entities().size();
        check(populated>1 && store.loadChunkEntityPopulationVersion(key.first,key.second)==1,"re-enabled village population fails");
        rule(scene.world,GameRuleId::SpawnMobs,GameRuleValue::boolean(false));scene.mobs.syncChunks();
        rule(scene.world,GameRuleId::SpawnMobs,GameRuleValue::boolean(true));scene.mobs.syncChunks();
        check(scene.mobs.entities().size()==populated,"population toggle duplicates restored entities");
    }
    for (int speed : {0,1000}) {
        EntityAiScenarios::Scene scene(assets);
        rule(scene.world,GameRuleId::RandomTickSpeed,GameRuleValue::integer(speed));
        scene.world.setBlock(-1,1,-1,farmlandForMoisture(7));
        scene.world.setBlock(-1,2,-1,BlockId::WHEAT_0);
        scene.world.getChunk(-1,-1)->setSkyLight(15,3,15,15);
        for(uint64_t tick=1;tick<=100;++tick) scene.world.tickSurvival({0,1,0},tick,true);
        const BlockId crop=scene.world.getBlock(-1,2,-1);
        check(speed==0 ? crop==BlockId::WHEAT_0 : crop>BlockId::WHEAT_0,"random tick zero/multiplier or negative section differs");
        rule(scene.world,GameRuleId::RandomTickSpeed,GameRuleValue::integer(INT32_MAX));
        scene.world.tickSurvival({0,1,0},101,true);
    }
    {
        EntityAiScenarios::Scene scene(assets);
        scene.world.setBlock(0,1,0,BlockId::FIRE); scene.world.setBlock(1,1,0,BlockId::PLANKS);
        rule(scene.world,GameRuleId::FireSpreadRadiusAroundPlayer,GameRuleValue::integer(0));
        WeatherSystem weather; weather.reset(42);
        for(uint64_t tick=1;tick<=200;++tick)scene.world.tickWeather(weather,true,tick);
        check(scene.world.getBlock(0,1,0)==BlockId::FIRE && scene.world.getBlock(1,1,0)==BlockId::PLANKS,"zero fire radius still ticks fire");
        rule(scene.world,GameRuleId::FireSpreadRadiusAroundPlayer,GameRuleValue::integer(1));
        scene.world.tickSurvival({1000,1,0},201,false);
        for(uint64_t tick=201;tick<=400;++tick)scene.world.tickWeather(weather,true,tick);
        check(scene.world.getBlock(0,1,0)==BlockId::FIRE,"distant fire still ages");
        rule(scene.world,GameRuleId::FireSpreadRadiusAroundPlayer,GameRuleValue::integer(-1));
        for(uint64_t tick=401;tick<=800;++tick)scene.world.tickWeather(weather,true,tick);
        check(scene.world.getBlock(0,1,0)!=BlockId::FIRE || scene.world.getBlock(1,1,0)!=BlockId::PLANKS,"unlimited fire fails to resume");
    }
    std::filesystem::remove_all(root);
    std::cout<<"GameRule integration tests passed\n";
    return 0;
}
}
