#pragma once
#include "plugins/Runtime.h"
#include "plugins/PluginManager.h"
#include "plugins/ContentQueries.h"
#include "plugins/ContentMutation.h"
#include "network/ContentIds.h"
#include "network/LodProtocol.h"
#include "world/BlockEntityLogic.h"

namespace PluginLanIntegration {
inline void codecs() {
    const auto saved=Plugins::mutableContentState();
    const auto block=Plugins::resolveBlock("official_content:crystal_block");
    const auto item=Plugins::resolveItem("official_content:crystal_block");
    Lan::ChunkSnapshot chunk;chunk.revision=1;chunk.epoch=1;chunk.blocks.assign(Config::CHUNK_VOLUME,static_cast<uint16_t>(block));
    const auto chunkBytes=Lan::encodeChunkSnapshot(chunk);
    Lan::ChunkDelta delta;delta.base=1;delta.revision=2;delta.epoch=1;delta.edits={{3,static_cast<uint16_t>(block)}};
    const auto deltaBytes=Lan::encodeChunkDelta(delta);
    Lan::AuthorityState state;state.self.mainhand={item,4,0};state.self.offhand={item,2,0};state.inventory.slot(0)={item,3,0};
    state.cursor={item,1,0};state.crafting[0]={item,2,0};state.window.kind=InventoryWindowKind::Container;
    state.window.container.emplace();state.window.container->chest[0]={item,5,0};state.window.container->input={item,1,0};
    const auto stateBytes=Lan::encodeAuthorityState(state);
    Lan::EntityBatch batch;EntitySnapshot dropped;dropped.id=1;dropped.type=EntityType::Item;dropped.health=1;dropped.item={item,6,0};batch.entities.push_back(dropped);
    const auto entityBytes=Lan::encodeEntityBatch(batch);
    Lan::GameEvent event;event.block=block;const auto eventBytes=Lan::encodeGameEvent(event);
    Lan::LodUpdate lod;lod.columns.at(0,0).exact=true;lod.columns.at(0,0).spans.push_back({1,2,block});const auto lodBytes=Lan::encodeLodUpdate(lod,true);
    Lan::GameAction action;action.sequence=1;action.epoch=1;action.inventory.operation=InventoryOperation::CreativeGrant;action.inventory.argument=static_cast<uint16_t>(item);
    const auto actionBytes=Lan::encodeGameAction(action);
    Lan::PlayerProfile profile;profile.identity={Lan::randomToken(),Lan::randomToken(),"Palette"};profile.inventory.slot(0)={item,7,0};
    const auto profileBytes=Lan::encodeProfile(profile);
    auto& c=Plugins::mutableContentState();auto blocks=std::move(c.blocks);auto items=std::move(c.items);c.blocks.clear();c.items.clear();
    for(auto& entry:blocks){const auto id=static_cast<uint16_t>(entry.first+100);entry.second.properties.id=static_cast<BlockId>(id);c.blocks.emplace(id,std::move(entry.second));}
    for(auto& entry:items)c.items.emplace(static_cast<uint16_t>(entry.first+200),std::move(entry.second));
    Plugins::freezeContent();const auto mappedBlock=Plugins::resolveBlock("official_content:crystal_block");const auto mappedItem=Plugins::resolveItem("official_content:crystal_block");
    require(Lan::decodeChunkSnapshot(chunkBytes).blocks.front()==static_cast<uint16_t>(mappedBlock),"chunk wire palette maps by key");
    require(Lan::decodeChunkDelta(deltaBytes).edits.front().block==static_cast<uint16_t>(mappedBlock),"delta wire palette maps by key");
    const auto restored=Lan::decodeAuthorityState(stateBytes);
    require(restored.self.mainhand.id==mappedItem&&restored.self.offhand.id==mappedItem&&restored.inventory.slot(0).id==mappedItem&&
        restored.cursor.id==mappedItem&&restored.crafting[0].id==mappedItem&&restored.window.container->chest[0].id==mappedItem&&
        restored.window.container->input.id==mappedItem,"all authority item fields map by key");
    require(Lan::decodeEntityBatch(entityBytes).entities.front().item.id==mappedItem,"entity item maps by key");
    require(Lan::decodeGameEvent(eventBytes).block==mappedBlock,"feedback block maps by key");
    require(Lan::decodeLodUpdate(lodBytes,true).columns.at(0,0).spans.front().block==mappedBlock,"LOD block maps by key");
    require(Lan::decodeGameAction(actionBytes).inventory.argument==static_cast<uint16_t>(mappedItem),"creative item maps by key");
    require(Lan::decodeProfile(profileBytes).inventory.slot(0).id==mappedItem,"guest durable palette maps by key");
    bool rejected=false;try{Lan::decodeBlockId(65535);}catch(const Lan::ProtocolError&){rejected=true;}require(rejected,"unknown wire ID rejected");
    const auto revision=c.revision;c=saved;c.revision=revision;Plugins::freezeContent();
}
inline int run(const std::filesystem::path& assets,const std::filesystem::path& legacy,const std::filesystem::path& modern) {
    const auto root=std::filesystem::temp_directory_path()/("minecraftc-plugin-lan-"+Lan::randomToken());
    std::filesystem::create_directories(root);
    {std::ofstream file(root/"plugins.json");file<<"{\"official_content\":true}";}
    for(const auto& fixture:std::vector<std::pair<std::string,std::filesystem::path>>{{"legacy_fixture",legacy},{"lan_fixture",modern}}) {
        const auto directory=root/"mods"/fixture.first;std::filesystem::create_directories(directory);
        std::filesystem::copy_file(fixture.second,directory/fixture.second.filename());
        std::ofstream manifest(directory/"mod.json");manifest<<"{\"id\":\""<<fixture.first<<"\",\"version\":\"1.0.0\",\"api\":1,\"network_compatibility\":\"fixture-1\",\"native\":{\"linux\":\""<<fixture.second.filename().string()<<"\",\"windows\":\""<<fixture.second.filename().string()<<"\",\"macos\":\""<<fixture.second.filename().string()<<"\"}}";
    }
    std::filesystem::copy(assets.parent_path()/"examples/plugins/recipe_demo", root/"mods/recipe_demo",
                          std::filesystem::copy_options::recursive);
    Plugins::PluginManager plugins({assets,root});plugins.initialize();
    for(const auto& info:plugins.plugins())if(info.manifest.id=="legacy_fixture"||info.manifest.id=="lan_fixture")require(info.active,"old and new native fixtures load");
    codecs();
    const int previousDistance=Config::RENDER_DISTANCE;Config::RENDER_DISTANCE=2;
    {
        ScenarioSession host(root/"host"/"saves"),client(root/"client"/"saves");
        plugins.operations.player=[&](MC_PlayerSnapshot& out){return host.pluginPlayer(out);};
        plugins.operations.players=[&]{return host.pluginPlayers();};
        plugins.operations.playerById=[&](uint64_t id,MC_PlayerSnapshot& out,uint32_t& dimension){return host.pluginPlayerById(id,out,dimension);};
        plugins.operations.giveItem=[&](uint16_t id,uint32_t count){return host.pluginGiveItem(id,count);};
        plugins.operations.getBlock=[&](int32_t x,int32_t y,int32_t z,uint16_t& id){return host.pluginGetBlock(x,y,z,id);};
        plugins.operations.setBlock=[&](int32_t x,int32_t y,int32_t z,uint16_t id){return host.pluginSetBlock(x,y,z,id);};
        Plugins::contextProvider()=[&]{return host.pluginContext();};
        const auto world=host.createWorld("Plugin LAN",42,GameMode::Survival,Difficulty::Normal,true,WorldType::Superflat);
        host.startWorld(world,true,0);SessionScenario::prepareLanScene(host);
        {
            auto& terrain = SessionScenario::world(host);
            auto& entities = SessionScenario::entities(host);
            const glm::ivec3 position{8, 8, 8};
            terrain.setBlock(position.x, position.y, position.z, BlockId::FURNACE);
            auto* furnace = terrain.getBlockEntity(position);
            require(furnace != nullptr, "Recipe furnace placed for destruction test");
            furnace->input = {Plugins::resolveItem("recipe_demo:blank"), 1};
            furnace->fuel = {ItemId::COAL, 1};
            for (int tick=0; tick<100; ++tick) tickFurnace(*furnace);
            const auto xp = [&] {
                uint32_t total = 0;
                for (const auto& entity : entities.entities())
                    if (entity.type==EntityType::ExperienceOrb && entity.health>0)
                        total += entity.experienceOrb.value * entity.experienceOrb.count;
                return total;
            };
            const auto before = xp();
            const auto center = glm::dvec3(position) + glm::dvec3(.5);
            entities.explodeAt(center, 3, {});
            require(terrain.getBlock(position.x, position.y, position.z)==BlockId::AIR &&
                    !terrain.getBlockEntity(position) &&
                    xp()==before+1, "Destroyed plugin furnace did not release banked XP");
            // Remove the paid orb before replaying the explosion, which also damages entities.
            entities.clear();
            entities.explodeAt(center, 3, {});
            require(xp()==0, "Destroyed plugin furnace released XP twice");
        }
        auto ready=Plugins::event(MC_WORLD_READY);plugins.dispatch(ready);
        require(host.openLanRoom(0,2,true),"plugin world opens LAN");
        double now=0;
        for (const char* entry : {"item-group/recipe_demo:stone", "smelting-v1/recipe_demo:tile"}) {
            auto changed = GameSession::lanCompatibility();
            auto value = nlohmann::json::parse(changed.content.at(entry));
            if (std::string_view(entry).starts_with("item-group/")) value.erase(0);
            else value.back() = 500;
            changed.content.at(entry) = value.dump();
            changed.contentSignature = Lan::contentSignature(changed.content);
            Lan::Client incompatible;
            require(incompatible.join("::1", host.lanPort(), changed,
                {Lan::randomToken(), Lan::randomToken(), "Recipe mismatch"}, now), "Recipe mismatch client starts");
            for (int i=0; i<1000 && incompatible.state()!=Lan::Client::State::Failed; ++i) {
                now += .001;
                host.pollLan(now);
                incompatible.poll(now);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            require(incompatible.state()==Lan::Client::State::Failed &&
                    incompatible.error().find(entry)!=std::string::npos,
                    ("Expected mismatch rejection for " + std::string(entry) + ", got: " + incompatible.error()).c_str());
        }
        client.configureLod({false,16,LodAggressiveness::PowerSaver,LodPrecision::Low});
        require(client.joinLanRoom("::1",host.lanPort(),now),"plugin-enabled client joins");
        auto pump=[&]{now+=.005;host.pollLan(now);client.pollLan(now);SessionScenario::world(client).processCompletedGenerations();std::this_thread::sleep_for(std::chrono::milliseconds(1));};
        for(int i=0;i<500&&!client.lanWorldReady();++i)pump();
        require(client.lanWorldReady(),"plugin bootstrap accepted");
        auto& guest=SessionScenario::guest(host,SessionScenario::replicaPeer(client));
        {
            SessionScenario::tickLanPlayers(host, .01f);
            require(!guest.loading, "LAN crafting guest has loaded terrain");
            const auto blank = Plugins::resolveItem("recipe_demo:blank");
            const auto& recipes = craftingRecipes();
            const auto recipe = std::find_if(recipes.begin(), recipes.end(), [&](const auto& r) {
                return r.output.id == blank;
            });
            require(recipe != recipes.end(), "LAN recipe index available");
            guest.player.inventory().slot(0) = {ItemId::DIORITE, 1};
            guest.player.inventory().slot(1) = {ItemId::FLINT, 1};
            guest.lastStateSent = -1;
            for (int i=0; i<20; ++i) pump();
            client.openInventoryWindow(InventoryWindowKind::Player);
            InventoryAction fill;
            fill.operation = InventoryOperation::FillRecipe;
            fill.argument = static_cast<uint16_t>(recipe - recipes.begin());
            client.submitInventoryAction(fill);
            for (int i=0; i<100 && client.inventoryWindowPending(); ++i) pump();
            require(client.inventoryWindow().kind == InventoryWindowKind::Player &&
                    guest.window.crafting()[0].id == ItemId::DIORITE &&
                    client.inventoryWindow().crafting == guest.window.crafting(), "LAN recipe fill did not sync");
            InventoryAction craft;
            craft.operation = InventoryOperation::QuickMove;
            craft.slot = {InventoryArea::Output, 0};
            client.submitInventoryAction(craft);
            client.closeInventoryWindow();
            for (int i=0; i<100 && client.inventoryWindowPending(); ++i) pump();
            require(guest.player.inventory().count(blank) == 1 && client.inventory().count(blank) == 1 &&
                    host.inventory().count(blank) == 0, "LAN authority did not craft for the guest");
        }
        const auto item=Plugins::resolveItem("official_content:crystal");const auto block=Plugins::resolveBlock("official_content:crystal_block");
        guest.player.inventory().slot(0)={item,1,0};guest.player.setPosition({.5,1.01,.5});guest.player.setOrientation(0,0);
        SessionScenario::world(host).setBlock(0,2,2,block);
        for(int i=0;i<100&&client.worldState().getBlock(0,2,2)!=block;++i)pump();
        require(client.worldState().getBlock(0,2,2)==block,"plugin block delta reaches replica");
        const auto hostDiamonds=host.inventory().count(ItemId::DIAMOND);
        for(int i=0;i<100&&client.playerState().activeItem().id!=item;++i)pump();
        require(client.playerState().activeItem().id==item,"plugin inventory reaches replica");
        client.setLocalControl(true);
        client.handleMouseButton(MouseButton::Right,ButtonAction::Press); // forwarded intent, no client gameplay callback
        for(int i=0;i<20;++i)pump();
        SessionScenario::pluginHostLoading(host,true);
        SessionScenario::tickLanPlayers(host,.05f);
        require(guest.player.inventory().count(ItemId::DIAMOND)==0,"legacy plugin rewards remain queued");
        plugins.flushOperations();
        SessionScenario::pluginHostLoading(host,false);
        require(guest.player.inventory().count(ItemId::DIAMOND)==3&&host.inventory().count(ItemId::DIAMOND)==hostDiamonds,"old ABI callback gives to guest, never host");
        require(SessionScenario::world(host).getBlock(0,2,2)==block,"cancelled guest use leaves terrain unchanged");
        client.handleMouseButton(MouseButton::Right,ButtonAction::Release);
        require(client.sendPluginCommand("/legacy_fixture:gift"),"old host-only command can be requested");
        for(int i=0;i<30;++i)pump();
        plugins.flushOperations();
        require(guest.player.inventory().count(ItemId::DIAMOND)==3,"old commands reject guest invocation");
        require(client.sendPluginCommand("/lan_fixture:gift"),"opt-in guest command requested");
        for(int i=0;i<30;++i)pump();
        plugins.flushOperations();
        for(int i=0;i<20;++i)pump();
        require(guest.player.inventory().count(ItemId::EMERALD)==2&&client.inventory().count(ItemId::EMERALD)==2,"opt-in command targets and syncs guest");
        // Explicit client role must suppress gameplay dispatch and world mutations.
        auto replicaUse=Plugins::event(MC_USE_PRE);replicaUse.role=MC_CLIENT;replicaUse.player_id=SessionScenario::replicaPeer(client);replicaUse.item=static_cast<uint16_t>(item);
        plugins.dispatch(replicaUse);plugins.flushOperations();require(!replicaUse.cancelled&&guest.player.inventory().count(ItemId::DIAMOND)==3,"replica does not run gameplay callbacks");
        auto& heaven=SessionScenario::pluginDimension(host,DimensionId::Heaven);
        heaven.setBlock(0,2,3,BlockId::STONE);
        const auto beforeHostBlock=host.worldState().getBlock(0,2,3);
        const Plugins::ActorContext queuedActor{SessionScenario::replicaPeer(client),1,MC_HOST};
        const auto provider=Plugins::dispatcher();
        bool dimensionRead=false;
        Plugins::dispatcher()=[&](MC_Event& event){
            const Plugins::ActorScope scope({event.player_id,event.dimension,event.role});
            provider(event);
            if(event.kind==MC_DAMAGE_PRE) {
                uint16_t id=0;dimensionRead=host.pluginGetBlock(0,2,3,id)&&id==static_cast<uint16_t>(BlockId::STONE);
                event.damage=1;
            }
        };
        guest.profile.dimension=DimensionId::Heaven;
        guest.player.bindWorld(heaven,nullptr);
        guest.player.configureRules(GameMode::Survival,Difficulty::Normal);
        guest.player.takeDamage(2,true);
        require(dimensionRead&&guest.player.survivalStats().health()==19,"damage callback uses injured guest and its dimension");
        {
            Plugins::ActorScope scope(queuedActor);
            require(plugins.command("/lan_fixture:dimension"),"dimension-targeted command runs");
        }
        require(heaven.getBlock(0,2,3)==BlockId::STONE,"dimension mutation remains queued");
        Plugins::dispatcher()=provider;
        guest.profile.dimension=DimensionId::Overworld;guest.player.bindWorld(SessionScenario::world(host),nullptr);
        plugins.flushOperations();
        require(heaven.getBlock(0,2,3)==block&&host.worldState().getBlock(0,2,3)==beforeHostBlock,"queued mutation keeps original dimension after actor travels");
        // Close triggers a queued gift for the departing player; flushing cannot redirect it.
        const auto hostEmeralds=host.inventory().count(ItemId::EMERALD);SessionScenario::disconnectReplica(client);
        for(int i=0;i<50&&host.lanGuestCount();++i)pump();
        plugins.flushOperations();
        require(!host.lanGuestCount()&&host.inventory().count(ItemId::EMERALD)==hostEmeralds,"departed actor operations are rejected without host fallback");
        require(client.joinLanRoom("::1",host.lanPort(),now),"plugin guest reconnects");
        for(int i=0;i<500&&(!client.lanWorldReady()||!host.lanGuestCount());++i)pump();
        require(host.lanGuestCount()==1,"guest profile reconnects after palette save");
        const auto savedLevel=root/"host"/"saves"/world/"level.bin";
        auto bytes=[](const auto& path){std::ifstream file(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(file),{});};
        const auto beforeFault=bytes(savedLevel);
        auto& faultGuest=SessionScenario::guest(host,SessionScenario::replicaPeer(client));
        const auto savedProfile=root/"host"/"saves"/world/"players"/(faultGuest.profile.identity.id+".lanplayer");
        const auto profileBeforeFault=bytes(savedProfile);
        require(!profileBeforeFault.empty(),"guest profile exists before fault");
        require(plugins.command("/legacy_fixture:gift"),"operation queued before fault");
        require(plugins.command("/lan_fixture:fault"),"faulting command found");
        require(!Plugins::runtimeFault().empty(),"callback failure marks runtime fault");
        faultGuest.player.inventory().slot(0)={ItemId::DIAMOND,64,0};
        host.closeLanRoom();
        require(bytes(savedProfile)==profileBeforeFault,"faulted departure does not overwrite guest profile");
        plugins.flushOperations();host.abortPluginWorld();
        for(int i=0;i<50&&!client.lanConnectionFailed();++i){now+=.005;client.pollLan(now);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        require(client.lanConnectionFailed()&&client.lanError().find("Plugin command failed")!=std::string::npos,("host fault ends guest session with reason: "+client.lanError()+" joining="+std::to_string(client.joiningLan())).c_str());
        require(bytes(savedLevel)==beforeFault&&!host.hasWorldStore(),"faulted world is detached without metadata save");
        SessionScenario::disconnectReplica(client);Plugins::contextProvider()={};plugins.operations={};
    }
    plugins.shutdown();std::filesystem::remove_all(root);Config::RENDER_DISTANCE=previousDistance;
    std::cout<<"Plugin LAN codec, old ABI, authority and guest permission regressions passed\n";return 0;
}
}
