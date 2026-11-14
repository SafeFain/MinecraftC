#pragma once
#include "game/EnchantingRules.h"
#include "game/FallingAnvilState.h"
#include "game/EnchantmentHitContext.h"

namespace EnchantmentIntegration {
inline int run(const std::filesystem::path& assets) {
    using EntityAiScenarios::Scene;
    using EntityAiScenarios::PresentationMode;
    {
        Scene scene(assets,PresentationMode::Unset);
        WorldMetadata::PersistedEntity source;source.type=static_cast<uint16_t>(EntityType::Zombie);
        source.health=20;source.position={3.5,1,.5};source.pluginState=EnchantmentHitContext::write("{}","local",5);
        scene.mobs.loadEntities({source});
        const auto saved=scene.mobs.saveEntities();scene.mobs.clear();scene.mobs.loadEntities(saved);
        const auto hit=EnchantmentHitContext::read(scene.mobs.saveEntities()[0].pluginState);
        require(hit&&hit->owner=="local"&&hit->remaining==5,"Recent enchantment attribution survives entity save/reload");
        auto state=source.pluginState;EnchantmentHitContext::tick(state,5.1f);
        require(!EnchantmentHitContext::read(state),"Reloaded hit attribution has a bounded lifetime");
        bool refused=false;try {EnchantmentHitContext::read("{\"minecraftc:enchantment_hit\":{\"owner\":\"local\",\"remaining\":9}}");}catch(const std::runtime_error&){refused=true;}
        require(refused,"Invalid durable attribution is rejected");
    }

    {
        const auto deathLoot = [&](uint8_t level, uint32_t seed, const std::string& owner, EntityType type) {
            Scene scene(assets,PresentationMode::Unset);
            scene.player.inventory().slot(0)={ItemId::IRON_SWORD,1};
            scene.player.inventory().slot(0).components.enchantments.set(EnchantmentId::Looting,level);
            WorldMetadata::PersistedEntity source;source.type=static_cast<uint16_t>(type);
            source.position={3.5,1,.5};source.health=20;source.behaviorSeed=seed;
            source.pluginState=EnchantmentHitContext::write("{}",owner,5);
            scene.mobs.loadEntities({source});auto saved=scene.mobs.saveEntities();scene.mobs.clear();
            saved[0].health=0;scene.mobs.loadEntities(saved);scene.step(.01f);
            uint32_t count=0;for(const auto& entity:scene.mobs.entities())if(entity.type==EntityType::Item)count+=entity.item.count;
            return count;
        };
        bool gained=false;
        for(uint32_t seed=0;seed<8;++seed) {
            const auto ordinary=deathLoot(0,seed,"local",EntityType::Zombie);
            const auto boosted=deathLoot(3,seed,"local",EntityType::Zombie);
            require(boosted>=ordinary,"Reloaded Looting reads the current equipped sword");gained|=boosted>ordinary;
            require(deathLoot(3,seed,std::string(32,'a'),EntityType::Zombie)==ordinary,"Disconnected owner grants no current-hand Looting bonus");
        }
        require(gained,"Durable hit attribution actually increases eligible mob drops");
        require(deathLoot(3,42,"local",EntityType::IronGolem)==deathLoot(0,42,"local",EntityType::IronGolem),"Looting does not increase iron golem iron");
    }
    {
        Scene scene(assets,PresentationMode::Unset);
        scene.player.inventory().slot(0)={ItemId::IRON_PICKAXE,1,30};
        scene.player.inventory().slot(0).components.enchantments.set(EnchantmentId::Mending,1);
        scene.mobs.spawnExperience({.5,1.1,.5},7);
        for(int i=0;i<30;++i)scene.mobs.update(scene.player,.02f,false,false,false,true);
        require(scene.player.inventory().slot(0).damage==16&&scene.player.experience().level()==0&&scene.player.experience().points()==0,"Actual orb pickup repairs held item instead of adding levels");
        scene.player.experience().addPoints(7);
        require(scene.player.inventory().slot(0).damage==16,"Direct XP grants do not mend");
    }
    {
        Scene scene(assets,PresentationMode::Unset);
        scene.world.setBlock(3,6,0,BlockId::ANVIL);
        scene.player.setPosition({3.5,1,.5});
        for(int i=0;i<2;++i)scene.world.tickInteractiveBlocks([](const glm::ivec3&){return false;});
        scene.mobs.update(scene.player,.05f,false,false,false,false);
        require(scene.world.getBlock(3,6,0)==BlockId::AIR&&!scene.mobs.entities().empty(),"Unsupported anvil becomes an authority-owned falling entity");
        auto snapshot=scene.mobs.saveEntities();scene.mobs.clear();scene.mobs.loadEntities(snapshot);
        require(FallingAnvilState::decode(scene.mobs.entities()[0].pluginState).has_value(),"Falling state survives entity reload");
        for(int i=0;i<100;++i)scene.mobs.update(scene.player,.025f,false,false,false,false);
        require(isAnvil(scene.world.getBlock(3,1,0)),"Falling anvil lands as a block");
        require(scene.player.survivalStats().health()<20,"Falling anvil damages an overlapping player");
    }
    {
        Scene scene(assets,PresentationMode::Unset);
        scene.player.setEntityManager(&scene.mobs);
        for(int x=1;x<=3;++x)scene.world.setBlock(x,0,0,BlockId::WATER);
        scene.player.inventory().armor()[3]={ItemId::IRON_BOOTS,1};
        scene.player.inventory().armor()[3].components.enchantments.set(EnchantmentId::FrostWalker,2);
        for(int i=0;i<5;++i)scene.player.update(.05f);
        require(scene.world.getBlock(1,0,0)==BlockId::FROSTED_ICE,"Grounded Frost Walker freezes loaded source water");
        const auto timers=scene.world.getBlockEntity({1,0,0});
        require(timers&&timers->type==BlockEntityType::Resource&&timers->resourceRemaining>0,"Frosted ice has a durable melt timer");
        scene.player.inventory().armor()[3].clear();
        for(int i=0;i<121;++i)scene.world.tickInteractiveBlocks([](const glm::ivec3&){return false;});
        require(scene.world.getBlock(1,0,0)==BlockId::WATER,"Temporary ice melts back to water");
    }
    {
        const auto root=std::filesystem::temp_directory_path()/"minecraftc-enchanting-integration";
        ScenarioSession session(root);SessionScenario::flatRuleScene(session);
        auto& world=SessionScenario::world(session);auto& player=SessionScenario::player(session);
        player.setOrientation(180,0);
        const glm::ivec3 station{0,2,-2};world.setBlock(0,1,-2,BlockId::STONE);world.setBlock(0,2,-2,BlockId::ENCHANTING_TABLE);
        unsigned shelves=0;
        for(int x=-2;x<=2;++x)for(int z=-2;z<=2;++z)if(std::abs(x)==2||std::abs(z)==2)for(int y=0;y<2;++y)if(!(x==0&&z==2)&&shelves<15){world.setBlock(station.x+x,station.y+y,station.z+z,BlockId::BOOKSHELF);++shelves;}
        player.experience().setLevel(30);player.inventory().slot(0)={ItemId::IRON_PICKAXE,1};player.inventory().slot(1)={ItemId::LAPIS_LAZULI,3};
        session.openInventoryWindow(InventoryWindowKind::Container,station);
        const auto move=[&](InventoryArea area,uint8_t index) {InventoryAction action;action.slot={area,index};session.submitInventoryAction(action);};
        move(InventoryArea::Storage,0);move(InventoryArea::Container,0);move(InventoryArea::Storage,1);move(InventoryArea::Container,1);
        auto view=session.inventoryWindow();require(view.container&&view.container->offerRequired[2]==30,"Actual authority window counts fifteen unblocked shelves");
        world.setBlock(-1,2,-2,BlockId::STONE);
        require(session.inventoryWindow().container->offerRequired[2]<30,"Blocking a bookshelf gap reduces live enchanting power");
        world.setBlock(-1,2,-2,BlockId::AIR);
        InventoryAction enchant;enchant.operation=InventoryOperation::Enchant;enchant.argument=2;session.submitInventoryAction(enchant);
        view=session.inventoryWindow();require(view.container&&!view.container->chest[0].components.enchantments.empty()&&player.experience().level()==27,"Live station executes authoritative enchant transaction");
        const auto result=view.container->chest[0];
        world.setBlock(station.x,station.y,station.z,BlockId::AIR);
        require(session.inventoryWindow().kind==InventoryWindowKind::Closed,"Destroying a workstation closes its window");
        bool returned=false;for(const auto& item:player.inventory().storage())if(item==result)returned=true;
        require(returned,"Destroyed station returns exactly the enchanted input");
    }
    std::cout<<"Equipment progression integration passed\n";return 0;
}
}
