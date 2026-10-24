#pragma once
#include "plugins/PluginManager.h"
#include "world/ChunkStore.h"
#include "core/AssetStore.h"
#include <fstream>
#include <future>
#include <limits>

namespace GenerationPolicyV2Integration {
inline void heavenReference(const std::filesystem::path& root);
inline void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
inline void clearFault(){std::lock_guard<std::mutex> lock(Plugins::generationFaultMutex());Plugins::generationFault().clear();}
template<class F> void rejected(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}clearFault();check(failed,"invalid pure output accepted");}
inline bool equal(const RegionGenerationData::PendingBlock& a,const RegionGenerationData::PendingBlock& b){
    return std::tie(a.worldX,a.worldY,a.worldZ,a.id,a.lootSeed,a.lootKey,a.lootRevision,a.structureKey,a.structureRevision)==
        std::tie(b.worldX,b.worldY,b.worldZ,b.id,b.lootSeed,b.lootKey,b.lootRevision,b.structureKey,b.structureRevision);
}
inline int run(const std::filesystem::path& assets,const std::filesystem::path& module,const std::filesystem::path& badModule){
    const auto root=std::filesystem::temp_directory_path()/"minecraftc-policy-v2";
    std::filesystem::remove_all(root);const auto package=root/"mods/worldgen_policy_v2";
    std::filesystem::create_directories(package);
    auto install=[&](const std::filesystem::path& binary){
        std::filesystem::copy_file(binary,package/binary.filename(),std::filesystem::copy_options::overwrite_existing);
        std::ofstream(package/"mod.json")<<"{\"id\":\"worldgen_policy_v2\",\"version\":\"1.0.0\",\"api\":1,\"network_compatibility\":\"policy-v2-1\",\"native\":{\"windows\":\""<<binary.filename().string()<<"\",\"linux\":\""<<binary.filename().string()<<"\",\"macos\":\""<<binary.filename().string()<<"\"}}";
    };
    install(module);AssetStore rootedAssets(assets);Plugins::PluginManager plugins({assets,root});plugins.initialize();
    const uint32_t id=Plugins::resolveDimension("worldgen_policy_v2:layered");const auto dim=static_cast<DimensionId>(id);
    check(id>=3,"native V2 dimension registration");
    check(plugins.extension(nullptr,MC_DIMENSIONS_EXTENSION,1)&&plugins.extension(nullptr,MC_DIMENSIONS_EXTENSION,2)&&
        plugins.extension(nullptr,MC_DIMENSIONS_EXTENSION,3)&&!plugins.extension(nullptr,MC_DIMENSIONS_EXTENSION,4)&&plugins.extension(nullptr,MC_STRUCTURES_EXTENSION,1)&&
        plugins.extension(nullptr,MC_STRUCTURES_EXTENSION,2)&&plugins.extension(nullptr,MC_STRUCTURES_EXTENSION,3)&&!plugins.extension(nullptr,MC_STRUCTURES_EXTENSION,4),"version negotiation");
    WorldGenerator g(12,WorldType::Normal,dim),same(12,WorldType::Normal,dim),other(13,WorldType::Normal,dim);
    const auto spawn=g.sampleDimensionSpawn();check(spawn&&*spawn==glm::dvec3(.5,101.01,.5)&&same.sampleDimensionSpawn()==spawn&&other.sampleDimensionSpawn()!=spawn,"pure seeded lower-layer spawn and actual parameters");
    check(g.sampleTerrainColumn(0,0).height==240,"fixture highest surface differs from spawn layer");
    check(std::async(std::launch::async,[&]{return g.sampleDimensionSpawn();}).get()==spawn,"concurrent spawn equality");
    auto& d=Plugins::content().extensions.dimensions.at(id);const auto sample=d.sampleSpawn;
    d.sampleSpawn=[](const MC_GenerationContext*,double*,void*)->int32_t{return -1;};rejected([&]{g.sampleDimensionSpawn();});
    d.sampleSpawn=[](const MC_GenerationContext*,double*,void*)->int32_t{throw std::runtime_error("spawn fault");};rejected([&]{g.sampleDimensionSpawn();});
    d.sampleSpawn=[](const MC_GenerationContext*,double* out,void*)->int32_t{out[0]=0;out[1]=320;out[2]=0;return 0;};rejected([&]{g.sampleDimensionSpawn();});
    d.sampleSpawn=[](const MC_GenerationContext*,double*,void*)->int32_t{return 0;};rejected([&]{g.sampleDimensionSpawn();});d.sampleSpawn=sample;
    const std::string key="worldgen_policy_v2:shrine";auto& s=Plugins::content().extensions.structures.at(key);
    const auto located=g.locatePluginStructure(key,-48,-48,64);check(located&&located->anchor[0]==-48&&located->anchor[1]==180&&located->variant==12,"custom placement locate and tie order");
    const auto blocks=g.pluginStructureBlocks(-64,-64,48,48);
    check(blocks.size()==4,"final writer owns duplicate positions and metadata");
    bool low=false,high=false,air=false,chest=false;
    for(const auto& b:blocks){low=low||(b.worldY==92&&b.id==BlockId::STONE);high=high||b.worldY==268;air=air||(b.worldY==181&&b.id==BlockId::AIR);
        if(b.id==BlockId::CHEST){chest=true;check(b.lootSeed==0&&b.lootRevision=="1"&&b.structureRevision=="1","exact zero loot seed/revisions");}}
    check(low&&high&&air&&chest,"signed vertical extent and AIR overwrite");
    const auto async=std::async(std::launch::async,[&]{return g.pluginStructureBlocks(-64,-64,48,48);}).get();
    check(async.size()==blocks.size(),"worker block count");for(size_t i=0;i<blocks.size();++i)check(equal(blocks[i],async[i]),"worker metadata equality");
    std::vector<std::unique_ptr<Chunk>> owned;std::vector<Chunk*> region;
    for(int z=-4;z<-1;++z)for(int x=-4;x<-1;++x){owned.push_back(std::make_unique<Chunk>(x,z));region.push_back(owned.back().get());}
    std::reverse(region.begin(),region.end());std::vector<RegionGenerationData::PendingBlock> pending;
    g.generateRegion(-4,-4,3,6,region,pending);
    for(auto* c:region){auto single=std::make_unique<Chunk>(c->cx,c->cz);g.generate(*single);std::vector<uint16_t> a,b;c->copyRawBlocks(a);single->copyRawBlocks(b);check(a==b,"negative region/singleton equality");
        check(c->pluginStructures.size()==single->pluginStructures.size(),"region metadata count");
        for(size_t i=0;i<c->pluginStructures.size();++i)check(equal(c->pluginStructures[i],single->pluginStructures[i]),"region metadata equality");
        for(const auto& v:c->pluginStructures){const auto spans=g.samplePluginLodColumn(v.worldX,v.worldZ,1);uint16_t block=0;for(const auto& span:spans)if(v.worldY>=span.bottom&&v.worldY<=span.top)block=span.block;check(block==uint16_t(v.id),"LOD exact structure authority");}}
    const auto place=s.placements;const auto build=s.buildV2;
    s.placements=[](const MC_StructureContext*,int32_t,int32_t,MC_StructureInstance*,uint32_t cap,uint32_t* count,void*)->int32_t{*count=cap+1;return 0;};rejected([&]{g.pluginStructureBlocks(0,0);});
    s.placements=[](const MC_StructureContext*,int32_t,int32_t,MC_StructureInstance* out,uint32_t,uint32_t* count,void*)->int32_t{*count=1;out[0]={{30000001,100,0},0,0};return 0;};rejected([&]{g.locatePluginStructure(key,0,0,64);});s.placements=place;
    s.buildV2=[](const MC_StructureContext*,MC_StructureBlockV2*,uint32_t,uint32_t*,void*)->int32_t{throw std::runtime_error("build fault");};
    auto untouched=std::make_unique<Chunk>(0,0);untouched->setBlock(0,100,0,BlockId::DIAMOND_ORE);rejected([&]{g.generate(*untouched);});check(!untouched->generated&&untouched->getBlock(0,100,0)==BlockId::DIAMOND_ORE,"callback failure publishes nothing");
    s.buildV2=[](const MC_StructureContext*,MC_StructureBlockV2* out,uint32_t,uint32_t* count,void*)->int32_t{*count=1;out[0]={{0,-89,0,1,nullptr},0};return 0;};rejected([&]{g.pluginStructureBlocks(0,0,48,48);});s.buildV2=build;
    const auto fingerprint=g.pluginStructureFingerprint();const auto description=Plugins::content().networkDescription;
    s.minY=-87;plugins.describeCapabilities();check(g.pluginStructureFingerprint()!=fingerprint&&Lan::contentSignature(description)!=Lan::contentSignature(Plugins::content().networkDescription),"V2 bounds invalidate cache and LAN identity");s.minY=-88;plugins.describeCapabilities();
    const int distance=Config::RENDER_DISTANCE;Config::RENDER_DISTANCE=0;
    {
        GameSession session(root/"host/saves");EntityAiTestRenderer renderer;
        session.configureLod({false,16,LodAggressiveness::PowerSaver,LodPrecision::Low});
        const auto world=session.createWorld("Policy V2",12,GameMode::Survival,Difficulty::Normal,true,WorldType::Superflat);
        session.startWorld(world,true,0);
        auto ready=[&]{RuntimeClock clock;for(int i=0;i<4000;++i){if(session.advanceLoading(&renderer,clock.now()))return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return false;};
        check(ready()&&session.switchDimension(dim,0)&&ready()&&session.playerState().getPosition()==*spawn,"session enters sampled lower layer");
        auto& terrain=GameSessionTestAccess::world(session);
        terrain.update({16.5,181,16.5});terrain.enqueueGeneration();terrain.waitForInitialGeneration(1500);terrain.processCompletedGenerations(false);
        const auto* loot=terrain.getBlockEntity({17,181,16});check(loot&&loot->lootSeed==0&&loot->structureKey==key,"streaming registers final chest metadata");
        check(!terrain.getBlockEntity({16,181,16}),"AIR overwrite leaves no ghost chest");
        check(session.switchDimension(DimensionId::Overworld,0)&&ready(),"leave sampled dimension");
        check(session.switchDimension(dim,0)&&ready()&&session.playerState().getPosition()==*spawn,"sampled re-entry");
        GameSessionTestAccess::player(session).setPosition(*spawn+glm::dvec3(1,0,0));
        check(session.switchDimension(DimensionId::Overworld,0)&&ready()&&session.switchDimension(dim,0)&&ready()&&session.playerState().getPosition()==*spawn+glm::dvec3(1,0,0),"valid historical position preferred");
        terrain.setBlock(1,101,0,BlockId::STONE);
        check(session.switchDimension(DimensionId::Overworld,0)&&ready()&&session.switchDimension(dim,0)&&ready()&&session.playerState().getPosition()==*spawn,"invalid history falls back to sampled layer");terrain.setBlock(1,101,0,BlockId::AIR);
        terrain.setBlock(0,101,0,BlockId::FIRE);
        check(session.switchDimension(DimensionId::Overworld,0)&&ready()&&!session.switchDimension(dim,0)&&session.activeDimension()==DimensionId::Overworld,"dangerous sample refuses travel without surface fallback");terrain.setBlock(0,101,0,BlockId::AIR);
        check(session.switchDimension(dim,0)&&ready(),"restore sample landing");session.respawn();check(ready()&&session.playerState().getPosition()==*spawn,"respawn uses sample");
        check(terrain.placeBed({3,101,0},BedDirection::South),"sampled dimension bed fixture");
        GameSessionTestAccess::dimensionBed(session,id,glm::ivec3(3,101,0));session.respawn();
        check(ready()&&session.playerState().getPosition().x==3.5&&session.playerState().getPosition().y<102,"valid bed wins over sampled spawn");
        auto& sourcePolicy=Plugins::content().extensions.dimensions.at(0);const auto deathTarget=sourcePolicy.deathTarget;
        sourcePolicy.deathTarget=d.key;terrain.setBlock(0,101,0,BlockId::FIRE);
        check(session.switchDimension(DimensionId::Overworld,0)&&ready(),"leave target bed for cross-dimension respawn");
        GameSessionTestAccess::dimensionPosition(session,id,*spawn);
        session.respawn();check(session.activeDimension()==dim&&ready()&&session.playerState().getPosition().x==3.5,"cross-dimension respawn reuses safe bed despite unsafe sample");
        sourcePolicy.deathTarget=deathTarget;terrain.setBlock(0,101,0,BlockId::AIR);
        terrain.setBlock(3,101,0,BlockId::AIR);GameSessionTestAccess::dimensionBed(session,id,std::nullopt);session.respawn();check(ready()&&session.playerState().getPosition()==*spawn,"removed bed falls back to sampled spawn");
        bool failed=false;session.saveNow([&]{failed=true;});check(!failed,"sampled dimension save");session.leaveWorld();
        SaveStore saved(WorldCatalog(root/"host/saves").open(world));
        const auto metadataPath=saved.worldDirectory()/"level.bin";
        auto bytes=[&]{std::ifstream f(metadataPath,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});};
        const auto before=bytes();d.revision="2";bool refused=false;
        try{session.startWorld(world,false,0);}catch(const std::exception&){refused=true;}
        check(refused&&bytes()==before&&!session.hasWorldStore(),"spawn policy revision refuses saved world without writes");d.revision="1";
        session.startWorld(world,false,0);check(ready()&&session.playerState().getPosition()==*spawn,"sampled dimension reload");
        check(session.openLanRoom(0,2,true),"policy LAN host");GameSession client(root/"client/saves");client.configureLod({false,16,LodAggressiveness::PowerSaver,LodPrecision::Low});
        check(client.joinLanRoom("::1",session.lanPort(),0),"policy LAN join");double now=0;
        auto pump=[&]{now+=.005;session.pollLan(now);client.pollLan(now);GameSessionTestAccess::world(client).processCompletedGenerations();std::this_thread::sleep_for(std::chrono::milliseconds(1));};
        for(int i=0;i<1500&&!client.lanWorldReady()&&!client.lanConnectionFailed();++i)pump();
        if(!client.lanWorldReady())std::cerr<<GameSessionTestAccess::replicaLoadingDiagnostic(client)<<'\n';
        check(client.lanWorldReady(),"policy LAN handshake");
        const auto peer=GameSessionTestAccess::replicaPeer(client);check(session.pluginTravel(peer,id),"host travels guest to sampled layer");
        for(int i=0;i<1500&&client.activeDimension()!=dim;++i)pump();
        check(client.activeDimension()==dim&&GameSessionTestAccess::guest(session,peer).player.getPosition()==*spawn,"LAN guest uses identical sample");
        client.leaveWorld();session.closeLanRoom();session.leaveWorld();
    }
    Config::RENDER_DISTANCE=distance;
    heavenReference(root);
    plugins.shutdown();install(badModule);Plugins::PluginManager bad({assets,root});bad.initialize();
    check(!Plugins::content().extensions.structures.count(key)&&!Plugins::content().extensions.generators.count("worldgen_policy_v2:terrain"),"invalid V2 descriptor rolls back whole package");bad.shutdown();
    std::filesystem::remove_all(root);std::cout<<"Seeded spawn and structure V2 passed\n";return 0;
}
}
#include "HeavenPolicyV2Reference.h"
