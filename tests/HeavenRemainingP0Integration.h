#pragma once
#include "plugins/PluginManager.h"
#include "HeavenNativeSkywayReference.h"
#include "network/PlayerProfile.h"
#include "network/StateProtocol.h"
#include "core/AssetStore.h"
#include "model/ModelAsset.h"
#include "world/WorldLighting.h"
#include "world/ChunkStore.h"
#include <fstream>
#include <tuple>

namespace HeavenRemainingP0 {
inline void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F> void rejects(F f,const char* why){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,why);}
inline void write(const std::filesystem::path& path,const std::string& body){std::filesystem::create_directories(path.parent_path());std::ofstream(path,std::ios::binary)<<body;}
inline auto snapshot(const std::filesystem::path& root){
    std::map<std::string,std::pair<std::string,std::filesystem::file_time_type>> result;
    for(const auto& f:std::filesystem::recursive_directory_iterator(root)){
        const auto key=f.path().lexically_relative(root).generic_string();
        if(f.is_directory())result[key+"/"]={"",f.last_write_time()};
        else {std::ifstream stream(f.path(),std::ios::binary);result[key]={std::string(std::istreambuf_iterator<char>(stream),{}),f.last_write_time()};}
    }return result;
}
inline void solid(World& world,int x,int y,int z,BlockId id){
    auto* c=world.getChunk(HeavenPolicy::floorDiv(x,16),HeavenPolicy::floorDiv(z,16));
    c->setBlock((x%16+16)%16,y,(z%16+16)%16,id);c->generated=true;c->lifecycle=Chunk::LifecycleState::Renderable;
}
inline void landing(World& world,glm::dvec3 p){
    const auto q=glm::ivec3(glm::floor(p));
    for(int z=q.z-1;z<=q.z+1;++z)for(int x=q.x-1;x<=q.x+1;++x){solid(world,x,q.y-1,z,BlockId::STONE);solid(world,x,q.y,z,BlockId::AIR);solid(world,x,q.y+1,z,BlockId::AIR);}
}
inline void aim(Player& p,glm::dvec3 target){const auto d=glm::normalize(target-p.getEyePosition());p.setOrientation(static_cast<float>(glm::degrees(std::atan2(d.x,d.z))),static_cast<float>(glm::degrees(std::asin(d.y))));}
inline void modelReference(const model::ModelAsset& asset){
    const auto& vertices=asset.primitives.at(0).vertices;
    const glm::vec3 centers[]={{0,.24f,0},{0,.72f,0},{0,.90f,0},{0,1.07f,0},{0,-.20f,0}};
    const glm::vec3 sizes[]={{.10f,1.05f,.10f},{.26f,.10f,.26f},{.20f,.28f,.20f},{.12f,.10f,.12f},{.14f,.10f,.14f}};
    const glm::vec3 corners[]={{-.5f,-.5f,-.5f},{.5f,-.5f,-.5f},{.5f,.5f,-.5f},{-.5f,.5f,-.5f},{-.5f,-.5f,.5f},{.5f,-.5f,.5f},{.5f,.5f,.5f},{-.5f,.5f,.5f}};
    const int faces[][4]={{0,3,2,1},{5,6,7,4},{4,7,3,0},{1,2,6,5},{3,7,6,2},{4,0,1,5}},palettes[]={0,4,6,5,4};
    for(int box=0;box<5;++box)for(int face=0;face<6;++face){
        const int tile=palettes[box]*6+face;const glm::vec2 lo((tile%8+.0625f)/8,(tile/8+.0625f)/8),hi((tile%8+.9375f)/8,(tile/8+.9375f)/8);
        const glm::vec2 uv[]={glm::vec2(lo.x,hi.y),lo,glm::vec2(hi.x,lo.y),hi};
        for(int corner=0;corner<4;++corner){const auto& v=vertices.at(static_cast<size_t>(box*24+face*4+corner));check(glm::distance(v.position,centers[box]+corners[faces[face][corner]]*sizes[box])<1e-7f&&v.uv==uv[corner],"scepter mesh differs from original five-box reference");}
    }
    const auto& image=asset.images.at(0);check(image.width==64&&image.height==64,"scepter palette dimensions");
    const glm::vec3 colors[]={{117,78,43},{158,112,62},{125,130,137},{213,221,224},{244,193,49},{61,220,208},{142,127,244},{50,54,65},{227,217,184}};
    const float shades[]={.90f,.78f,.70f,.84f,1,.60f};
    for(int palette=0;palette<9;++palette)for(int face=0;face<6;++face)for(int y=0;y<8;++y)for(int x=0;x<8;++x){const int tile=palette*6+face;const auto color=colors[palette]*shades[face]*(((x*3+y*5)%7==0)?.91f:1.f);const auto at=static_cast<size_t>(((tile/8*8+y)*64+tile%8*8+x)*4);for(int channel=0;channel<3;++channel)check(image.pixels[at+channel]==static_cast<uint8_t>(color[channel]),"scepter original palette mismatch");}
}
inline void actionEquivalence(const std::filesystem::path& root){
    const int oldDistance=Config::RENDER_DISTANCE;Config::RENDER_DISTANCE=0;
    for(uint64_t seed:{12ULL,123ULL,999ULL}){
        GameSession session(root/"actions");GameSessionTestAccess::prepareActionScene(session,DimensionId::Heaven,seed);
        auto& world=GameSessionTestAccess::world(session);auto& p=GameSessionTestAccess::player(session);
        Plugins::targetDispatcher()=[&](uint64_t id,const std::string& key,bool preview,MC_ActionTarget& out){return session.pluginActionTarget(id,key,preview,out);};
        Plugins::actionDispatcher()=[&](uint64_t id,const std::string& key){return session.pluginExecuteAction(id,key);};
        Plugins::useDispatcher()=[&](uint64_t id,const std::string& key){return session.pluginUseAction(id,key);};
        HeavenNativeSkywayReference native(world.generator());
        const auto productionOrigin=world.generator().originHeavenSkyway();
        check(productionOrigin.worldX==native.m_originSkyway.worldX&&productionOrigin.worldZ==native.m_originSkyway.worldZ&&productionOrigin.mainFloorY==native.m_originSkyway.mainFloorY,"origin policy differs from frozen native reference");
        for(int z=-3;z<=3;++z)for(int x=-3;x<=3;++x){const auto a=world.generator().heavenSkywayForCell(x,z);const auto b=native.heavenSkywayForCell(x,z);check(bool(a)==bool(b)&&(!a||(a->worldX==b->worldX&&a->worldZ==b->worldZ&&a->mainFloorY==b->mainFloorY)),"cell policy differs from frozen native reference");}
        auto verify=[&](WorldGenerator::HeavenSkyway anchor){
            for(int layer=0;layer<5;++layer)for(bool sneak:{false,true}){
                const glm::ivec3 core(anchor.worldX,anchor.mainFloorY+HeavenPolicy::HEAVEN_SKYWAY_OFFSETS[layer]+1,anchor.worldZ);
                p.inventory().slot(0)={};p.setSelectedSlot(0);p.setPosition(glm::dvec3(core)+glm::dvec3(-1.5,.01,.5));
                InputState input;input.setVirtual(InputAction::Sneak,sneak?1.f:0.f);input.update({});p.handleMovement(input,0);aim(p,glm::dvec3(core)+.5);
                const int targetLayer=layer+(sneak?-1:1);
                std::optional<glm::dvec3> expected;
                if(targetLayer>=0&&targetLayer<5){expected=glm::dvec3(anchor.worldX+1.5,anchor.mainFloorY+HeavenPolicy::HEAVEN_SKYWAY_OFFSETS[targetLayer]+1.01,anchor.worldZ+.5);landing(world,*expected);}
                solid(world,core.x,core.y,core.z,BlockId::STAR_CRYSTAL);
                check(native.heavenSkywayDestination(core,!sneak)==expected,"Skyway layer differs from frozen native reference");
                MC_ActionTarget target{};bool handled=false;
                check(session.pluginActionTarget(0,"official_heaven:skyway",true,target,&handled)&&handled,"Skyway core not recognized");
                check(bool(target.valid)==expected.has_value(),"Skyway five-layer direction/endpoint mismatch");
                if(expected){check(glm::distance(glm::dvec3(target.position[0],target.position[1],target.position[2]),*expected)<1e-9,"Skyway native coordinate mismatch");check(world.heavenSkywayDestination(core,!sneak)==expected,"Skyway loaded native reference mismatch");}
                if(expected)check(session.pluginUseAction(0,"official_heaven:skyway")&&p.getPosition()==*expected,"Skyway execution differs from preview");
                if(!expected)check(session.pluginUseAction(0,"official_heaven:skyway"),"Skyway endpoint use must be consumed");
            }
        };
        verify(world.generator().originHeavenSkyway());
        const auto origin=world.generator().originHeavenSkyway();
        const glm::ivec3 forged(origin.worldX,origin.mainFloorY+3,origin.worldZ);
        p.inventory().slot(0)={};p.setPosition(glm::dvec3(forged)+glm::dvec3(-1.5,.01,.5));
        InputState released;released.update({});p.handleMovement(released,0);aim(p,glm::dvec3(forged)+.5);solid(world,forged.x,forged.y,forged.z,BlockId::STAR_CRYSTAL);
        MC_ActionTarget forgedTarget{};bool forgedHandled=true;
        check(session.pluginActionTarget(0,"official_heaven:skyway",true,forgedTarget,&forgedHandled)&&!forgedTarget.valid&&!forgedHandled,"forged Skyway core was accepted");
        bool ordinary=false;
        for(int z=-3;z<0&&!ordinary;++z)for(int x=-3;x<0&&!ordinary;++x)if(const auto a=world.generator().heavenSkywayForCell(x,z)){verify(*a);ordinary=true;}
        check(ordinary,"negative-coordinate ordinary Skyway fixture missing");
        InputState input;p.setToggleSneak(false);input.update({});p.handleMovement(input,0);
        // Synthetic loaded terrain isolates the original first-hit/3x3 algorithm.
        p.inventory().slot(0)={ItemId::STARSTEP_SCEPTER,1,0};p.setPosition({.5,1.01,.5});p.clearPluginCooldowns();
        landing(world,{.5,1.01,.5});for(int z=-1;z<=1;++z)for(int x=5;x<=7;++x)solid(world,x,0,z,BlockId::STONE);
        aim(p,{6.5,.5,.5});const auto expected=world.starstepDestination(p.getEyePosition(),p.getForward());check(expected.has_value(),"native scepter test target missing");
        MC_ActionTarget target{};check(session.pluginActionTarget(0,"official_heaven:starstep",true,target)&&target.valid,"scepter plugin preview unavailable");
        check(glm::distance(glm::dvec3(target.position[0],target.position[1],target.position[2]),*expected)<1e-9&&p.activeItem().damage==0,"scepter preview changed result or consumed wear");
        solid(world,3,1,0,BlockId::STONE);solid(world,3,2,0,BlockId::STONE);
        const auto blocked=world.starstepDestination(p.getEyePosition(),p.getForward());
        MC_ActionTarget blockedTarget{};check(blocked&&session.pluginActionTarget(0,"official_heaven:starstep",true,blockedTarget)&&blockedTarget.valid&&glm::distance(glm::dvec3(blockedTarget.position[0],blockedTarget.position[1],blockedTarget.position[2]),*blocked)<1e-9,"scepter ignored nearer obstruction");
        solid(world,3,1,0,BlockId::AIR);solid(world,3,2,0,BlockId::AIR);
        const auto oldYaw=p.getYaw(),oldPitch=p.getPitch();aim(p,{102.5,2.5,.5});solid(world,102,2,0,BlockId::STONE);
        MC_ActionTarget distant{};check(session.pluginActionTarget(0,"official_heaven:starstep",true,distant)&&!distant.valid,"scepter exceeded 96-block eye ray");
        p.setOrientation(oldYaw,oldPitch);
        p.inventory().offhand()={ItemId::SHIELD,1,0};p.setMouseLocked(true);p.handleMouseButton(MouseButton::Right,ButtonAction::Press);
        check(p.getPosition()==*expected&&p.activeItem().damage==1&&p.pluginCooldown("official_heaven:starstep")==3,"scepter movement/cost/cooldown mismatch");
        check(!session.pluginExecuteAction(0,"official_heaven:starstep")&&p.activeItem().damage==1,"scepter duplicate costs");
        p.clearPluginCooldowns();p.setActionUseCooldown(0);p.setPosition({.5,1.01,.5});aim(p,{40.5,2.5,.5});
        MC_ActionTarget unloaded{};check(session.pluginActionTarget(0,"official_heaven:starstep",true,unloaded)&&!unloaded.valid,"scepter acquired an unloaded landing");
        check(!session.pluginExecuteAction(123456,"official_heaven:starstep"),"departed actor acquired host authority");
        session.abortPluginWorld();
    }
    Plugins::actionDispatcher()={};Plugins::targetDispatcher()={};Plugins::useDispatcher()={};Config::RENDER_DISTANCE=oldDistance;
}
inline int run(const std::filesystem::path& assets,const std::filesystem::path& module){
    const auto root=std::filesystem::temp_directory_path()/"minecraftc-heaven-remaining-p0";
    std::filesystem::remove_all(root);std::filesystem::create_directories(root);
    AssetStore assetStore(assets);uint64_t originalSignature=0;
    {
        Plugins::PluginManager plugins({assets,root});plugins.initialize();
        check(Plugins::validDimension(1)&&Plugins::content().requirements.size()==1&&Plugins::content().requirements[0].first=="official_heaven","official provider not default enabled");
        const auto& held=Plugins::content().extensions.heldModels.at(static_cast<uint16_t>(ItemId::STARSTEP_SCEPTER));
        check(held.path.parent_path()==assets/"plugins/official_heaven"&&held.asset->primitives.size()==1&&held.asset->primitives[0].vertices.size()==120&&held.asset->primitives[0].indices.size()==180,"scepter package/geometry mismatch");
        modelReference(*held.asset);originalSignature=Lan::contentSignature(Plugins::content().networkDescription);
        check(getLightDampening(BlockId::SKYROOT_LEAVES)==1&&isLeafBlock(BlockId::SKYROOT_LEAVES)&&fuelTicks(ItemId::SKYROOT_PLANKS)==300,"official semantic rules not published");
        WorldCatalog catalog(root/"saves");const auto id=catalog.create("Enabled",12,GameMode::Survival,Difficulty::Normal,true,WorldType::Superflat);
        check(SaveStore(root/"saves"/id).loadMetadata().generation.size()==2,"enabled save lost Heaven");
        Lan::PlayerProfile profile;profile.identity={std::string(32,'a'),std::string(32,'b'),"P0"};profile.inventory.slot(0)={ItemId::STARSTEP_SCEPTER,1,0};
        Lan::ProfileStore(root/"saves"/id/"players").save(profile);
        (void)Lan::ProfileStore::localIdentity(root);
        auto requirements=Plugins::content().requirements;Plugins::content().requirements.clear();WorldMetadata old;old.generationVersion=20;old.displayName="Old set";SaveStore(root/"saves/old-set").saveMetadata(old);Plugins::content().requirements=requirements;
        const auto before=snapshot(root/"saves");rejects([&]{SaveStore(root/"saves/old-set").loadMetadata();},"old plugin set accepted");check(snapshot(root/"saves")==before,"old-set refusal wrote files");
        actionEquivalence(root);plugins.shutdown();
    }
    {
        const auto changedAssets=root/"repacked-assets";
        std::filesystem::create_directories(changedAssets/"plugins");
        std::filesystem::copy(assets/"plugins/official_heaven",changedAssets/"plugins/official_heaven",std::filesystem::copy_options::recursive);
        std::ofstream(changedAssets/"plugins/official_heaven/LICENSE.md",std::ios::app)<<"\nChanged package identity\n";
        Plugins::PluginManager changed({changedAssets,root/"repacked-data"});changed.initialize();
        check(Plugins::validDimension(1)&&Lan::contentSignature(Plugins::content().networkDescription)!=originalSignature,"official package fingerprint omitted from LAN identity");changed.shutdown();
    }
    write(root/"plugins.json",R"({"official_heaven":false})");
    {
        Plugins::PluginManager plugins({assets,root});plugins.initialize();
        check(!Plugins::validDimension(1)&&Plugins::content().requirements.empty(),"disabled Heaven retained registration");
        check(!isValidItemId(ItemId::STARSTEP_SCEPTER)&&!isValidBlockId(BlockId::SKYROOT_LEAVES)&&!itemFromCommandName("starstep_scepter"),"disabled reserved content available");
        rejects([]{Plugins::resolveBlock("minecraftc:block_106");},"numeric key bypassed disabled owner");
        for(auto item:creativeInventoryItems())check(!Plugins::heavenItemSlot(item),"creative catalog leaked Heaven");
        for(const auto& recipe:craftingRecipes()){check(!Plugins::heavenItemSlot(recipe.output.id),"Heaven recipe output leaked");for(auto item:recipe.ingredients)check(!Plugins::heavenItemSlot(item),"Heaven recipe ingredient leaked");}
        check(!findSmeltingRecipe(ItemId::CLOUDBERRY),"Heaven smelting leaked");
        const auto before=snapshot(root/"saves");GameSession session(root/"saves");
        rejects([&]{session.startWorld("enabled",false,0);},"disabled provider world opened");
        check(snapshot(root/"saves")==before,"refused session load wrote world/profile files");
        const auto listed=WorldCatalog(root/"saves").list();check(listed.size()==2&&!listed[0].compatible&&!listed[1].compatible,"incompatible worlds disappeared from inspection");
        rejects([&]{Lan::ProfileStore(root/"saves/enabled/players").load({std::string(32,'a'),std::string(32,'b'),"P0"});},"disabled profile content loaded");
        check(snapshot(root/"saves")==before,"profile refusal wrote files");
        const auto id=session.createWorld("Disabled",13,GameMode::Survival,Difficulty::Normal,true,WorldType::Superflat);
        check(SaveStore(root/"saves"/id).loadMetadata().generation.size()==1,"single-dimension save cannot reload");
        Lan::WorldInfo info;check(Lan::decodeWorldInfo(Lan::encodeWorldInfo(info)).generation.size()==1,"single-dimension LAN info failed");
        Lan::PlayerProfile profile;profile.identity={std::string(32,'c'),std::string(32,'d'),"Off"};check(Lan::decodeProfile(Lan::encodeProfile(profile)).positions.size()==1,"single-dimension profile failed");
        const int distance=Config::RENDER_DISTANCE;Config::RENDER_DISTANCE=2;
        session.configureLod({false,16,LodAggressiveness::PowerSaver,LodPrecision::Low});session.startWorld(id,true,0);GameSessionTestAccess::prepareLanScene(session);
        GameSession client(root/"off-client/saves");client.configureLod({false,16,LodAggressiveness::PowerSaver,LodPrecision::Low});
        check(session.openLanRoom(0,2,true)&&client.joinLanRoom("::1",session.lanPort(),0),"single-dimension LAN failed to start");
        double now=0;for(int i=0;i<600&&!client.lanWorldReady();++i){now+=.005;session.pollLan(now);client.pollLan(now);GameSessionTestAccess::world(client).processCompletedGenerations();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        check(client.lanWorldReady(),"single-dimension LAN did not become ready");
        session.closeLanRoom();GameSessionTestAccess::disconnectReplica(client);session.abortPluginWorld();Config::RENDER_DISTANCE=distance;
        plugins.shutdown();
    }
    {
        Plugins::PluginManager safe({assets,root});safe.initialize(true);check(Plugins::content().extensions.dimensions.size()==1&&Plugins::content().requirements.empty(),"safe mode retained Heaven");safe.shutdown();
    }
    write(root/"plugins.json","{}");
    {
        Plugins::PluginManager plugins({assets,root});plugins.initialize();
        const auto before=snapshot(root/"saves");
        rejects([&]{SaveStore(root/"saves/disabled").loadMetadata();},"off-world accepted after enabling provider");
        const auto requirements=Plugins::content().requirements;Plugins::content().requirements[0].second+="-changed";
        rejects([&]{SaveStore(root/"saves/enabled").loadMetadata();},"changed package fingerprint accepted");Plugins::content().requirements=requirements;
        auto& revision=Plugins::content().extensions.dimensions.at(1).revision;const auto original=revision;revision="changed";
        rejects([&]{SaveStore(root/"saves/enabled").loadMetadata();},"changed dimension revision accepted");revision=original;
        check(snapshot(root/"saves")==before,"compatibility refusals wrote files");
        size_t index=0;for(size_t i=0;i<plugins.plugins().size();++i)if(plugins.plugins()[i].manifest.id=="official_heaven")index=i;
        plugins.setEnabled(index,false);check(Plugins::validDimension(1),"settings hot-unloaded active content");plugins.shutdown();
    }
    {Plugins::PluginManager restarted({assets,root});restarted.initialize();check(!Plugins::validDimension(1),"restart did not apply disable");restarted.shutdown();}
    write(root/"plugins.json","{}");
    write(root/"missing/mods/dependent/mod.json",R"({"id":"dependent","api":1,"version":"1.0.0","dependencies":[{"id":"official_heaven"}]})");
    {
        Plugins::PluginManager missing({root/"missing-assets",root/"missing"});missing.initialize();check(!Plugins::validDimension(1)&&Plugins::content().extensions.actions.empty()&&Plugins::content().requirements.empty(),"missing official resources left partial registration");
        for(const auto& info:missing.plugins())if(info.manifest.id=="dependent")check(!info.active&&!info.status.empty(),"failed provider did not block dependent");missing.shutdown();
    }
    const auto package=root/"mods/capability_native";
    std::filesystem::create_directories(package);
    for(const auto file:{"staff.glb","chime.wav"})std::filesystem::copy_file(assets.parent_path()/"examples/plugins/capability_native"/file,package/file,std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(module,package/module.filename(),std::filesystem::copy_options::overwrite_existing);
    write(package/"mod.json","{\"id\":\"capability_native\",\"version\":\"1.0.0\",\"api\":1,\"native\":{\"windows\":\""+module.filename().string()+"\",\"linux\":\""+module.filename().string()+"\",\"macos\":\""+module.filename().string()+"\"}}");
    write(root/"mods/p0_data/mod.json",R"({"id":"p0_data","version":"1.0.0","api":1,"data":"content.json"})");
    write(root/"mods/p0_data/content.json",R"({"materials":[{"key":"p0_data:m","color":[1,1,1,1]}],"blocks":[{"key":"p0_data:plant","name":"Plant","materials":"p0_data:m","cross":true,"solid":false,"layer":1}],"block_behaviors":[{"block":"p0_data:plant","light_dampening":0,"tags":128,"substrate_tags":4,"break_drops":[]}]})");
    write(root/"mods/p0_bad/mod.json",R"({"id":"p0_bad","version":"1.0.0","api":1,"data":"content.json"})");
    write(root/"mods/p0_bad/content.json",R"({"materials":[{"key":"p0_bad:m","color":[1,1,1,1]}],"blocks":[{"key":"p0_bad:b","name":"Bad","materials":"p0_bad:m"}],"block_behaviors":[{"block":"p0_bad:b","light_dampening":16}]})");
    write(root/"mods/official_heaven/mod.json",R"({"id":"official_heaven","version":"1.0.0","api":1})");
    {
        Plugins::PluginManager plugins({assets,root});plugins.initialize();
        for(const auto& info:plugins.plugins())if(info.manifest.id=="official_heaven")check(info.manifest.builtin?info.active:!info.active,"external package spoofed official identity");
        const auto ore=Plugins::resolveBlock("capability_native:ore"),plant=Plugins::resolveBlock("p0_data:plant");const auto plank=Plugins::resolveItem("capability_native:plank");
        check(getLightDampening(ore)==3&&isLeafBlock(ore)&&isWoodPlankItem(plank)&&fuelTicks(plank)==0,"native V2 semantic consumer mismatch");
        check(supportsBiomePlant(BlockId::FERN,ore),"native plant ignored third-party fertile substrate tag");
        ChunkStore lightChunks;auto* tunnel=lightChunks.get(0,0);
        tunnel->loadRawBlocks(std::vector<uint16_t>(Config::CHUNK_VOLUME,static_cast<uint16_t>(BlockId::STONE)));
        for(int x=0;x<16;++x)tunnel->setBlock(x,64,8,BlockId::AIR);
        tunnel->setBlock(0,64,8,BlockId::TORCH);tunnel->setBlock(1,64,8,ore);tunnel->generated=true;
        WorldLighting lighting(lightChunks);lighting.rebuild();
        check(tunnel->getBlockLight(1,64,8)==11&&tunnel->getBlockLight(2,64,8)==10,"production light propagation ignored V2 attenuation");
        std::array<ItemId,9> grid{};grid[0]=grid[2]=plank;
        const auto* sticks=findCraftingRecipe(grid,2,2);check(sticks&&sticks->output.id==ItemId::STICK&&sticks->output.count==4,"third-party plank did not match native recipe");
        check(getBlockDrops(ore,{},0).empty(),"semantic drops bypassed harvest tier");
        check(getBlockDrops(ore,{ItemId::STONE_PICKAXE,1,0},1).empty(),"semantic drop probability ignored");
        const auto drops=getBlockDrops(ore,{ItemId::STONE_PICKAXE,1,0},2);check(drops.size()==1&&drops[0].id==plank&&drops[0].count==3,"deterministic range/probability mismatch");
        check(getLightDampening(plant)==0&&getBlockDrops(plant,{},0).empty()&&supportsBiomePlant(plant,ore)&&!supportsBiomePlant(plant,BlockId::STONE),"JSON explicit zero/empty/substrate mismatch");
        rejects([]{Plugins::resolveBlock("p0_bad:b");},"bad semantic registration did not roll back");
        check(!Plugins::content().materials.count("p0_bad:m"),"bad semantic material survived rollback");
        GameSession session(root/"native-session");GameSessionTestAccess::prepareLanScene(session);auto& p=GameSessionTestAccess::player(session);p.inventory().slot(0)={Plugins::resolveItem("capability_native:context_staff"),1,0};
        MC_ActionTarget target{};check(session.pluginActionTarget(0,"capability_native:context",true,target)&&target.valid&&target.position[0]==2.5,"native V2 view/services failed");
        InputState input;input.setVirtual(InputAction::Sneak,1);input.update({});p.handleMovement(input,0);check(session.pluginActionTarget(0,"capability_native:context",true,target)&&target.valid&&target.position[0]==-1.5,"native V2 did not receive sneak intent");
        input.clearVirtual();input.update({});p.handleMovement(input,0);
        size_t nativeIndex=0;for(size_t i=0;i<plugins.plugins().size();++i)if(plugins.plugins()[i].manifest.id=="capability_native")nativeIndex=i;
        MC_PluginContext context{};context.manager=&plugins;context.index=nativeIndex;
        const auto* actions=static_cast<const MC_PlayerActionsV2*>(plugins.host().query_extension(&context,MC_PLAYER_ACTIONS_EXTENSION,2));check(actions&&actions->v1,"action V2 lacks frozen V1 table");
        plugins.actionState=[&](uint64_t id){return session.pluginActionState(id);};plugins.executeAction=[&](uint64_t id,const std::string& key){return session.pluginExecuteAction(id,key);};
        auto ready=Plugins::event(MC_WORLD_READY);plugins.dispatch(ready);
        MC_ActionRequest request{sizeof(request),0,"capability_native:context"};uint64_t receipt=0;
        check(actions->v1->request(&context,&request,&receipt)==0,"V2 action queue refused");
        input.setVirtual(InputAction::Sneak,1);input.update({});p.handleMovement(input,0);plugins.flushOperations();
        MC_ActionResult result{};result.size=sizeof(result);check(actions->v1->result(&context,receipt,&result)==0&&result.status==MC_ACTION_SUCCEEDED&&p.getPosition().x==-1.5,"execution reused preview instead of current sneak intent");
        check(actions->v1->request(&context,&request,&receipt)==0,"second V2 queue refused");++p.inventory().slot(0).damage;plugins.flushOperations();
        check(actions->v1->result(&context,receipt,&result)==0&&result.status==MC_ACTION_FAILED&&p.getPosition().x==-1.5,"changed held-item state survived queued action validation");
        check(Plugins::resolveDimension("capability_native:flat")>=2,"third-party dimension stole reserved handle");
        const auto signature=Lan::contentSignature(Plugins::content().networkDescription);auto& b=Plugins::content().extensions.blockBehaviors.at(static_cast<uint16_t>(ore));++b.light;plugins.describeCapabilities();check(signature!=Lan::contentSignature(Plugins::content().networkDescription),"semantic change omitted from LAN identity");--b.light;plugins.describeCapabilities();
        session.abortPluginWorld();plugins.shutdown();
    }
    std::cout<<"Heaven remaining P0 integration passed\n";return 0;
}
}
