#pragma once
#include "core/AssetStore.h"

namespace NaturalTextureIntegration {
inline const Plugins::PluginInfo& pack(const Plugins::PluginManager& manager) {
    const auto found=std::find_if(manager.plugins().begin(),manager.plugins().end(),[](const auto& p){return p.manifest.id=="official_natural_textures";});
    require(found!=manager.plugins().end(),"natural package missing");return *found;
}
inline int run() {
    try {
        const std::filesystem::path assets=MINECRAFTC_SOURCE_DIR "/assets";
        const auto root=std::filesystem::temp_directory_path()/"minecraftc-natural-texture-tests";
        std::filesystem::remove_all(root);std::filesystem::create_directories(root);
        for(const bool heaven:{false,true}) {
            const auto data=root/(heaven?"heaven-on":"heaven-off");
            auto config=[&](bool enabled){write(data/"plugins.json",nlohmann::json{{"official_heaven",heaven},{"official_natural_textures",enabled}}.dump());};
            config(false);
            Plugins::PluginManager baseline({assets,data});baseline.initialize();
            require(!pack(baseline).enabled&&!pack(baseline).active,"package must be disabled by default");
            const auto requirements=Plugins::content().requirements;
            const auto network=Plugins::content().networkDescription;
            const auto itemCount=Plugins::content().items.size(),blockCount=Plugins::content().blocks.size();
            const auto normal=buildBlockAtlasData(assets);
            const auto normalGi=buildVoxelGiMaterials(normal);
            SaveStore save(data/"world");WorldMetadata worldMetadata;worldMetadata.displayName="Visual pack compatibility";
            worldMetadata.generationVersion=WorldGenContext::GENERATION_VERSION;worldMetadata.inventory.slot(0)={ItemId::STONE,7,0};save.saveMetadata(worldMetadata);
            const auto saved=bytes(data/"world/level.bin");
            baseline.shutdown();config(true);
            Plugins::PluginManager enabled({assets,data});enabled.initialize();
            require(pack(enabled).active,pack(enabled).status.c_str());
            require(Plugins::content().requirements==requirements&&Plugins::content().networkDescription==network,"visual pack changed save or LAN requirements");
            require(Plugins::content().items.size()==itemCount&&Plugins::content().blocks.size()==blockCount,"visual pack registered gameplay content");
            require(save.loadMetadata().inventory.slot(0).count==7&&bytes(data/"world/level.bin")==saved,"texture switch refused or rewrote an existing save");
            for(uint16_t id=0;id<static_cast<uint16_t>(BlockTexture::Count);++id)
                require(Plugins::content().materials.count(getBlockTextureAssetName(static_cast<BlockTexture>(id))),"runtime material has no natural replacement");
            for(uint16_t id=1;id<static_cast<uint16_t>(ItemId::COUNT);++id)
                if(Plugins::availableItem(static_cast<ItemId>(id)))require(Plugins::content().itemIconOverrides.count(id),"valid item has no natural icon");
            const auto natural=buildBlockAtlasData(assets);const auto gi=buildVoxelGiMaterials(natural);
            require(natural.tilesPerSide==normal.tilesPerSide,"matching sequences changed atlas dimensions");
            require(natural.texture.pixels!=normal.texture.pixels,"natural atlas equals baseline");
            require(gi[uint16_t(BlockId::STONE)].reflectance[0]!=normalGi[uint16_t(BlockId::STONE)].reflectance[0],"GI retained old texture color");
            require(natural.texture.mipLevels.size()==4&&natural.normalTexture.mipLevels.size()==4&&natural.propertyTexture.mipLevels.size()==4,"natural mipmaps missing");
            for(const auto& entry:Plugins::content().materials)require(entry.second.replace,"natural material appended instead of replacing");
            const auto water=getAtlasTextureIndex(BlockTexture::Water);
            require(natural.sequences.at(water).frames==normal.sequences.at(water).frames,"water animation lost");
            const auto metadata=nlohmann::json::parse(bytes(assets/"textures/generated/items_atlas.json"));
            int rows=metadata.at("rows"),columns=metadata.at("columns");
            TextureData icons;icons.width=columns*16;icons.height=rows*16;icons.pixels.resize(size_t(icons.width)*icons.height*4);
            std::unordered_map<std::string,int> indices;
            for(const auto& value:metadata.at("items").items())indices[value.key()]=value.value().at("index");
            appendPluginItemIcons(icons,indices,columns,rows);
            for(const auto& entry:Plugins::content().itemIconOverrides) {
                const auto id=static_cast<ItemId>(entry.first);const auto key=Plugins::itemKey(id);
                require(indices.at(key)==indices.at(itemCommandName(id)),"item icon aliases diverged");
            }
            // Enable switches persist but do not mutate the already-loaded registry.
            size_t index=0;while(enabled.plugins()[index].manifest.id!="official_natural_textures")++index;
            enabled.setEnabled(index,false);require(!Plugins::content().itemIconOverrides.empty(),"toggle applied before restart");enabled.shutdown();
            Plugins::PluginManager restored({assets,data});restored.initialize();
            require(!pack(restored).active&&Plugins::content().itemIconOverrides.empty(),"disabled overlays survived restart");
            require(buildBlockAtlasData(assets).texture.pixels==normal.texture.pixels,"default atlas not restored");restored.shutdown();
            config(true);Plugins::PluginManager safe({assets,data});safe.initialize(true);
            require(!pack(safe).active&&Plugins::content().itemIconOverrides.empty(),"safe mode applied natural textures");safe.shutdown();
        }
        const auto data=root/"rollback";write(data/"plugins.json",R"({"official_heaven":false,"official_natural_textures":true})");
        const auto source=assets/"plugins/official_natural_textures/textures/items/stone.png";
        auto fixture=[&](const std::string& id,const std::string& definition){
            const auto p=data/"mods"/id;
            write(p/"mod.json",nlohmann::json{{"id",id},{"version","1.0.0"},{"api",1},{"package_format",1},{"visual_only",true},{"after",{"official_natural_textures"}}}.dump());
            std::filesystem::copy_file(source,p/"icon.png",std::filesystem::copy_options::overwrite_existing);
            write(p/"item_icons/a.json",definition);return p;
        };
        const auto failed=fixture("z_failed",R"({"item":"minecraftc:stone","image":"icon.png"})");
        write(failed/"item_icons/b.json",R"({"item":"minecraftc:dirt","image":"absent.png"})");
        write(failed/"materials/stone.json",R"({"key":"stone","replace":true,"color":[1,0,1,1]})");
        const auto illegal=fixture("z_gameplay",R"({"item":"minecraftc:stone","image":"icon.png"})");
        write(illegal/"items/new.json",R"({"key":"z_gameplay:new","name":"Not visual","material":"stone"})");
        fixture("z_unknown",R"({"item":"minecraftc:unknown_item","image":"icon.png"})");
        const auto winner=fixture("z_winner",R"({"item":"minecraftc:dirt","image":"icon.png"})");
        const auto owner=fixture("z_owner",R"({"item":"z_owner:gem","image":"icon.png"})");
        write(owner/"mod.json",R"({"id":"z_owner","version":"1.0.0","api":1,"package_format":1})");
        write(owner/"items/gem.json",R"({"key":"z_owner:gem","name":"Owned gem","material":"stone"})");
        write(winner/"mod.json",R"({"id":"z_winner","version":"1.0.0","api":1,"package_format":1,"visual_only":true,"after":["official_natural_textures","z_owner"]})");
        write(winner/"item_icons/plugin.json",R"({"item":"z_owner:gem","image":"icon.png"})");
        const auto ownership=fixture("z_ownership",R"({"item":"minecraftc:stone","image":"icon.png"})");
        write(ownership/"mod.json",R"({"id":"z_ownership","version":"1.0.0","api":1,"package_format":1})");
        for(const auto& spec:std::vector<std::pair<std::string,std::string>>{
            {"z_bad_count",R"({"key":"stone","replace":true,"sequence":{"variants":2,"images":["icon.png"]}})"},
            {"z_bad_range",R"({"key":"stone","replace":true,"sequence":{"frames":0,"images":[]}})"},
            {"z_bad_path",R"({"key":"stone","replace":true,"sequence":{"images":["../escape.png"]}})"}}) {
            const auto p=fixture(spec.first,R"({"item":"minecraftc:stone","image":"icon.png"})");
            write(p/"materials/a.json",spec.second);
        }
        Plugins::PluginManager rollback({assets,data});rollback.initialize();
        for(const auto& p:rollback.plugins())if(p.manifest.id=="z_failed"||p.manifest.id=="z_gameplay"||p.manifest.id=="z_unknown"||p.manifest.id=="z_ownership"||p.manifest.id.rfind("z_bad_",0)==0)require(!p.active&&!p.status.empty(),"bad visual package loaded");
        require(Plugins::content().materials.at("stone").images.size()>1,"failed package did not roll back material sequence");
        const auto stone=uint16_t(Plugins::resolveItem("minecraftc:stone")),dirt=uint16_t(Plugins::resolveItem("minecraftc:dirt"));
        require(Plugins::content().itemIconOverrides.at(stone).generic_string().find("official_natural_textures")!=std::string::npos,"failed icon overlay survived rollback");
        require(Plugins::content().itemIconOverrides.at(dirt)==winner/"icon.png","later icon package did not win");
        const auto gem=Plugins::resolveItem("z_owner:gem");
        require(Plugins::content().itemIconOverrides.at(uint16_t(gem))==winner/"icon.png"&&Plugins::pluginItem(gem)->icon==owner/"icon.png","foreign overlay changed provider icon");
        TextureData customIcons;customIcons.width=128;customIcons.height=16;customIcons.pixels.resize(128*16*4);
        std::unordered_map<std::string,int> customIndices;int customRows=1;
        appendPluginItemIcons(customIcons,customIndices,8,customRows);
        require(customIndices.at("z_owner:gem")==8,"overlaid provider icon consumed a duplicate slot");rollback.shutdown();
        // Exercise Android/iOS Title Storage materialization, including byte-exact inventory.
        const auto mobile=root/"virtual";write(mobile/"plugins.json",R"({"official_heaven":false,"official_natural_textures":true})");
        {AssetStore title(assets);Plugins::PluginManager virtualPack({{},mobile});virtualPack.initialize();
            require(pack(virtualPack).active,pack(virtualPack).status.c_str());
            require(pack(virtualPack).root.generic_string().find(".cache/builtin_plugins")!=std::string::npos,"virtual assets were not materialized");virtualPack.shutdown();}
        std::filesystem::remove_all(root);std::cout<<"Natural textures coverage, overrides, compatibility, rollback and mobile materialization passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Natural textures: "<<error.what()<<'\n';return 1;}
}
}
