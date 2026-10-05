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
int main(int argc,char** argv) {
    try {
        const auto root=std::filesystem::temp_directory_path()/"minecraftc-plugin-regressions";
        std::filesystem::remove_all(root);std::filesystem::create_directories(root);
        rejects([]{Plugins::Json::parse("{\"id\":1,\"id\":2}");},"duplicate manifest keys rejected");
        rejects([]{Plugins::Json::parse("[1,]");},"malformed JSON rejected");
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
        RuntimePaths paths{MINECRAFTC_SOURCE_DIR "/assets",root};
        Plugins::PluginManager manager(paths);manager.initialize();
        const auto& plugins=manager.plugins();
        for(const auto& p:plugins)if(p.manifest.id=="bad_abi"||p.manifest.id=="bad_entry")require(!p.active&&!p.status.empty(),"invalid native entries blocked");else require(p.active,"valid builtin/data/native plugin loaded");
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
        for(auto& entry:remapBlocks){const uint16_t id=static_cast<uint16_t>(entry.first+100);blockIds[entry.first]=id;entry.second.properties.id=static_cast<BlockId>(id);Plugins::content().blocks.emplace(id,std::move(entry.second));}
        auto remapItems=Plugins::content().items;Plugins::content().items.clear();
        for(auto& entry:remapItems){const uint16_t id=static_cast<uint16_t>(entry.first+200);itemIds[entry.first]=id;if(entry.second.properties.placedBlock)entry.second.properties.placedBlock=static_cast<BlockId>(blockIds.at(static_cast<uint16_t>(*entry.second.properties.placedBlock)));Plugins::content().items.emplace(id,std::move(entry.second));}
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
        for(auto id:creativeInventoryItemsIn(CreativeItemCategory::BuildingBlocks))require(isValidItemId(id),"registry reset removes stale creative items");
        safe.shutdown();
        std::filesystem::remove_all(root);std::cout<<"Plugin loader/content/native/events/persistence regression passed\n";
    }catch(const std::exception& error){std::cerr<<"Plugin regression failed: "<<error.what()<<'\n';return 1;}
}
