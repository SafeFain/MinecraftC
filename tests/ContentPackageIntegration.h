#pragma once
#include "core/AssetStore.h"

extern "C" const MC_Plugin* mc_content_query_package_fixture(uint32_t);
namespace ContentPackageIntegration {
inline int run(const std::filesystem::path& published,const std::filesystem::path& staged) {
    const auto root=std::filesystem::temp_directory_path()/("minecraftc-content-package-tests-"+std::to_string(std::hash<std::string>{}(published.generic_string())));
    try {
        std::filesystem::remove_all(root);std::filesystem::create_directories(root);
        const std::filesystem::path assets=MINECRAFTC_SOURCE_DIR "/assets";
        const auto data=root/"external";
        write(data/"plugins.json",R"({"official_heaven":false})");
        std::filesystem::create_directories(data/"mods");
        std::filesystem::copy(published,data/"mods/package_demo",std::filesystem::copy_options::recursive);
        auto package=[&](const std::string& id) {
            const auto folder=data/"mods"/id;
            write(folder/"mod.json","{\"id\":\""+id+"\",\"version\":\"1.0.0\",\"api\":1,\"package_format\":1}");
            return folder;
        };
        auto material=[&](const std::filesystem::path& folder,const std::string& id) {
            write(folder/"materials/material.json","{\"key\":\""+id+":mat\",\"color\":[0.2,0.4,0.8,1]}");
        };
        const auto split=package("split_data");material(split,"split_data");
        write(split/"blocks/nested/zeta.json",R"({"key":"split_data:zeta","name":"Zeta","name_key":"split_data:zeta.name","materials":"split_data:mat","drop":"split_data:zeta"})");
        write(split/"blocks/alpha.json",R"({"key":"split_data:alpha","name":"Alpha","materials":"split_data:mat"})");
        write(split/"items/zeta.json",R"({"key":"split_data:zeta","name":"Zeta","placed_block":"split_data:zeta","material":"split_data:mat","name_key":"split_data:zeta.name"})");
        write(split/"dimensions/islands.json",R"({"key":"split_data:islands","name":"Islands","revision":"1","generator":"split_data:flat","generator_revision":"1"})");
        write(split/"generators/flat.json",R"({"key":"split_data:flat","revision":"1","dimension":"split_data:islands","height":64,"surface":"split_data:zeta","fill":"split_data:alpha"})");
        write(split/"lang/en_us.json",R"({"split_data:zeta.name":"Localized Zeta"})");
        write(split/"lang/zh_cn.json",R"({"split_data:zeta.name":"本地化方块"})");
        for(const char* shader:{"vert","frag"}) {
            const auto path=split/(std::string("shaders/effect.")+shader+".spv");std::filesystem::create_directories(path.parent_path());
            std::filesystem::copy_file(std::filesystem::path(MINECRAFTC_SOURCE_DIR "/examples/plugins/render_demo")/(std::string("effect.")+shader+".spv"),path);
            write(split/(std::string("render_resources/z_")+shader+".json"),std::string("{\"key\":\"split_data:")+shader+"\",\"kind\":\"shader\",\"path\":\"shaders/effect."+shader+".spv\"}");
        }
        write(split/"render_resources/a_pipeline.json",R"({"key":"split_data:pipeline","kind":"pipeline","vertex_shader":"split_data:vert","fragment_shader":"split_data:frag"})");
        auto bad=package("bad_type");material(bad,"bad_type");
        write(bad/"blocks/a_good.json",R"({"key":"bad_type:good","name":"Good","materials":"bad_type:mat"})");
        write(bad/"blocks/z_bad.json",R"({"key":"bad_type:bad","name":"Bad","materials":"bad_type:mat","hardness":"wrong"})");
        const auto dependent=package("dependent");
        write(dependent/"mod.json",R"({"id":"dependent","version":"1.0.0","api":1,"package_format":1,"dependencies":[{"id":"bad_type"}]})");
        bad=package("duplicate");material(bad,"duplicate");
        write(bad/"blocks/a.json",R"({"key":"duplicate:block","name":"First","materials":"duplicate:mat"})");
        write(bad/"blocks/b.json",R"({"key":"duplicate:block","name":"Second","materials":"duplicate:mat"})");
        bad=package("late_bad");material(bad,"late_bad");
        write(bad/"items/item.json",R"({"key":"late_bad:item","name":"Item","material":"late_bad:mat"})");
        write(bad/"item_behaviors/item.json",R"({"item":"late_bad:item","action":"late_bad:missing"})");
        bad=package("early_bad");material(bad,"early_bad");
        write(bad/"items/item.json",R"({"key":"early_bad:item","name":"Item","material":"early_bad:mat","placed_block":"early_bad:code_block"})");
        bad=package("missing_lang");write(bad/"lang/zh_cn.json",R"({"missing_lang:name":"名字"})");
        bad=package("bad_lang");write(bad/"lang/en_us.json",R"({"bad_lang:label":12})");
        bad=package("escape");write(bad/"materials/material.json",R"({"key":"escape:mat","image":"../outside.png"})");
        bad=package("cycle");
        write(bad/"render_resources/a.json",R"({"key":"cycle:a","kind":"pipeline","texture":"cycle:b"})");
        write(bad/"render_resources/b.json",R"({"key":"cycle:b","kind":"pipeline","texture":"cycle:a"})");
        bad=package("no_key");write(bad/"blocks/block.json",R"({"name":"No key","materials":"minecraftc:stone"})");
        const auto legacy=data/"mods/legacy";
        write(legacy/"mod.json",R"({"id":"legacy","version":"1.0.0","api":1,"data":"content.json"})");
        write(legacy/"content.json",R"({"items":[{"key":"legacy:item","name":"Legacy","material":"stone"}]})");
        const auto relativeData=data.lexically_relative(std::filesystem::current_path());
        Plugins::PluginManager manager({assets,relativeData.empty()?data:relativeData});manager.initialize();
        auto status=[&](const std::string& id)->const Plugins::PluginInfo& {
            const auto& all=manager.plugins();const auto it=std::find_if(all.begin(),all.end(),[&](const auto& p){return p.manifest.id==id;});
            require(it!=all.end(),"missing package status");return *it;
        };
        for(const char* id:{"package_demo","split_data","legacy"})require(status(id).active,status(id).status.c_str());
        for(const char* id:{"bad_type","dependent","duplicate","late_bad","early_bad","missing_lang","bad_lang","escape","cycle","no_key"})require(!status(id).active,"invalid package survived");
        require(status("bad_type").status.find("blocks/z_bad.json/hardness")!=std::string::npos,"typed field source missing");
        require(status("late_bad").status.find("item_behaviors/item.json")!=std::string::npos,"deferred validation lost source");
        require(status("bad_lang").status.find("lang/en_us.json")!=std::string::npos&&status("bad_lang").status.find("bad_lang:label")!=std::string::npos,"translation error lost file/key context");
        require(status("early_bad").status.find("before code")!=std::string::npos,"early-reference guidance missing");
        require(status("dependent").status.find("failed dependency")!=std::string::npos,"failed dependency did not propagate");
        for(const char* key:{"bad_type:good","duplicate:block"})rejects([&]{Plugins::resolveBlock(key);},"failed package leaked block");
        rejects([&]{Plugins::resolveItem("late_bad:item");},"late failure leaked item");
        const auto alpha=Plugins::resolveBlock("split_data:alpha"),zeta=Plugins::resolveBlock("split_data:zeta");
        require(uint16_t(alpha)<uint16_t(zeta),"definitions were not sorted by relative path");
        require(getItemProps(Plugins::resolveItem("split_data:zeta")).placedBlock==zeta,"cross-file placement reference");
        require(Plugins::content().extensions.nameKeys.at("split_data:zeta")=="split_data:zeta.name","localized same-key block/item binding");
        require(Plugins::content().extensions.translations.at("zh_cn").at("split_data:zeta.name")=="本地化方块","language bundle missing");
        require(Plugins::content().extensions.generators.count("split_data:flat"),"late generator for early dimension failed");
        require(Plugins::content().extensions.renderResources.size()==3,"render dependency sort failed");
        require(Plugins::content().extensions.renderResources.begin()->second.key=="split_data:frag","independent render resources lost stable path order");
        int grants=0;manager.operations.giveItem=[&](uint16_t item,uint32_t count){require(item==uint16_t(Plugins::resolveItem("package_demo:blue_brick")),"wrong granted item");grants+=int(count);return true;};
        auto ready=Plugins::event(MC_WORLD_READY);manager.dispatch(ready);
        require(manager.command("/package_demo:gift"),"native command missing");require(grants==0,"mutation was not queued");manager.flushOperations();require(grants==4,"native command did not grant");
        auto use=Plugins::event(MC_USE_PRE);use.item=uint16_t(Plugins::resolveItem("package_demo:crystal"));manager.dispatch(use);manager.flushOperations();require(use.cancelled&&grants==5,"native use callback failed");
        const auto requirements=Plugins::content().requirements;
        SaveStore store(root/"world");WorldMetadata metadata;metadata.displayName="Content packages";metadata.generationVersion=WorldGenContext::GENERATION_VERSION;metadata.inventory.slot(0)={Plugins::resolveItem("package_demo:blue_brick"),3,0};store.saveMetadata(metadata);
        require(store.loadMetadata().inventory.slot(0).count==3,"package content failed save/reopen");
        manager.shutdown();
        Plugins::PluginManager again({assets,data});again.initialize();require(Plugins::content().requirements==requirements,"stable files changed requirements");again.shutdown();
        // Same code and definitions, original lifecycle: code must still run before data.
        const auto failure=root/"failure";write(failure/"plugins.json",R"({"official_heaven":false})");
        std::filesystem::create_directories(failure/"mods");std::filesystem::copy(published,failure/"mods/package_demo",std::filesystem::copy_options::recursive);
        std::filesystem::remove(failure/"mods/package_demo/items/crystal.json");
        Plugins::PluginManager failed({assets,failure});failed.initialize();
        rejects([]{Plugins::resolveBlock("package_demo:blue_brick");},"native failure did not roll back data");failed.shutdown();
        write(failure/"mods/package_demo/mod.json",R"({"id":"package_demo","version":"1.0.0","api":1,"data":"content.json","native":{"windows":"package_demo.dll","linux":"package_demo.so","macos":"package_demo.dylib"}})");
        write(failure/"mods/package_demo/content.json",R"({"blocks":[{"key":"package_demo:blue_brick","name":"Brick","materials":"stone"}],"items":[{"key":"package_demo:blue_brick","name":"Brick","material":"stone","placed_block":"package_demo:blue_brick"},{"key":"package_demo:crystal","name":"Crystal","material":"stone"}]})");
        Plugins::PluginManager old({assets,failure});old.initialize();
        require(!std::find_if(old.plugins().begin(),old.plugins().end(),[](const auto& p){return p.manifest.id=="package_demo";})->active,"legacy lifecycle changed");old.shutdown();
        // Inject a compile-time provider through the same registry contract used by
        // generated CMake descriptors, without enabling it in production builds.
        auto builtins=Plugins::builtinPlugins();const auto stage=staged.generic_string(),assetPath=assets.generic_string();
        builtins.push_back({"package_demo","Package Demo","content-package-v1","plugins/package_demo",false,true,mc_content_query_package_fixture,true,stage.c_str(),assetPath.c_str()});
        const auto builtinData=root/"builtin";write(builtinData/"plugins.json",R"({"official_heaven":false})");
        Plugins::PluginManager builtin({assets,builtinData},{},builtins);builtin.initialize();require(Plugins::resolveItem("package_demo:crystal")!=ItemId::EMPTY,"compiled provider failed");const auto builtinRequirements=Plugins::content().requirements;builtin.shutdown();
        const auto titleRoot=root/"title";std::filesystem::create_directories(titleRoot/"plugins");std::filesystem::copy(staged,titleRoot/"plugins/package_demo",std::filesystem::copy_options::recursive);write(titleRoot/"plugins/descriptions.json","{}");
        const auto virtualData=root/"virtual";write(virtualData/"plugins.json",R"({"official_heaven":false})");
        {AssetStore title(titleRoot);Plugins::PluginManager virtualPackage({{},virtualData},{},builtins);virtualPackage.initialize();require(Plugins::content().requirements==builtinRequirements,"virtual packaged resources changed fingerprint");require(Plugins::resolveBlock("package_demo:blue_brick")!=BlockId::AIR,"virtual package did not load");virtualPackage.shutdown();}
        std::filesystem::create_directories(builtinData/"mods");std::filesystem::copy(published,builtinData/"mods/package_demo",std::filesystem::copy_options::recursive);
        Plugins::PluginManager duplicateBuiltin({assets,builtinData},{},builtins);duplicateBuiltin.initialize();int builtinCount=0,externalCount=0;for(const auto& p:duplicateBuiltin.plugins())if(p.manifest.id=="package_demo"){if(p.manifest.builtin){require(p.active,"external identity disabled builtin");++builtinCount;}else{require(!p.active,"external claimed builtin identity");++externalCount;}}require(builtinCount==1&&externalCount==1,"duplicate builtin identity count");duplicateBuiltin.shutdown();
        Plugins::PluginManager safe({assets,builtinData},{},builtins);safe.initialize(true);require(Plugins::content().requirements.empty(),"safe mode registered content");safe.shutdown();
        std::filesystem::remove_all(root);std::cout<<"Structured content package integration passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Content package integration: "<<error.what()<<'\n';return 1;}
}
}
