#pragma once
#include "plugins/PluginManager.h"
#include "plugins/ContentMutation.h"
#include "plugins/ContentQueries.h"
#include "plugins/GenerationValidation.h"
#include "entity/EntityPresenter.h"
#include "network/EntityProtocol.h"
#include "network/StateProtocol.h"
#include "world/LodTerrainSystem.h"
#include "core/AssetStore.h"
#include <fstream>
#include <future>

namespace PluginExtensionIntegration {
inline int run(const std::filesystem::path& assets,const std::filesystem::path& entityModule,const std::filesystem::path& terrainModule) {
    const auto root=std::filesystem::temp_directory_path()/"minecraftc-plugin-extension-tests";
    std::filesystem::remove_all(root);std::filesystem::create_directories(root);
    auto package=[&](const char* name,const std::filesystem::path& module){const auto path=root/"mods"/name;std::filesystem::create_directories(path);std::filesystem::copy_file(module,path/module.filename());
        std::ofstream manifest(path/"mod.json");manifest<<"{\"id\":\""<<name<<"\",\"version\":\"1.0.0\",\"api\":1,\"network_compatibility\":\"extension-test-1\",\"native\":{\"linux\":\""<<module.filename().string()<<"\",\"windows\":\""<<module.filename().string()<<"\",\"macos\":\""<<module.filename().string()<<"\"}}";};
    package("entity_demo",entityModule);package("terrain_demo",terrainModule);
    const auto modelRoot=root/"mods"/"model_data";std::filesystem::create_directories(modelRoot);
    std::filesystem::copy_file(assets/"models"/"entities"/"cow.glb",modelRoot/"custom.glb");
    std::filesystem::copy_file(assets/"models"/"entities"/"cow.anim.json",modelRoot/"custom.anim.json");
    {std::ofstream manifest(modelRoot/"mod.json");manifest<<R"({"id":"model_data","version":"1.0.0","api":1,"data":"content.json"})";}
    {std::ofstream content(modelRoot/"content.json");content<<R"({"entities":[{"key":"model_data:custom","name":"Custom model","model":"custom.glb","template_type":1,"health":10}]})";}
    AssetStore rootedAssets(assets);
    Plugins::PluginManager plugins({assets,root});plugins.initialize();
    auto check=[](bool value,const char* reason){if(!value)throw std::runtime_error(reason);};
    check(Plugins::mutableContentState().runtimeFault.empty(),"extension initialization fault");
    for(const auto& info:plugins.plugins())if(info.enabled)check(info.active,"example package rejected");
    EntityModelRegistry models;models.loadAll(assets);
    check(!models.definition(static_cast<EntityType>(Plugins::resolveEntity("model_data:custom"))).usesPlaceholder,"package model/animation loads with rooted Title Storage active");
    const auto type=Plugins::resolveEntity("entity_demo:grazer");check(type>=4096,"custom entity runtime range");
    check(Plugins::entityContent(2)&&Plugins::entityContent(2)->health==24,"explicit builtin override");
    const auto* item=Plugins::pluginItem(Plugins::resolveItem("entity_demo:grazer_egg"));check(item&&item->spawnEntity==type&&item->properties.kind==ItemKind::SpawnEgg,"custom spawn egg registration");
    Plugins::GenerationConfig config{"terrain_demo:plateau","1","{\"base_height\":48}",{{"terrain_demo:markers","1"}}};
    WorldGenerator generator(12,WorldType::Normal,DimensionId::Overworld,config);
    auto singleton=std::make_unique<Chunk>(-1,-1);generator.generate(*singleton);
    std::vector<std::unique_ptr<Chunk>> owned;std::vector<Chunk*> region;
    for(int z=-3;z<0;++z)for(int x=-3;x<0;++x){owned.push_back(std::make_unique<Chunk>(x,z));region.push_back(owned.back().get());}
    std::vector<RegionGenerationData::PendingBlock> pending;generator.generateRegion(-3,-3,3,6,region,pending);
    std::vector<uint16_t> expected,actual;singleton->copyRawBlocks(expected);owned.back()->copyRawBlocks(actual);check(expected==actual,"region/singleton negative boundary equality");
    WorldGenerator same(12,WorldType::Normal,DimensionId::Overworld,config),different(13,WorldType::Normal,DimensionId::Overworld,config);
    auto second=std::make_unique<Chunk>(-1,-1),other=std::make_unique<Chunk>(-1,-1);same.generate(*second);different.generate(*other);second->copyRawBlocks(actual);check(actual==expected,"same-seed generation equality");other->copyRawBlocks(actual);check(actual!=expected,"different-seed generation variation");
    auto a=std::async(std::launch::async,[&]{auto c=std::make_unique<Chunk>(-1,-1);generator.generate(*c);std::vector<uint16_t> out;c->copyRawBlocks(out);return out;});
    auto b=std::async(std::launch::async,[&]{auto c=std::make_unique<Chunk>(-1,-1);generator.generate(*c);std::vector<uint16_t> out;c->copyRawBlocks(out);return out;});check(a.get()==expected&&b.get()==expected,"parallel generation order equality");
    const auto column=generator.samplePluginColumnBlocks(-15,-1);for(int y=Config::WORLD_MIN_Y;y<Config::WORLD_MAX_Y;++y)check(column[y-Config::WORLD_MIN_Y]==static_cast<uint16_t>(singleton->getBlock(1,y,15)),"LOD voxel/modifier equivalence");
    Plugins::GenerationConfig overlay;overlay.modifiers=config.modifiers;
    WorldGenerator builtinOverlay(12,WorldType::Superflat,DimensionId::Overworld,overlay);
    auto flat=std::make_unique<Chunk>(-1,-1);builtinOverlay.generate(*flat);
    const LodTileKey lodKey{-1,-1,0};const auto approximate=buildApproximateLodTile(builtinOverlay,lodKey);
    for(const auto& block:builtinOverlay.pluginModifierBlocks(-16,-16,16,16)) {
        bool found=false;for(const auto& span:approximate.at(block.x+16,block.z+16).spans)
            if(span.bottom<=block.y&&span.top>=block.y&&static_cast<uint16_t>(span.block)==block.block)found=true;
        check(found,"builtin generator modifier appears in LOD");
    }
    auto invalid=config;invalid.parameters="[]";bool configRefused=false;try{Plugins::validateGeneration(invalid,0);}catch(...){configRefused=true;}check(configRefused,"non-object generator parameters rejected");
    invalid=config;invalid.modifiers.push_back(invalid.modifiers.front());configRefused=false;try{Plugins::validateGeneration(invalid,0);}catch(...){configRefused=true;}check(configRefused,"duplicate modifier rejected");
    check(generator.getTerrainHeight(-1,-1)>=48&&generator.getTerrainHeight(-1,-1)<=51,"generation parameters affect surface queries");
    WorldMetadata metadata;metadata.displayName="Extensions";metadata.seed=12;metadata.generationVersion=WorldGenContext::GENERATION_VERSION;metadata.generation[0]=config;
    WorldMetadata::PersistedEntity persisted;persisted.type=type;persisted.health=30;persisted.pluginStateVersion=1;persisted.pluginState="{\"counter\":7}";metadata.entities.push_back(persisted);
    SaveStore store(root/"world");store.saveMetadata(metadata);store.saveGeneratedChunk(-1,-1,expected,generator.chunkCacheVersion(),Plugins::generationFingerprint(config));
    check(store.loadGeneratedChunk(-1,-1,generator.chunkCacheVersion(),Plugins::generationFingerprint(config)).has_value(),"generation cache identity roundtrip");
    check(!store.loadGeneratedChunk(-1,-1,generator.chunkCacheVersion(),Plugins::generationFingerprint(config)+1),"generation cache identity mismatch rejected");
    check(store.loadMetadata().generation[0]==config,"generation metadata roundtrip");
    // Simulate a changed local runtime allocation while preserving stable content identity.
    auto definition=*Plugins::entityContent(type);Plugins::mutableContentState().extensions.entities.erase(type);definition.id=5000;Plugins::mutableContentState().extensions.entities.emplace(5000,definition);
    const auto loaded=store.loadMetadata();check(loaded.entities.front().type==5000&&loaded.entities.front().pluginState==persisted.pluginState,"entity stable key/state survives runtime remap");
    EntitySnapshot snapshot;snapshot.id=1;snapshot.type=static_cast<EntityType>(5000);snapshot.health=30;snapshot.pluginStateVersion=1;snapshot.pluginState=persisted.pluginState;
    Lan::EntityBatch batch;batch.entities.push_back(snapshot);const auto decoded=Lan::decodeEntityBatch(Lan::encodeEntityBatch(batch));check(decoded.entities[0].type==snapshot.type&&decoded.entities[0].pluginState==persisted.pluginState,"LAN entity state roundtrip");
    auto large=snapshot;large.pluginState="{\"value\":\""+std::string(4000,'x')+"\"}";
    Lan::EntityBatch sizable;for(uint64_t id=1;id<=200;++id){large.id=id;sizable.entities.push_back(large);}
    const auto payload=Lan::encodeEntityBatch(sizable);check(payload.size()<Lan::MAX_PAYLOAD&&Lan::decodeEntityBatch(payload).entities.size()==200,"large plugin states retain bounded LAN snapshot encoding");
    Lan::WorldInfo info;info.name="Extensions";info.generation[0]=config;check(Lan::decodeWorldInfo(Lan::encodeWorldInfo(info)).generation[0]==config,"LAN generator configuration roundtrip");
    World world;world.resetForNewSeed(12,WorldType::Superflat);EntityPresenter presenter;EntityManager entities(world);EntityAiTestRenderer renderer;
    entities.setPresentation(&presenter);presenter.initializeModels(assets,renderer);
    check(entities.spawnMob(static_cast<EntityType>(5000),{0,0,0}),"registered mob spawn");check(entities.entities().back().health==30,"registered mob attributes");
    MC_EntitySnapshot sdkSnapshot{};sdkSnapshot.size=sizeof(sdkSnapshot);std::string state;check(entities.pluginSnapshot(entities.entities().back().id,sdkSnapshot,state)&&sdkSnapshot.state_version==1,"entity snapshot extension");
    Player player(world);player.setPosition({0,1,0});for(int frame=0;frame<2;++frame)entities.update(player,.1f,true,false,true,false);
    check(entities.entities().front().pluginState=="{\"behavior\":\"circle\"}","custom AI decision executes and persists output");
    check(entities.spawnMob(EntityType::Pig,{8,0,8})&&entities.entities().back().health==24,"builtin mob override applies at spawn");
    const auto saved=entities.saveEntities();entities.clear();entities.loadEntities(saved);check(entities.entities().front().type==static_cast<EntityType>(5000),"entity manager save/load");
    MC_EntityIntent intent{};intent.size=sizeof(intent);intent.attack_player=UINT64_MAX;intent.state="{\"test\":1}";check(entities.pluginIntent(entities.entities().front().id,intent),"validated entity intent state");
    check(entities.saveEntities().front().pluginState==intent.state,"entity instance state persisted");
    Plugins::mutableContentState().extensions.entities.erase(5000);bool refused=false;try{store.loadMetadata();}catch(...){refused=true;}check(refused,"missing entity type refuses load");
    auto original=Plugins::mutableContentState().extensions.generators.at(config.generator);Plugins::mutableContentState().extensions.generators.erase(config.generator);refused=false;try{WorldGenerator invalid(12,WorldType::Normal,DimensionId::Overworld,config);}catch(...){refused=true;}check(refused,"missing generator refuses construction");Plugins::mutableContentState().extensions.generators.emplace(original.key,original);
    auto& faulty=Plugins::mutableContentState().extensions.generators.at(config.generator);
    faulty.column=[](const MC_GenerationContext*,int32_t,int32_t,MC_SurfaceSample*,void*)->int32_t{return -1;};
    WorldGenerator broken(12,WorldType::Normal,DimensionId::Overworld,config);
    const auto failed=std::async(std::launch::async,[&]{try{Chunk chunk(0,0);broken.generate(chunk);}catch(...){return true;}return false;}).get();
    check(failed&&Plugins::mutableContentState().runtimeFault.empty(),"worker failure isolated from main content state");
    plugins.pollFaults();check(!Plugins::mutableContentState().runtimeFault.empty(),"worker generation failure reaches main-thread fault boundary");
    plugins.shutdown();std::filesystem::remove_all(root);std::cout<<"Plugin entity/worldgen extension integration passed\n";return 0;
}
}
