#pragma once
#include "plugins/PluginManager.h"
#include "network/PlayerProfile.h"
#include <fstream>

namespace DimensionIntegration {
inline void check(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
inline std::string bytes(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
inline int run(const std::filesystem::path& assets,const std::filesystem::path& module) {
    const auto root=std::filesystem::temp_directory_path()/"minecraftc-dimension-integration";
    std::filesystem::remove_all(root);std::filesystem::create_directories(root);
    auto package=[&](const std::string& id,const std::string& content,bool visual=false){const auto path=root/"mods"/id;std::filesystem::create_directories(path);std::ofstream(path/"mod.json")<<"{\"id\":\""<<id<<"\",\"version\":\"1.0.0\",\"api\":1,\"visual_only\":"<<(visual?"true":"false")<<",\"data\":\"content.json\"}";std::ofstream(path/"content.json")<<content;};
    package("dimension_bad",R"({"dimensions":[{"key":"dimension_bad:lost","revision":"1","generator":"dimension_bad:missing","generator_revision":"1"}]})");
    package("dimension_data",R"({"dimensions":[{"key":"dimension_data:rainland","revision":"1","generator":"dimension_data:flat","generator_revision":"1","weather":2,"spawn":1,"spawn_position":[0.5,65.01,0.5]}],"generators":[{"key":"dimension_data:flat","revision":"1","dimension":"dimension_data:rainland","height":64,"water_level":-65}]})");
    package("dimension_visual",R"({"dimensions":[{"key":"dimension_visual:lost","revision":"1","generator":"dimension_visual:missing","generator_revision":"1"}]})",true);
    package("dimension_dependency",R"({"dimensions":[{"key":"dimension_dependency:lost","revision":"1","generator":"dimension_data:flat","generator_revision":"1"}]})");
    const auto native=root/"mods"/"dimension_demo";std::filesystem::create_directories(native);std::filesystem::copy_file(module,native/module.filename());
    std::ofstream(native/"mod.json")<<"{\"id\":\"dimension_demo\",\"version\":\"1.0.0\",\"api\":1,\"native\":{\"linux\":\""<<module.filename().string()<<"\",\"windows\":\""<<module.filename().string()<<"\",\"macos\":\""<<module.filename().string()<<"\"}}";
    Plugins::PluginManager plugins({assets,root});plugins.initialize();
    const auto id=Plugins::resolveDimension("dimension_demo:plateau"),rainId=Plugins::resolveDimension("dimension_data:rainland");
    const auto dimension=static_cast<DimensionId>(id),rain=static_cast<DimensionId>(rainId);
    check(id>=2&&rainId>=2&&id!=rainId&&Plugins::content().extensions.dimensions.size()==4,"native/data dimensions and rollback");
    for(const auto& info:plugins.plugins())if(info.manifest.id=="dimension_bad"||info.manifest.id=="dimension_visual"||info.manifest.id=="dimension_dependency")check(!info.active,"invalid dimension package rejected");
    size_t index=0;for(;index<plugins.plugins().size();++index)if(plugins.plugins()[index].manifest.id=="dimension_demo")break;
    MC_PluginContext ctx;ctx.manager=&plugins;ctx.index=index;
    const auto* api=static_cast<const MC_DimensionsV1*>(plugins.host().query_extension(&ctx,MC_DIMENSIONS_EXTENSION,1));
    check(api&&api->find(&ctx,"dimension_demo:plateau")==id&&api->find(&ctx,"missing:dimension")==MC_INVALID_DIMENSION,"SDK find");
    MC_DimensionDefinition definition{};definition.size=sizeof(definition);
    check(api->query(&ctx,id,&definition)==0&&definition.weather==MC_WEATHER_CLEAR&&definition.disable_natural_spawning,"SDK query");
    uint32_t duplicate=0;check(api->register_dimension(&ctx,&definition,&duplicate)!=0,"frozen registration rejected");
    check(api->travel(&ctx,0,id)!=0,"travel outside world rejected");
    const auto config=Plugins::dimensionContent(id).generation;
    WorldGenerator generator(12,WorldType::Normal,dimension,config),same(12,WorldType::Normal,dimension,config),other(13,WorldType::Normal,dimension,config);
    auto chunk=std::make_unique<Chunk>(-1,-1),again=std::make_unique<Chunk>(-1,-1),different=std::make_unique<Chunk>(-1,-1);
    generator.generate(*chunk);same.generate(*again);other.generate(*different);
    std::vector<uint16_t> a,b,c;chunk->copyRawBlocks(a);again->copyRawBlocks(b);different->copyRawBlocks(c);check(a==b&&a!=c,"new dimension deterministic seeded terrain");
    std::vector<std::unique_ptr<Chunk>> owned;std::vector<Chunk*> region;for(int z=-3;z<0;++z)for(int x=-3;x<0;++x){owned.push_back(std::make_unique<Chunk>(x,z));region.push_back(owned.back().get());}
    std::vector<RegionGenerationData::PendingBlock> pending;generator.generateRegion(-3,-3,3,6,region,pending);owned.back()->copyRawBlocks(b);check(a==b,"new dimension negative region/singleton equality");
    const auto lod=buildApproximateLodTile(generator,{-1,-1,0});check(!lod.at(0,0).spans.empty(),"plugin dimension LOD");
    WorldMetadata metadata;metadata.displayName="Dimensions";metadata.seed=12;metadata.generationVersion=WorldGenContext::GENERATION_VERSION;metadata.activeDimension=dimension;
    for(const auto& entry:Plugins::content().extensions.dimensions)metadata.generation[entry.first]=entry.second.generation;
    metadata.dimensions[id].playerPosition={2.5,49.01,.5};metadata.dimensions[id].positioned=true;metadata.dimensions[id].hasSafePosition=true;metadata.dimensions[id].safePlayerPosition={2.5,49.5625,.5};metadata.dimensions[id].bedSpawn=glm::ivec3(1,49,2);
    SaveStore store(root/"keyed-world");store.saveMetadata(metadata);check(store.loadMetadata().activeDimension==dimension,"keyed save roundtrip");
    Lan::ChunkAddress address{dimension,-1,2};const auto chunkWire=Lan::encodeChunkSnapshot({address,1,1,a});
    Lan::WorldInfo info;info.generation=metadata.generation;const auto worldWire=Lan::encodeWorldInfo(info);
    Lan::EntityBatch entities;entities.dimension=dimension;const auto entityWire=Lan::encodeEntityBatch(entities);
    Lan::PlayerProfile profile;profile.identity={Lan::randomToken(),Lan::randomToken(),"Dimension player"};profile.dimension=dimension;profile.positions[id]={2.5,49.01,.5};profile.positioned[id]=true;profile.safePositions[id]=profile.positions[id];profile.beds[id]={1,49,2};const auto profileWire=Lan::encodeProfile(profile);
    // A different local handle must resolve all persistent and wire identities correctly.
    auto saved=Plugins::content().extensions.dimensions.at(id);Plugins::content().extensions.dimensions.erase(id);Plugins::content().extensions.dimensions.emplace(20,saved);
    Plugins::content().extensions.generators.at(config.generator).dimension=20;
    const auto loaded=store.loadMetadata();check(static_cast<uint32_t>(loaded.activeDimension)==20&&loaded.dimensions.at(20).bedSpawn==metadata.dimensions.at(id).bedSpawn&&loaded.dimensions.at(20).safePlayerPosition==metadata.dimensions.at(id).safePlayerPosition,"save remaps dimension key");
    check(static_cast<uint32_t>(Lan::decodeChunkSnapshot(chunkWire).address.dimension)==20,"chunk protocol remaps key");
    check(Lan::decodeWorldInfo(worldWire).generation.count(20)&&static_cast<uint32_t>(Lan::decodeEntityBatch(entityWire).dimension)==20,"world/entity protocols remap key");
    const auto remapped=Lan::decodeProfile(profileWire);check(static_cast<uint32_t>(remapped.dimension)==20&&remapped.beds.at(20)==profile.beds.at(id),"profile remaps key and per-dimension state");
    Plugins::content().extensions.dimensions.erase(20);Plugins::content().extensions.dimensions.emplace(id,saved);Plugins::content().extensions.generators.at(config.generator).dimension=id;
    const auto before=bytes(root/"keyed-world"/"level.bin");Plugins::content().extensions.dimensions.erase(id);bool refused=false;try{store.loadMetadata();}catch(const std::exception&){refused=true;}check(refused&&bytes(root/"keyed-world"/"level.bin")==before,"missing dimension zero-write refusal");Plugins::content().extensions.dimensions.emplace(id,saved);
    auto changed=Plugins::content().extensions.dimensions.at(id);Plugins::content().extensions.dimensions.at(id).revision="2";refused=false;
    try{store.loadMetadata();}catch(const std::exception&){refused=true;}check(refused&&bytes(root/"keyed-world"/"level.bin")==before,"dimension policy revision zero-write refusal");Plugins::content().extensions.dimensions.at(id)=changed;
    auto oldProfile=profileWire;oldProfile[4]=3;oldProfile[5]=0;uint64_t checksum=14695981039346656037ULL;for(size_t i=0;i<oldProfile.size()-8;++i){checksum^=oldProfile[i];checksum*=1099511628211ULL;}for(int i=0;i<8;++i)oldProfile[oldProfile.size()-8+i]=static_cast<uint8_t>(checksum>>(i*8));
    refused=false;try{Lan::decodeProfile(oldProfile);}catch(const Lan::ProtocolError& error){refused=std::string(error.what())=="Unsupported player profile format";}check(refused,"old profile format explicitly refused");
    const int distance=Config::RENDER_DISTANCE;Config::RENDER_DISTANCE=0;
    {
        GameSession session(root/"saves");EntityAiTestRenderer renderer;session.configureLod({false,16,LodAggressiveness::PowerSaver,LodPrecision::Low});
        const auto world=session.createWorld("Dimension session",12,GameMode::Survival,Difficulty::Normal,true,WorldType::Superflat);session.startWorld(world,true,0);
        auto ready=[&]{RuntimeClock clock;for(int i=0;i<4000;++i){if(session.advanceLoading(&renderer,clock.now()))return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return false;};
        check(ready(),"normal loading");
        plugins.travelPlayer=[&](uint64_t player,uint32_t target){return session.pluginTravel(player,target);};
        check(api->travel(&ctx,0,id)==0&&session.activeDimension()==DimensionId::Overworld,"SDK travel queues without immediate mutation");plugins.flushOperations();
        check(session.activeDimension()==dimension,"queued SDK session authority travel");check(ready(),"plugin dimension loading gate");
        check(!session.weatherState().raining()&&session.playerState().getPosition().y==49.01,"default generator and clear policy");
        double mobPosition[3]{.5,49.01,.5};check(session.pluginSpawnEntity(id,static_cast<uint16_t>(EntityType::Cow),mobPosition),"explicit mob spawning ignores natural-spawn disable");
        const auto entered=session.playerState().getPosition();GameSessionTestAccess::player(session).setPosition(entered+glm::dvec3(2,0,0));
        check(session.switchDimension(rain,0)&&ready()&&session.weatherState().raining(),"fixed coordinate and rain policy");
        check(session.switchDimension(dimension,0)&&ready()&&session.playerState().getPosition()==entered+glm::dvec3(2,0,0),"per-dimension re-entry position");
        Localization localization;const auto fixed=parseCommand("/weather rain");check(fixed.command.has_value(),"weather parser");session.executeCommand(*fixed.command,localization);check(!session.weatherState().raining(),"fixed weather rejects command");
        GameSessionTestAccess::player(session).setPosition({.5,Config::WORLD_MIN_Y-3,.5});check(session.handleVoidFall(0,{})&&session.activeDimension()==DimensionId::Overworld&&ready(),"declarative void return");
        const auto command=parseCommand("/dimension dimension_demo:plateau");check(command.command&&session.executeCommand(*command.command,localization).teleported&&ready(),"dimension command authority travel");
        session.respawn();check(session.activeDimension()==DimensionId::Overworld&&ready(),"declarative death target");
        check(session.switchDimension(rain,0)&&ready(),"enter rainland");session.respawn();check(session.activeDimension()==rain&&ready(),"default death stays in dimension");
        if(!session.openLanRoom(0,2,true))throw std::runtime_error("dimension LAN host starts: "+session.lanError());
        GameSession client(root/"client"/"saves");client.configureLod({false,16,LodAggressiveness::PowerSaver,LodPrecision::Low});check(client.joinLanRoom("::1",session.lanPort(),0),"dimension LAN client connects");
        double now=0;auto pump=[&]{now+=.005;session.pollLan(now);client.pollLan(now);GameSessionTestAccess::world(client).processCompletedGenerations();std::this_thread::sleep_for(std::chrono::milliseconds(1));};
        for(int i=0;i<1000&&!client.lanWorldReady();++i){pump();}
        check(client.lanWorldReady()&&!client.lanConnectionFailed(),"dimension LAN registry handshake");
        const auto roster=session.roomRoster();check(roster.size()==2,"guest joined");check(session.pluginTravel(roster.back().id,id),"host travels guest to plugin dimension");
        for(int i=0;i<1000&&client.activeDimension()!=dimension;++i){pump();}
        check(client.activeDimension()==dimension&&!client.pluginTravel(0,rainId),"client follows authority and cannot mutate dimension");
        check(session.activeDimension()==rain,"host and guest independent dimensions");client.leaveWorld();session.closeLanRoom();
        // Remove support and block the fixed landing: failure must retain source.
        auto& terrain=GameSessionTestAccess::world(session);terrain.setBlock(0,64,0,BlockId::AIR);
        check(session.switchDimension(dimension,0)&&ready(),"leave edited dimension");check(!session.switchDimension(rain,0)&&session.activeDimension()==dimension,"invalid fixed landing keeps source dimension");
        bool failed=false;session.saveNow([&]{failed=true;});check(!failed,"multi-dimension save");session.leaveWorld();session.startWorld(world,false,0);check(session.activeDimension()==dimension&&ready(),"active plugin dimension reload");session.leaveWorld();
    }
    plugins.travelPlayer={};
    Config::RENDER_DISTANCE=distance;
    plugins.shutdown();Plugins::PluginManager safe({assets,root});safe.initialize(true);check(Plugins::content().extensions.dimensions.size()==1&&!Plugins::validDimension(1),"safe mode retains only the host Overworld");safe.shutdown();
    std::filesystem::remove_all(root);std::cout<<"Dimension registry/runtime integration passed\n";return 0;
}
}
