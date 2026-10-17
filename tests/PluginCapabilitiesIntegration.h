#pragma once
#include "minecraftc/plugin.hpp"
#include "plugins/PluginManager.h"
#include "plugins/EnvironmentPresentation.h"
#include "player/PluginLanding.h"
#include "network/StateProtocol.h"
#include "world/ChunkStore.h"
#include "core/AssetStore.h"
#include <fstream>
#include <future>

namespace PluginCapabilitiesIntegration {
inline int run(const std::filesystem::path& assets,const std::filesystem::path& module){
    auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    const auto root=std::filesystem::temp_directory_path()/"minecraftc-plugin-capability-tests";
    std::filesystem::remove_all(root);std::filesystem::create_directories(root/"mods");
    const auto example=assets.parent_path()/"examples/plugins";
    std::filesystem::copy(example/"capability_demo",root/"mods/capability_demo",std::filesystem::copy_options::recursive);
    std::filesystem::copy(example/"capability_native",root/"mods/capability_native",std::filesystem::copy_options::recursive);
    std::filesystem::copy_file(module,root/"mods/capability_native"/module.filename(),std::filesystem::copy_options::overwrite_existing);
    {std::ofstream f(root/"mods/capability_native/mod.json");f<<"{\"id\":\"capability_native\",\"api\":1,\"version\":\"1.0.0\",\"native\":{\"linux\":\""<<module.filename().string()<<"\",\"windows\":\""<<module.filename().string()<<"\",\"macos\":\""<<module.filename().string()<<"\"}}";}
    {std::ofstream f(root/"plugins.json");f<<R"({"capability_demo":true})";}
    std::filesystem::create_directories(root/"mods/capability_bad");
    {std::ofstream f(root/"mods/capability_bad/mod.json");f<<R"({"id":"capability_bad","version":"1.0.0","api":1,"data":"content.json"})";}
    {std::ofstream f(root/"mods/capability_bad/content.json");f<<R"({"player_actions":[{"key":"capability_bad:partial","range":32}],"loot_tables":[{"key":"capability_bad:bad","revision":"1","entries":[{"item":"minecraftc:coal","minimum":4,"maximum":2}]}]})";}
    AssetStore rootedAssets(assets);Plugins::PluginManager plugins({assets,root});plugins.initialize();
    size_t index=0;bool found=false;
    for(size_t i=0;i<plugins.plugins().size();++i){const auto& p=plugins.plugins()[i];if(p.manifest.id=="capability_native"){check(p.active,p.status.c_str());index=i;found=true;}if(p.manifest.id=="capability_demo")check(p.active,p.status.c_str());if(p.manifest.id=="capability_bad")check(!p.active,"invalid loot rejects transaction");}
    check(found&&!Plugins::content().extensions.actions.count("capability_bad:partial"),"capability rollback");
    MC_PluginContext context{};context.manager=&plugins;context.index=index;
    const minecraftc::Host host(&plugins.host(),&context);
    check(host.behaviors()&&host.structures()&&host.playerActions()&&host.environment()&&host.presentation(),"five extension tables");
    check(host.extension(MC_STRUCTURES_EXTENSION,2)&&!host.extension(MC_STRUCTURES_EXTENSION,3),"structure V2 and unknown extension version");
    const auto vine=Plugins::resolveBlock("capability_demo:vine"),resource=Plugins::resolveBlock("capability_demo:resource"),spent=Plugins::resolveBlock("capability_demo:spent");
    check(Plugins::blockBehavior(vine)->climbable&&isReplaceableByFluid(vine)&&fireEncouragement(vine)==30&&burnOdds(vine)==60,"block physical/fire behavior");
    const auto dim=static_cast<DimensionId>(Plugins::resolveDimension("capability_demo:islands"));
    WorldGenerator g(17,WorldType::Normal,dim),same(17,WorldType::Normal,dim),other(18,WorldType::Normal,dim);
    const auto located=g.locatePluginStructure("capability_demo:bridge",-130,-130,512);
    check(bool(located),"negative coordinate structure locate");const auto repeated=same.locatePluginStructure("capability_demo:bridge",-130,-130,512),different=other.locatePluginStructure("capability_demo:bridge",-130,-130,512);
    check(repeated&&located->variant==repeated->variant&&located->anchor[0]==repeated->anchor[0],"same seed structure determinism");check(different&&different->variant!=located->variant,"structure seed variation");
    const int cx=World::worldToChunkX(located->anchor[0]),cz=World::worldToChunkZ(located->anchor[2]);std::vector<std::unique_ptr<Chunk>> owned;std::vector<Chunk*> chunks;
    for(int z=cz-1;z<=cz+1;++z)for(int x=cx-1;x<=cx+1;++x){owned.push_back(std::make_unique<Chunk>(x,z));chunks.push_back(owned.back().get());}
    std::vector<RegionGenerationData::PendingBlock> pending;g.generateRegion(cx-1,cz-1,3,6,chunks,pending);
    for(auto it=chunks.rbegin();it!=chunks.rend();++it){auto singleton=std::make_unique<Chunk>((*it)->cx,(*it)->cz);g.generate(*singleton);std::vector<uint16_t> a,b;singleton->copyRawBlocks(a);(*it)->copyRawBlocks(b);check(a==b,"reverse-order region/singleton equivalence");}
    const auto blocks=g.pluginStructureBlocks(located->anchor[0]-18,located->anchor[2]-2,37,5);std::set<int> crossing;std::optional<RegionGenerationData::PendingBlock> chest;
    for(const auto& b:blocks){crossing.insert(World::worldToChunkX(b.worldX));if(!b.lootKey.empty())chest=b;}
    check(crossing.size()>1&&chest&&chest->lootRevision=="1"&&chest->structureKey=="capability_demo:bridge","cross-chunk metadata");
    WorldGenerator native(17,WorldType::Normal,static_cast<DimensionId>(Plugins::resolveDimension("capability_native:flat")));
    const auto n=native.locatePluginStructure("capability_native:bridge",0,0,512);check(bool(n),"native structure locate");const auto nb=native.pluginStructureBlocks(n->anchor[0]-17,n->anchor[2],35,1);check(nb.size()==36,"native pure build and loot");
    const auto async=std::async(std::launch::async,[&]{return g.pluginStructureBlocks(located->anchor[0]-18,located->anchor[2]-2,37,5);}).get();check(async.size()==blocks.size()&&async.back().lootSeed==blocks.back().lootSeed,"worker structure seed equivalence");
    ChunkStore store;store.get(0,0)->generated=true;WorldPersistence persistence(store);
    const uint32_t local=static_cast<uint32_t>(Config::worldYToStorageY(66)*256);
    store.withUnique([&](ChunkStore&){persistence.registerGeneratedBlockEntityUnlocked(0,0,local,BlockId::CHEST,StructureLootProfile::None,chest->lootSeed,&*chest);});
    const glm::ivec3 position(0,66,0);auto* entity=persistence.getBlockEntity(position);check(entity&&entity->lootInitialized&&entity->lootKey==chest->lootKey,"chest initializes keyed loot");bool coal=false;for(const auto& s:entity->chest)coal=coal||(s.id==ItemId::COAL&&s.count>=2);check(coal,"deterministic loot populated");entity->chest={};
    store.withUnique([&](ChunkStore&){persistence.registerGeneratedBlockEntityUnlocked(0,0,local,BlockId::CHEST,StructureLootProfile::None,123,&*chest);});check(persistence.getBlockEntity(position)->chest[0].empty(),"duplicate generation does not refill chest");
    SaveStore saves(root/"world");saves.saveBlockEntities(0,0,{{local,*entity}});const auto restored=saves.loadBlockEntities(0,0);check(restored[0].value.lootInitialized&&restored[0].value.lootSeed==chest->lootSeed&&restored[0].value.chest[0].empty(),"loot metadata roundtrip without refill");
    BlockEntity resourceEntity;resourceEntity.type=BlockEntityType::Resource;resourceEntity.resourceRemaining=73;resourceEntity.resourceBlock=static_cast<uint16_t>(resource);
    saves.saveBlockEntities(1,0,{{local,resourceEntity}});const auto savedResource=saves.loadBlockEntities(1,0);check(savedResource[0].value.resourceRemaining==73&&savedResource[0].value.resourceBlock==static_cast<uint16_t>(resource),"resource timer/key survives persistence");
    WorldMetadata metadata;metadata.displayName="Capabilities";metadata.seed=17;metadata.generationVersion=WorldGenContext::GENERATION_VERSION;metadata.timePolicies[0]={45,1};saves.saveMetadata(metadata);check(saves.loadMetadata().timePolicies.at(0)==std::make_pair(45u,1u),"time policy persisted");
    GameSession session(root/"saves");GameSessionTestAccess::prepareLanScene(session);auto& p=GameSessionTestAccess::player(session);auto& world=GameSessionTestAccess::world(session);
    const auto staff=Plugins::resolveItem("capability_native:staff");p.inventory().slot(0)={staff,1,0};p.setSelectedSlot(0);
    Plugins::contextProvider()=[&]{return session.pluginContext();};plugins.actionState=[&](uint64_t id){return session.pluginActionState(id);};plugins.executeAction=[&](uint64_t id,const std::string& key){return session.pluginExecuteAction(id,key);};plugins.actionTarget=[&](uint64_t id,const std::string& key,bool preview,MC_ActionTarget& t){return session.pluginActionTarget(id,key,preview,t);};plugins.actionCooldown=[&](uint64_t id,const std::string& key,float& out){return session.pluginActionCooldown(id,key,out);};plugins.dimensionTime=[&](uint32_t dim,MC_DimensionTime& out){return session.pluginDimensionTime(dim,out);};plugins.setDimensionTime=[&](const MC_DimensionTime& t){return session.pluginSetDimensionTime(t);};
    auto ready=Plugins::event(MC_WORLD_READY);plugins.dispatch(ready);MC_ActionTarget target{};target.size=sizeof(target);check(host.playerActions()->preview(&context,0,"capability_native:blink",&target)==0&&target.valid,"read-only native target preview");check(p.activeItem().damage==0&&p.getPosition().x==.5,"preview cannot mutate player");
    MC_ActionRequest request{sizeof(request),0,"capability_native:blink"};uint64_t receipt=0;check(host.playerActions()->request(&context,&request,&receipt)==0,"host action queued");plugins.flushOperations();MC_ActionResult result{};result.size=sizeof(result);check(host.playerActions()->result(&context,receipt,&result)==0&&result.status==MC_ACTION_SUCCEEDED,"action success receipt");check(p.getPosition().x==2.5&&p.activeItem().damage==1&&p.pluginCooldown("capability_native:blink")==2,"success atomic costs");
    check(!session.pluginExecuteAction(0,request.action)&&p.activeItem().damage==1,"cooldown prevents duplicate action/cost");p.clearPluginCooldowns();check(host.playerActions()->request(&context,&request,&receipt)==0,"second action queued");p.inventory().slot(0)={ItemId::COAL,1,0};plugins.flushOperations();check(host.playerActions()->result(&context,receipt,&result)==0&&result.status==MC_ACTION_FAILED&&p.getPosition().x==2.5,"item changed cancels queued action");
    check(!session.pluginExecuteAction(999,request.action),"departed player cannot become host");
    p.teleport({.5,1.01,.5});p.inventory().slot(0)={staff,1,0};world.setBlock(2,1,0,BlockId::STONE);
    check(!session.pluginExecuteAction(0,request.action)&&p.activeItem().damage==0&&p.getPosition().x==.5,"blocked landing has no cost or movement");world.setBlock(2,1,0,BlockId::AIR);
    p.configureRules(GameMode::Creative,Difficulty::Normal);check(session.pluginExecuteAction(0,request.action)&&p.activeItem().damage==0,"Creative action is wear-exempt");p.clearPluginCooldowns();p.configureRules(GameMode::Survival,Difficulty::Normal);

    world.setBlock(4,1,0,resource);world.setBlock(4,1,0,spent);world.scheduleResource({4,1,0},resource,2);world.tickInteractiveBlocks([](const glm::ivec3&){return false;});world.tickInteractiveBlocks([](const glm::ivec3&){return false;});check(world.getBlock(4,1,0)==resource,"resource reset simulation");
    p.setPosition({.5,1.01,.5});p.setOrientation(0,0);p.setMouseLocked(true);p.inventory().slot(0)={ItemId::COAL,1,0};
    world.setBlock(0,2,2,resource);p.handleMouseButton(MouseButton::Right,ButtonAction::Press);check(world.getBlock(0,2,2)==spent&&p.inventory().count(ItemId::COAL)==3,"harvest awards after successful replacement");
    p.update(.2f);world.setBlock(0,2,2,BlockId::STONE);p.inventory().slot(0)={Plugins::resolveItem("capability_demo:vine"),1,0};
    p.handleMouseButton(MouseButton::Right,ButtonAction::Press);check(world.getBlock(0,2,1)==BlockId::AIR,"hanging behavior refuses unsupported placement");world.setBlock(0,3,1,BlockId::STONE);p.handleMouseButton(MouseButton::Right,ButtonAction::Press);check(world.getBlock(0,2,1)==vine,"hanging behavior accepts supporting ceiling");
    world.setBlock(0,3,1,BlockId::AIR);check(world.getBlock(0,2,1)==BlockId::AIR,"support loss removes dependent plugin block");world.setBlock(0,3,1,BlockId::STONE);world.setBlock(0,2,1,vine);
    p.update(.2f);p.inventory().slot(0)={ItemId::STONE,1,0};p.handleMouseButton(MouseButton::Right,ButtonAction::Press);check(world.getBlock(0,2,1)==BlockId::STONE,"placement replaces replaceable target");
    const auto nativeVine=Plugins::resolveBlock("capability_native:vine");world.setBlock(0,1,0,nativeVine);world.setBlock(0,2,0,nativeVine);InputState input;std::array<InputBinding,INPUT_ACTION_COUNT> bindings{};input.setVirtual(InputAction::Jump,1);input.update(bindings);p.handleMovement(input,.05f);const auto beforeClimb=p.getPosition().y;p.update(.05f);check(p.getPosition().y>beforeClimb,"climbable behavior moves player upward");
    input.clearVirtual();input.update(bindings);p.handleMovement(input,.05f);p.teleport({.5,1.01,.5});world.setBlock(0,1,0,spent);world.setBlock(0,2,0,BlockId::AIR);const auto health=p.survivalStats().health();for(int i=0;i<12;++i)p.update(.05f);check(p.survivalStats().health()<health,"contact damage enters survival damage path");
    world.setBlock(0,1,0,BlockId::AIR);
    world.setBlock(4,1,0,spent);world.scheduleResource({4,1,0},resource,1);world.setBlock(4,1,0,BlockId::CHEST);world.tickInteractiveBlocks([](const glm::ivec3&){return false;});check(world.getBlock(4,1,0)==BlockId::CHEST&&world.getBlockEntity({4,1,0})->type==BlockEntityType::Chest,"container edit cancels resource reset and creates correct entity");
    const auto env=DayNightCycle{}.evaluate();bool clouds=true;const auto changed=Plugins::applyEnvironment(env,static_cast<uint32_t>(dim),clouds);check(!clouds&&changed.skyStyle==RenderSkyStyle::Heaven&&changed.moonSize==1.3f,"dimension sky/cloud profile");
    world.setBlock(5,0,0,BlockId::PLANKS_SLAB_BOTTOM);check(pluginSafeLanding(world,{5.5,.51,.5}),"full-foot landing accepts slab support height");
    world.setBlock(5,0,0,BlockId::PLANKS_STAIRS_BOTTOM_NORTH);check(!pluginSafeLanding(world,{5.5,1.01,.5}),"partial stair face cannot authorize unsupported foot area");world.setBlock(5,0,0,BlockId::STONE);
    auto rules=world.gameRules();rules.set(GameRuleId::AdvanceTime,GameRuleValue::boolean(false));GameSessionTestAccess::setRules(session,rules);
    MC_DimensionTime timePolicy{sizeof(timePolicy),0,20,1,.25f};check(host.environment()->set_time(&context,&timePolicy)==0,"time mutation queued");plugins.flushOperations();GameSessionTestAccess::tickDaylight(session,.1f);check(session.daylightState().phase()==.25f,"paused per-dimension clock");
    timePolicy.advancing=2;check(host.environment()->set_time(&context,&timePolicy)==0,"advancing override queued");plugins.flushOperations();GameSessionTestAccess::tickDaylight(session,.1f);check(std::abs(session.daylightState().phase()-.255f)<.00001f,"dimension duration/advance override world rule");
    timePolicy.advancing=0;timePolicy.cycle_seconds=0;check(host.environment()->set_time(&context,&timePolicy)==0,"inherited clock queued");plugins.flushOperations();GameSessionTestAccess::tickDaylight(session,.1f);check(session.daylightState().phase()==.25f,"inherited clock follows paused world rule");
    ParticleSystem particles;MC_ParticleEmission emission{};emission.size=sizeof(emission);emission.particle="capability_demo:spark";emission.count=3;check(particles.emitPlugin(emission)&&particles.size()==3,"host particle emission");check(!particles.emitPlugin(MC_ParticleEmission{sizeof(emission),0,129,1,emission.particle,{0,0,0}}),"particle emission bound");const auto render=particles.buildRenderData({0,0,0});check(render[0].color.b==1&&render[0].color.a==.8f,"particle colors reach rendering");
    check(Plugins::content().extensions.heldModels.at(static_cast<uint16_t>(staff)).asset&&Plugins::content().extensions.playlists.size()==2,"held model/playlists loaded");
    Lan::AuthorityState state;state.self.id=1;state.cycleOverride=45;state.advancing=1;state.actionCooldowns[request.action]=1.5f;const auto wire=Lan::decodeAuthorityState(Lan::encodeAuthorityState(state));check(wire.cycleOverride==45&&wire.advancing==1&&wire.actionCooldowns.at(request.action)==1.5f,"LAN authoritative time/cooldowns roundtrip");
    check(Plugins::content().networkDescription.count("structure/capability_demo:bridge")&&Plugins::content().networkDescription.count("action/capability_native:blink"),"gameplay capability handshake");
    Config::RENDER_DISTANCE=2;
    {
        GameSession hostSession(root/"host/saves"),clientSession(root/"client/saves");
        const auto worldId=hostSession.createWorld("Capability LAN",42,GameMode::Survival,Difficulty::Normal,true,WorldType::Superflat);
        hostSession.startWorld(worldId,true,0);GameSessionTestAccess::prepareLanScene(hostSession);
        Plugins::contextProvider()=[&]{return hostSession.pluginContext();};
        Plugins::actionDispatcher()=[&](uint64_t id,const std::string& key){return hostSession.pluginExecuteAction(id,key);};
        check(hostSession.openLanRoom(0,2,true),"capability LAN host opens");clientSession.configureLod({false,16,LodAggressiveness::PowerSaver,LodPrecision::Low});
        check(clientSession.joinLanRoom("::1",hostSession.lanPort(),0),"capability LAN join");
        double now=0;auto pump=[&]{now+=.005;hostSession.pollLan(now);clientSession.pollLan(now);GameSessionTestAccess::world(clientSession).processCompletedGenerations();std::this_thread::sleep_for(std::chrono::milliseconds(1));};
        for(int i=0;i<500&&!clientSession.lanWorldReady();++i)pump();
        check(clientSession.lanWorldReady(),"capability LAN ready");
        const auto peer=GameSessionTestAccess::replicaPeer(clientSession);auto& guest=GameSessionTestAccess::guest(hostSession,peer);
        guest.player.inventory().slot(0)={staff,1,0};guest.player.setSelectedSlot(0);guest.player.setPosition({.5,1.01,.5});
        MC_DimensionTime policy{sizeof(policy),0,45,1,.3f};check(hostSession.pluginSetDimensionTime(policy),"host time override");
        for(int i=0;i<100&&clientSession.playerState().activeItem().id!=staff;++i)pump();
        check(clientSession.playerState().activeItem().id==staff,"LAN special item replica");
        for(int i=0;i<100;++i)pump();
        Plugins::targetDispatcher()=[&](uint64_t id,const std::string& key,bool preview,MC_ActionTarget& out){return clientSession.pluginActionTarget(id,key,preview,out);};
        check(clientSession.playerState().pluginTarget().has_value(),"client render preview uses local LAN actor");
        MC_ActionTarget clientTarget{};clientTarget.size=sizeof(clientTarget);check(clientSession.pluginActionTarget(peer,"capability_native:blink",true,clientTarget)&&clientTarget.valid,"client target from loaded replica");
        check(!clientSession.pluginExecuteAction(peer,"capability_native:blink"),"client cannot execute gameplay action");
        clientSession.setLocalControl(true);clientSession.handleMouseButton(MouseButton::Right,ButtonAction::Press);for(int i=0;i<20;++i)pump();
        GameSessionTestAccess::tickLanPlayers(hostSession,.05f);
        check(guest.player.getPosition().x==2.5&&guest.player.activeItem().damage==1,"guest use executes own action with one cost");
        check(hostSession.playerState().activeItem().empty(),"guest action never costs host inventory");
        for(int i=0;i<60&&clientSession.playerState().activeItem().damage!=1;++i)pump();
        check(clientSession.playerState().activeItem().damage==1&&clientSession.playerState().pluginCooldown("capability_native:blink")>0,"host cooldown/wear reaches replica");
        MC_DimensionTime clientTime{};clientTime.size=sizeof(clientTime);check(clientSession.pluginDimensionTime(0,clientTime)&&clientTime.cycle_seconds==45&&clientTime.advancing==1,"time override reaches replica");
        clientSession.handleMouseButton(MouseButton::Right,ButtonAction::Release);
        // V2 is exercised through real guest input; toggle sneak remains latched
        // after release, and the host derives the destination from that intent.
        const auto contextStaff=Plugins::resolveItem("capability_native:context_staff");
        guest.player.inventory().slot(0)={contextStaff,1,0};guest.player.setPosition({.5,1.01,.5});
        GameSessionTestAccess::tickLanPlayers(hostSession,.2f); // Polling alone does not advance the previous use cooldown.
        for(int i=0;i<100&&clientSession.playerState().activeItem().id!=contextStaff;++i)pump();
        check(clientSession.playerState().activeItem().id==contextStaff,"V2 LAN item replica");
        InputState sneak;std::array<InputBinding,INPUT_ACTION_COUNT> emptyBindings{};
        clientSession.setToggleSneak(true);sneak.setVirtual(InputAction::Sneak,1);sneak.update(emptyBindings);clientSession.handleMovement(sneak,0);
        sneak.beginFrame();sneak.clearVirtual();sneak.update(emptyBindings);clientSession.handleMovement(sneak,0);
        check(clientSession.playerState().sneakInput(),"toggle sneak lost intent after release");
        for(int i=0;i<20;++i)pump();
        GameSessionTestAccess::tickLanPlayers(hostSession,.05f); // Observe Use released before the next press edge.
        check(!clientSession.pluginExecuteAction(peer,"capability_native:context"),"client executed V2 authority action");
        clientSession.handleMouseButton(MouseButton::Right,ButtonAction::Press);for(int i=0;i<20;++i)pump();GameSessionTestAccess::tickLanPlayers(hostSession,.05f);
        check(guest.player.sneakInput()&&guest.player.getPosition().x==-1.5,"LAN V2 used key release or old preview instead of latched intent");
        clientSession.handleMouseButton(MouseButton::Right,ButtonAction::Release);clientSession.setToggleSneak(false);sneak.beginFrame();sneak.update(emptyBindings);clientSession.handleMovement(sneak,0);
        for(int i=0;i<20;++i)pump();GameSessionTestAccess::tickLanPlayers(hostSession,.05f);
        MC_ParticleEmission networkEmission{sizeof(networkEmission),0,3,0,"capability_native:spark",{2.5,2,0}};
        const auto before=GameSessionTestAccess::particleCount(clientSession);check(hostSession.pluginEmitParticle(networkEmission),"host world particle emission");for(int i=0;i<30;++i)pump();
        check(GameSessionTestAccess::particleCount(clientSession)==before+3,"dimension-scoped particles reach LAN client");
        check(!clientSession.pluginEmitParticle(networkEmission),"client world emission rejected");
        guest.player.inventory().slot(0)={Plugins::resolveItem("capability_demo:charm"),1,0};
        check(hostSession.pluginExecuteAction(peer,"capability_demo:visit"),"guest action uses dimension landing policy");
        check(guest.profile.dimension==dim&&guest.player.activeItem().damage==1&&guest.player.pluginCooldown("capability_native:blink")>0&&guest.player.pluginCooldown("capability_demo:visit")==3,"travel preserves prior cooldown and charges successful action");
        check(!hostSession.pluginExecuteAction(peer,"capability_demo:visit")&&guest.player.activeItem().damage==1,"duplicate travel does not consume wear");
        hostSession.closeLanRoom();GameSessionTestAccess::disconnectReplica(clientSession);
        Plugins::actionDispatcher()={};Plugins::targetDispatcher()={};Plugins::contextProvider()={};
    }
    plugins.shutdown();std::filesystem::remove_all(root);std::cout<<"Plugin gameplay/presentation capabilities passed\n";return 0;
}
}
