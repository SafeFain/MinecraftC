#include "plugins/PluginManager.h"
#include "game/SaveStore.h"
#include "game/WorldCatalog.h"
#include "renderer/BlockAtlasData.h"
#include "world/Chunk.h"
#include "world/ChunkMesh.h"
#include "debug/Log.h"
#include "entity/EntityLogic.h"
#include "world/WorldGenContext.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
template<class F> void rejects(F&& f,const char* reason){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}require(rejected,reason);}
void write(const std::filesystem::path& file,const std::string& text){std::filesystem::create_directories(file.parent_path());std::ofstream stream(file);stream<<text;}
std::string bytes(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
Plugins::PluginInfo info(const std::string& id){Plugins::PluginInfo p;p.manifest.id=id;p.manifest.version="1.0.0";return p;}
}
#include "ContentPackageIntegration.h"
int main(int argc,char** argv) {
    if(argc==4&&std::string(argv[1])=="--content-package-tests")return ContentPackageIntegration::run(argv[2],argv[3]);
    try {
        const auto root=std::filesystem::temp_directory_path()/"minecraftc-plugin-regressions";
        std::filesystem::remove_all(root);std::filesystem::create_directories(root);
        rejects([]{Plugins::Json::parse("{\"id\":1,\"id\":2}");},"duplicate manifest keys rejected");
        rejects([]{Plugins::Json::parse("[1,]");},"malformed JSON rejected");
        const auto localized=Plugins::Manifest::parse(Plugins::Json::parse(R"({"id":"description_test","version":"1.0.0","api":1,"description":{"en_us":"English description","zh_cn":"中文描述"}})"));
        require(localized.description("zh_cn")=="中文描述"&&localized.description("fr_fr")=="English description","localized description and English fallback");
        const auto plain=Plugins::Manifest::parse(Plugins::Json::parse(R"({"id":"description_test","version":"1.0.0","api":1,"description":"Package description"})"));
        require(plain.description("zh_cn")=="Package description","plain descriptions are language independent");
        rejects([]{Plugins::Manifest::parse(Plugins::Json::parse(R"({"id":"description_test","version":"1.0.0","api":1,"description":{"zh_cn":"Missing fallback"}})"));},"localized description requires fallback");
        rejects([]{Plugins::Manifest m;m.readDescription(Plugins::Json::parse("123"));},"non-text descriptions rejected");
        rejects([]{Plugins::Manifest m;m.readDescription(Plugins::Json::parse(nlohmann::json(std::string(4097,'x')).dump()));},"oversize descriptions rejected");
        rejects([]{Plugins::Version::parse("-1.0.0");},"negative version rejected");
        rejects([]{Plugins::Version::parse("1.01.0");},"leading zero version rejected");
        require(Plugins::Version::parse("1.0.0-beta.2")<Plugins::Version::parse("1.0.0-beta.10"),"numeric prerelease ordering");
        require(Plugins::Version::parse("1.0.0-beta.10")<Plugins::Version::parse("1.0.0"),"release follows prerelease");
        rejects([&]{Plugins::PluginManager::containedPath(root,"../escape");},"parent traversal rejected");
        rejects([&]{Plugins::PluginManager::containedPath(root,"/escape");},"absolute path rejected");
        std::vector<Plugins::PluginInfo> graph{info("b"),info("a"),info("c")};graph[0].manifest.dependencies.push_back({"a","1.0.0","2.0.0",false});
        auto order=Plugins::PluginManager::resolve(graph);require(order.size()==3&&order[0]==1&&order[1]==0,"dependency order stable");
        graph={info("a"),info("b")};graph[0].manifest.after={"b"};graph[1].manifest.after={"a"};require(Plugins::PluginManager::resolve(graph).empty(),"cycles blocked");
        graph={info("a"),info("a")};require(Plugins::PluginManager::resolve(graph).empty(),"duplicate IDs blocked");
        graph={info("a"),info("b")};graph[0].manifest.dependencies.push_back({"missing",{}, {},false});graph[1].manifest.dependencies.push_back({"a",{}, {},false});require(Plugins::PluginManager::resolve(graph).empty(),"missing dependencies propagate");
        graph={info("a"),info("b")};graph[0].manifest.dependencies.push_back({"b","2.0.0",{},true});require(Plugins::PluginManager::resolve(graph).size()==1,"present optional dependency must be compatible");
        write(root/"plugins.json","{\"official_content\":true,\"official_atmosphere\":true}");
        std::filesystem::create_directories(root/"mods");
        std::filesystem::copy(MINECRAFTC_SOURCE_DIR "/examples/plugins/example_content",root/"mods"/"example_content",std::filesystem::copy_options::recursive);
        if(Platform::DynamicLibrary::supported()) {
            require(argc==5,"native fixture paths required");
            for(int i=1;i<5;++i) {
                const std::string id=i==1?"native_demo":i==2?"bad_abi":i==3?"bad_entry":"callback_fault";
                write(root/"mods"/id/"mod.json","{\"id\":\""+id+"\",\"version\":\"1.0.0\",\"api\":1,\"native\":{\"linux\":\"plugin.bin\",\"windows\":\"plugin.bin\",\"macos\":\"plugin.bin\"}}");
                std::filesystem::copy_file(argv[i],root/"mods"/id/"plugin.bin");
            }
        }
        write(root/"mods"/"extension_data"/"mod.json",R"({"id":"extension_data","version":"1.0.0","api":1,"data":"content.json"})");
        write(root/"mods"/"extension_data"/"content.json",R"({"entities":[{"key":"extension_data:mob","name":"Test mob","health":28,"template_type":1,"size":[0.8,1.2,0.8]}],"generators":[{"key":"extension_data:flat","name":"Test plateau","revision":"1","height":40,"water_level":39,"surface":"minecraftc:grass","fill":"minecraftc:stone"}]})");
        write(root/"mods"/"extension_bad"/"mod.json",R"({"id":"extension_bad","version":"1.0.0","api":1,"data":"content.json"})");
        write(root/"mods"/"extension_bad"/"content.json",R"({"entities":[{"key":"extension_bad:first","name":"Rollback","template_type":1,"health":10},{"key":"extension_bad:invalid","name":"Invalid","template_type":1,"health":-1}]})");
        write(root/"mods"/"render_bad"/"mod.json",R"({"id":"render_bad","version":"1.0.0","api":1,"data":"content.json"})");
        write(root/"mods"/"render_bad"/"content.json",R"({"entities":[{"key":"render_bad:first","name":"Rollback","template_type":1,"health":10}],"render_resources":[{"key":"render_bad:shader","kind":"shader","path":"bad.spv"}]})");
        write(root/"mods"/"render_bad"/"bad.spv","not-a-valid-spirv!!!!");
        for(const std::string id:{"model_corrupt","model_unsupported"}) {
            write(root/"mods"/id/"mod.json","{\"id\":\""+id+"\",\"version\":\"1.0.0\",\"api\":1,\"data\":\"content.json\"}");
            write(root/"mods"/id/"content.json","{\"entities\":[{\"key\":\""+id+":prior\",\"template_type\":1},{\"key\":\""+id+":bad\",\"template_type\":1,\"model\":\"model.glb\"}]}");
            write(root/"mods"/id/"model.glb",id=="model_corrupt"?"broken glb":R"({"asset":{"version":"2.0"},"extensionsRequired":["EXT_unsupported"]})");
        }
        write(root/"mods"/"model_valid"/"mod.json",R"({"id":"model_valid","version":"1.0.0","api":1,"data":"content.json"})");
        write(root/"mods"/"model_valid"/"content.json",R"({"entities":[{"key":"model_valid:mob","template_type":1,"model":"cow.glb"}]})");
        std::filesystem::copy_file(MINECRAFTC_SOURCE_DIR "/assets/models/entities/cow.glb",root/"mods"/"model_valid"/"cow.glb");
        RuntimePaths paths{MINECRAFTC_SOURCE_DIR "/assets",root};
        Plugins::PluginManager manager(paths);manager.initialize();
        const auto& plugins=manager.plugins();
        for(const auto& p:plugins)if(p.manifest.builtin) {
            require(!p.manifest.description("en_us").empty()&&!p.manifest.description("zh_cn").empty(),"official descriptions loaded from assets");
            require(p.manifest.description("en_us")!=p.manifest.description("zh_cn"),"official Chinese description supplied");
        }
        for(const auto& p:plugins)if(p.manifest.id=="bad_abi"||p.manifest.id=="bad_entry"||p.manifest.id=="extension_bad"||p.manifest.id=="render_bad"||p.manifest.id=="model_corrupt"||p.manifest.id=="model_unsupported")require(!p.active&&!p.status.empty(),"invalid native entries blocked");else require(p.active,"valid builtin/data/native plugin loaded");
        for(const auto* id:{"model_corrupt:prior","model_corrupt:bad","model_unsupported:prior","model_unsupported:bad"})
            rejects([&]{Plugins::resolveEntity(id);},"failed entity model did not roll back registration");
        require(Plugins::entityContent(Plugins::resolveEntity("model_valid:mob"))->asset!=nullptr,"registered entity did not retain its validated model");
        const auto dataMob=Plugins::resolveEntity("extension_data:mob");require(dataMob>=4096&&Plugins::entityContent(dataMob)->health==28,"data entity registry");
        rejects([]{Plugins::resolveEntity("extension_bad:first");},"extension registration failure rolls back all types");
        rejects([]{Plugins::resolveEntity("render_bad:first");},"invalid shader rolls back prior gameplay registration");
        require(Plugins::content().extensions.generators.count("extension_data:flat"),"data generator registry");
        require(Plugins::content().networkDescription.count("entity/extension_data:mob")&&Plugins::content().networkDescription.count("generator/extension_data:flat"),"extensions participate in network handshake");
        const auto block=Plugins::resolveBlock("example_content:blue_brick"),crystalBlock=Plugins::resolveBlock("official_content:crystal_block");
        const auto item=Plugins::resolveItem("example_content:blue_brick"),crystal=Plugins::resolveItem("official_content:crystal");
        require(isValidBlockId(block)&&isValidItemId(item)&&getItemProps(item).placedBlock==block,"registered content usable");
        require(getLightEmission(crystalBlock)==9,"custom light emission");
        const auto category=creativeInventoryItemsIn(CreativeItemCategory::BuildingBlocks);require(std::find(category.begin(),category.end(),item)!=category.end(),"custom items appear in creative catalog");
        std::array<ItemId,9> grid{};grid[0]=ItemId::COBBLESTONE;const auto* recipe=findCraftingRecipe(grid,1,1);require(recipe&&recipe->output.id==item&&recipe->output.count==4,"custom recipe actually selected");
        rejects([&]{Plugins::registerBlock({});},"frozen registry rejects mutation");
        const auto atlas=buildBlockAtlasData(paths.assetRoot);
        require(atlas.texture.mipLevels.size()==4&&atlas.normalTexture.mipLevels.size()==4,"plugin atlas has tile-safe mip chain");
        const auto tile=getFaceTextureIndex(block,FaceDir::TOP);require(tile==Plugins::pluginItem(item)->tile&&tile!=getFaceTextureIndex(BlockId::STONE,FaceDir::TOP),"shared block/item material slot");
        const auto gi=buildVoxelGiMaterials(atlas);require(gi.size()>static_cast<size_t>(crystalBlock)&&gi[static_cast<uint16_t>(crystalBlock)].emission.b>0,"custom material GI injection");
        int rectangles=0,text=0,operations=0;
        manager.operations.hudRect=[&](float,float,float,float,const glm::vec4&){++rectangles;};
        manager.operations.hudText=[&](float,float,const std::string&,const glm::vec4&){++text;};
        manager.operations.player=[](MC_PlayerSnapshot& p){p.position[0]=1;return true;};
        manager.operations.giveItem=[&](uint16_t,uint32_t){++operations;return true;};
        auto ready=Plugins::event(MC_WORLD_READY);manager.dispatch(ready);
        auto hud=Plugins::event(MC_HUD);hud.screen_height=600;manager.dispatch(hud);require(text>0,"builtin HUD event draws");
        auto environment=Plugins::event(MC_ENVIRONMENT);for(float& c:environment.environment.zenith)c=1;for(float& c:environment.environment.fog)c=1;manager.dispatch(environment);require(environment.environment.zenith[0]<1,"environment event changes colors");
        if(Platform::DynamicLibrary::supported()) {
            require(manager.command("/native_demo:gift"),"native command registered");require(operations==0,"native writes deferred");manager.flushOperations();require(operations==1,"native writes execute at safe boundary");
            auto use=Plugins::event(MC_USE_PRE);use.item=static_cast<uint16_t>(Plugins::resolveItem("native_demo:crystal"));manager.dispatch(use);require(use.cancelled,"native behavior cancels default use");manager.flushOperations();require(operations==2,"native use changes inventory through host interface");
        }
        SaveStore store(root/"world");WorldMetadata metadata;metadata.displayName="Plugin world";metadata.generationVersion=WorldGenContext::GENERATION_VERSION;metadata.inventory.slot(0)={item,12,0};metadata.inventory.offhand()={crystal,3,0};
        WorldMetadata::PersistedEntity dropped;dropped.type=static_cast<uint8_t>(EntityType::Item);dropped.item={item,2,0};metadata.entities={dropped};store.saveMetadata(metadata);
        store.saveChunkOverrides(-1,-2,{{17,block}});std::vector<uint16_t> generated(Config::CHUNK_VOLUME,static_cast<uint16_t>(block));store.saveGeneratedChunk(-1,-2,generated,metadata.generationVersion);
        PersistedBlockEntity chest;chest.localIndex=1;chest.value.type=BlockEntityType::Chest;chest.value.chest[0]={item,8,0};store.saveBlockEntities(-1,-2,{chest});store.saveChunkEntities(-1,-2,{dropped});
        const auto before=bytes(root/"world"/"level.bin");const auto savedState=Plugins::content();
        // Simulate a different runtime allocation while keeping the same installed
        // package set. Every durable file must resolve names independently.
        auto remapBlocks=Plugins::content().blocks;Plugins::content().blocks.clear();std::map<uint16_t,uint16_t> blockIds,itemIds;
        for(auto& entry:remapBlocks){const uint16_t id=static_cast<uint16_t>(entry.first+(entry.first>=Plugins::FIRST_CONTENT_ID?100:0));blockIds[entry.first]=id;entry.second.properties.id=static_cast<BlockId>(id);Plugins::content().blocks.emplace(id,std::move(entry.second));}
        auto remapItems=Plugins::content().items;Plugins::content().items.clear();
        for(auto& entry:remapItems){const uint16_t id=static_cast<uint16_t>(entry.first+(entry.first>=Plugins::FIRST_CONTENT_ID?200:0));itemIds[entry.first]=id;if(entry.second.properties.placedBlock)entry.second.properties.placedBlock=static_cast<BlockId>(blockIds.at(static_cast<uint16_t>(*entry.second.properties.placedBlock)));Plugins::content().items.emplace(id,std::move(entry.second));}
        for(auto& entry:Plugins::content().blocks)if(entry.second.drop!=ItemId::EMPTY)entry.second.drop=static_cast<ItemId>(itemIds.at(static_cast<uint16_t>(entry.second.drop)));
        Plugins::freezeContent();const auto changedItem=Plugins::resolveItem("example_content:blue_brick");const auto changedBlock=Plugins::resolveBlock("example_content:blue_brick");
        auto loaded=store.loadMetadata();require(loaded.inventory.slot(0).id==changedItem&&loaded.entities[0].item.id==changedItem,"inventory and entity IDs remapped by name");
        require(store.loadChunkOverrides(-1,-2)[0].block==changedBlock,"block edits remapped");require(store.loadBlockEntities(-1,-2)[0].value.chest[0].id==changedItem,"containers remapped");require(store.loadChunkEntities(-1,-2)[0].item.id==changedItem,"chunk dropped items remapped");
        const auto cache=store.loadGeneratedChunk(-1,-2,metadata.generationVersion);require(cache&&cache->front()==static_cast<uint16_t>(changedBlock),"generated RLE remapped");
        Chunk restored(-1,-2);restored.loadRawBlocks(*cache);require(restored.getBlock(0,Config::WORLD_MIN_Y,0)==changedBlock,"runtime chunk accepts registered cache palette");
        Plugins::content().requirements.clear();rejects([&]{store.loadMetadata();},"missing plugin set refuses load");require(bytes(root/"world"/"level.bin")==before,"refused load does not write");
        require(!store.loadMetadata(true).pluginCompatibilityError.empty(),"inspection retains incompatible world metadata");
        Plugins::content()=savedState;Plugins::freezeContent();
        if(Platform::DynamicLibrary::supported()) {
            manager.command("/native_demo:gift");const int beforeFault=operations;
            auto update=Plugins::event(MC_UPDATE_POST);manager.dispatch(update);manager.flushOperations();
            require(!Plugins::content().runtimeFault.empty()&&operations==beforeFault,"callback fault discards pending world mutations");
        }
        manager.shutdown();
        Plugins::PluginManager safe(paths);safe.initialize(true);require(Plugins::content().blocks.empty()&&Plugins::content().requirements.empty(),"safe mode loads no optional content");
        for(const auto& p:safe.plugins())if(p.manifest.builtin)require(!p.enabled&&!p.manifest.description("zh_cn").empty(),"safe mode retains disabled official descriptions");
        for(auto id:creativeInventoryItemsIn(CreativeItemCategory::BuildingBlocks))require(isValidItemId(id),"registry reset removes stale creative items");
        safe.shutdown();
        std::filesystem::remove_all(root);std::cout<<"Plugin loader/content/native/events/persistence regression passed\n";
    }catch(const std::exception& error){std::cerr<<"Plugin regression failed: "<<error.what()<<'\n';return 1;}
}
