#pragma once
#include "game/ExperienceRewards.h"
#include "network/StateProtocol.h"
#include "world/BlockEntityLogic.h"
#include <limits>

namespace ExperienceIntegration {
static_assert(static_cast<uint16_t>(EntityType::ExperienceOrb)==29,"Experience appends a stable builtin entity identity");
inline uint64_t orbPoints(const EntityManager& entities) {
    uint64_t result=0;
    for(const auto& e:entities.entities())if(e.type==EntityType::ExperienceOrb&&e.health>0)
        result+=uint64_t(e.experienceOrb.value)*e.experienceOrb.count;
    return result;
}
inline int run(const std::filesystem::path& assets) {
    const char* stage="orbs and mining";
    try {
    using EntityAiScenarios::Scene;
    using EntityAiScenarios::PresentationMode;
    const ItemStack pick{ItemId::DIAMOND_PICKAXE,1,0};
    require(miningExperience(BlockId::DIAMOND_ORE,{},4)==0,"wrong mining tool gives no XP");
    for(uint32_t i=0;i<100;++i) {
        require(miningExperience(BlockId::DIAMOND_ORE,pick,i)>=3&&miningExperience(BlockId::DIAMOND_ORE,pick,i)<=7,"diamond mining XP range");
        require(miningExperience(BlockId::IRON_ORE,pick,i)==0,"raw ore mining gives no XP");
        require(miningExperience(BlockId::NETHER_QUARTZ_ORE,pick,i)>=2,"quartz mining XP");
    }
    {
        Scene scene(assets,PresentationMode::Unset);
        scene.mobs.spawnExperience({.5,1.1,.5},100);
        scene.mobs.spawnExperience({.5,1.1,.5},100);
        require(orbPoints(scene.mobs)==200,"merged orb denominations conserve rewards");
        auto records=scene.mobs.saveEntities();scene.mobs.clear();scene.mobs.loadEntities(records);
        require(orbPoints(scene.mobs)==200,"durable entity roundtrip preserves orb counts");
        Player other(scene.world);other.configureRules(GameMode::Survival,Difficulty::Normal);other.setPosition(scene.player.getPosition());
        for(int i=0;i<600;++i)scene.mobs.update({{1,&scene.player,false,true},{2,&other,false,true}},.02f,false,false);
        require(scene.player.experience().level()==11&&scene.player.experience().points()==13&&other.experience().level()==0,
            "two-player pickup is authority-owned and never duplicated");
        require(orbPoints(scene.mobs)==0,"picked-up orb removed");
    }
    {
        Scene scene(assets,PresentationMode::Unset);
        scene.player.configureRules(GameMode::Spectator,Difficulty::Normal);
        scene.mobs.spawnExperience({.5,1.1,.5},7);
        const auto start=scene.mobs.entities().front().position;
        for(int i=0;i<20;++i)scene.mobs.update(scene.player,.02f,false,false,false,true);
        require(scene.player.experience().level()==0&&orbPoints(scene.mobs)==7,"spectators cannot pick up XP");
        auto records=scene.mobs.saveEntities();records.front().ageSeconds=299.99f;
        scene.mobs.clear();scene.mobs.loadEntities(records);scene.step(.02f);
        require(orbPoints(scene.mobs)==0,"orb loaded age expires at five minutes");
        (void)start;
    }
    {
        Scene scene(assets,PresentationMode::Unset);
        scene.add(EntityType::Cow,{.5,1,-1.5});
        MeleeAttackRequest hit;hit.damage=100;hit.reach=3;
        require(scene.mobs.attackRay({.5,1.6,.5},{0,0,-1},hit).primaryDamaged,"player kill fixture hits");
        scene.step(.02f);require(orbPoints(scene.mobs)>=1&&orbPoints(scene.mobs)<=3,"player kill drops animal XP");
        scene.step(.02f);require(orbPoints(scene.mobs)<=3,"death processed only once");
    }
    {
        Scene scene(assets,PresentationMode::Unset);scene.add(EntityType::Cow,{.5,1,.5});
        scene.mobs.primeTnt({0,1,0},.05f,false,true);
        auto saved=scene.mobs.saveEntities();scene.mobs.clear();scene.mobs.loadEntities(saved);
        scene.step(.1f);require(orbPoints(scene.mobs)>0,"player-ignited TNT retains XP credit after save/reload");
    }
    stage="pickup rates";
    for(int fps:{30,60,144}) {
        Scene scene(assets,PresentationMode::Unset);scene.mobs.spawnExperience({.5,1.2,.5},1);
        auto records=scene.mobs.saveEntities();records.front().experienceOrb.count=100;records.front().velocity={0,0,0};
        scene.mobs.clear();scene.mobs.loadEntities(records);
        for(int frame=0;frame<fps*2;++frame)scene.mobs.update(scene.player,1.f/fps,false,false,false,true);
        require(scene.player.experience().level()==2&&scene.player.experience().points()==4,("XP pickup rate: fps="+std::to_string(fps)+" level="+std::to_string(scene.player.experience().level())+" points="+std::to_string(scene.player.experience().points())).c_str());
    }
    {
        stage="trading";Scene scene(assets,PresentationMode::Unset);scene.add(EntityType::Villager,{.5,1,-1.5});
        auto records=scene.mobs.saveEntities();records.front().villager.profession=VillagerProfession::Farmer;records.front().villager.experience=9;
        scene.mobs.clear();scene.mobs.loadEntities(records);scene.player.inventory().slot(0)={ItemId::WHEAT,64,0};
        require(scene.mobs.tradeWith(scene.mobs.entities().front().id,0,scene.player.inventory())==TradeResult::Success,"XP trade fixture succeeds");
        require(orbPoints(scene.mobs)>=8&&orbPoints(scene.mobs)<=11,"level-up trade XP is separate from villager XP");
        const auto before=orbPoints(scene.mobs);scene.player.inventory().clear();
        require(scene.mobs.tradeWith(scene.mobs.entities().front().id,0,scene.player.inventory())!=TradeResult::Success&&orbPoints(scene.mobs)==before,"failed trade gives no XP");
        stage="smelting";BlockEntity furnace;furnace.type=BlockEntityType::Furnace;furnace.input={ItemId::RAW_IRON,1,0};furnace.fuel={ItemId::COAL,1,0};
        for(int tick=0;tick<200;++tick)tickFurnace(furnace);
        require(furnace.output.id==ItemId::IRON_INGOT&&furnace.furnaceExperience.size()==1&&furnace.furnaceExperience.begin()->second==700,"smelting banks XP only for completed recipes");
    }
    stage="kill attribution";
    for(bool recent:{false,true}) {
        Scene scene(assets,PresentationMode::Unset);scene.add(EntityType::Cow,{.5,1,-1.5});
        MeleeAttackRequest hit;hit.damage=1;hit.reach=3;
        require(scene.mobs.attackRay({.5,1.6,.5},{0,0,-1},hit).primaryDamaged,"kill attribution fixture hits");
        if(!recent)for(int i=0;i<260;++i)scene.step(.02f);
        const auto position=scene.mobs.entities().front().position;
        scene.mobs.explodeAt(position,3,{});scene.step(.02f);
        require((orbPoints(scene.mobs)>0)==recent,"environmental death respects five-second player credit");
    }
    {
        Scene scene(assets,PresentationMode::Unset);
        scene.mobs.settleFurnaceExperience({.5,1.1,.5},{{"minecraftc:test",2000}});
        require(orbPoints(scene.mobs)==2,"furnace whole experience settles exactly");
        scene.mobs.awardMiningExperience({2,1,2},BlockId::DIAMOND_ORE,pick);
        require(orbPoints(scene.mobs)>=5&&orbPoints(scene.mobs)<=9,"mining generates physical orbs");
    }
    {
        stage="session death and commands";const auto root=std::filesystem::temp_directory_path()/("minecraftc-experience-session-"+Lan::randomToken());
        GameSession session(root/"saves");GameSessionTestAccess::flatRuleScene(session);
        auto& player=GameSessionTestAccess::player(session);auto& entities=GameSessionTestAccess::entities(session);
        Localization localization;
        GameSessionTestAccess::setCheats(session,false);runCommand(session,localization,"/xp add @s 7");
        require(player.experience().level()==0,"XP commands require cheats");
        GameSessionTestAccess::setCheats(session,true);runCommand(session,localization,"/xp add @s 7");
        require(player.experience().level()==1,"XP command reaches authority player");
        runCommand(session,localization,"/xp set @s 9 points");require(player.experience().points()==0,"invalid set points leaves authority unchanged");
        runCommand(session,localization,"/xp add @s -7");
        player.experience().addPoints(1395);GameSessionTestAccess::die(session);
        require(player.experience().level()==0&&orbPoints(entities)==100,"death drops capped XP and clears player");
        entities.clear();player.experience().addPoints(352);GameRuleSet rules;rules.set(GameRuleId::KeepInventory,GameRuleValue::boolean(true));
        GameSessionTestAccess::setRules(session,rules);GameSessionTestAccess::die(session);
        require(player.experience().level()==16&&orbPoints(entities)==0,"keepInventory retains XP without dropping");
        std::filesystem::remove_all(root);
    }
    {
        stage="state protocol";Lan::AuthorityState state;state.experience.addPoints(1628);state.experiencePickups=42;state.experienceLevelSounds=3;
        auto decoded=Lan::decodeAuthorityState(Lan::encodeAuthorityState(state));
        require(decoded.experience.level()==32&&decoded.experiencePickups==42,"owner state carries XP and feedback sequences");
        stage="entity protocol";Lan::EntityBatch batch;EntitySnapshot orb;orb.id=1;orb.type=EntityType::ExperienceOrb;orb.health=5;orb.experienceOrb={7,4};batch.entities.push_back(orb);
        const auto roundtrip=Lan::decodeEntityBatch(Lan::encodeEntityBatch(batch));
        require(roundtrip.entities.front().experienceOrb.count==4,"orb network count roundtrip");
        batch.entities.front().experienceOrb.count=0;bool rejected=false;
        try{(void)Lan::encodeEntityBatch(batch);}catch(const Lan::ProtocolError&){rejected=true;}
        require(rejected,"zero-count orb packet rejected");
    }
    std::cout<<"Experience integration tests passed\n";return 0;
    }catch(const std::exception& error){throw std::runtime_error(std::string(stage)+": "+error.what());}
}
}
