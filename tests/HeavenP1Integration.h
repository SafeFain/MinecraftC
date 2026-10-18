#pragma once
#include "plugins/PluginManager.h"
#include "plugins/EnvironmentPresentation.h"
#include "renderer/BlockAtlasData.h"
#include <fstream>
#include <nlohmann/json.hpp>

namespace HeavenP1Integration {
inline void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
inline int run(const std::filesystem::path& assets,const std::array<std::filesystem::path,3>& modules) {
    const auto root=std::filesystem::temp_directory_path()/"minecraftc-heaven-p1-registration";
    std::filesystem::remove_all(root);
    const auto write=[](const auto& path,const auto& value){std::filesystem::create_directories(path.parent_path());std::ofstream(path,std::ios::binary)<<value;};
    const std::array<std::string,3> ids{{"p1_fixture","p1_bad_namespace","p1_bad_english"}};
    for(size_t i=0;i<ids.size();++i) {
        const auto package=root/"mods"/ids[i];std::filesystem::create_directories(package);
        std::filesystem::copy_file(modules[i],package/modules[i].filename());
        std::filesystem::copy_file(assets/"textures/generated/stone.png",package/"tile.png");
        const auto binary=modules[i].filename().string();
        write(package/"mod.json",nlohmann::json{{"id",ids[i]},{"version","1.0.0"},{"api",1},
            {"native",{{"windows",binary},{"linux",binary},{"macos",binary}}}}.dump());
        auto english=nlohmann::json{{ids[i]+":name","Third realm"},{ids[i]+":travel_name","Visit third realm"},{ids[i]+":english_only","English fallback"}};
        if(i==1)english["foreign:stolen"]="Must roll back";
        if(i==2)english.erase(ids[i]+":name");
        write(package/"lang/en_us.json",english.dump());
        write(package/"lang/zh_cn.json",nlohmann::json{{ids[i]+":name",u8"第三维度"}}.dump());
        write(package/"lang/ar_sa.json",nlohmann::json{{ids[i]+":name",u8"عالم ثالث"}}.dump());
    }
    AssetStore rootedAssets(assets);
    Plugins::PluginManager plugins({assets,root});plugins.initialize();
    for(const auto& info:plugins.plugins())if(info.manifest.id.rfind("p1_",0)==0)
        check(info.active==(info.manifest.id=="p1_fixture"),"P1 fixture activation/rollback status differs");
    const auto dimension=Plugins::resolveDimension("p1_fixture:realm");
    check(dimension>=2&&Plugins::content().extensions.dimensions.size()==3,"P1 extra registered dimension missing");
    for(const auto& pair:Plugins::content().extensions.dimensions)check(pair.second.key.rfind("p1_bad_",0)!=0,"Failed dimension registration leaked");
    for(const auto& pair:Plugins::content().materials)check(pair.first.rfind("p1_bad_",0)!=0,"Late registration failure leaked material");
    check(!Plugins::content().extensions.actions.count("p1_bad_english:action")&&
          !Plugins::content().extensions.particles.count("p1_bad_english:particle")&&
          !Plugins::content().extensions.translations.at("en_us").count("p1_bad_english:travel_name"),"Late failure did not roll back all registries");
    check(Plugins::content().extensions.translations.at("zh_cn").at("p1_fixture:name")==u8"第三维度"&&
          Plugins::content().extensions.translations.at("ar_sa").at("p1_fixture:name")==u8"عالم ثالث", "P1 UTF-8 dictionaries differ");
    check(Plugins::content().extensions.bedTravel.at("p1_fixture:travel").target==dimension,"P1 bed travel target differs");
    WorldGenerator generator(123,WorldType::Normal,static_cast<DimensionId>(dimension));
    check(generator.getSeed()==WorldGenContext(123).derive(0x50315f444f4d4149ULL),"P1 seed domain was not derived exactly once");
    const auto layers=generator.sampleGenerationLayers(-17,16);
    check(layers.size()==2&&layers[0].top==80&&layers[0].biome==7&&!layers[1].present&&layers[1].bottom==210,"P1 logical layer slot query differs");
    check(generator.sampleDimensionSpawn()->y==81.01,"P1 sampled spawn lost actual parameters");
    MC_ActionViewV3 view{};view.size=sizeof(view);view.generation_provider="p1_fixture:flat";view.dimension_key="p1_fixture:realm";
    MC_ActionTargetV2 target{};target.base.size=sizeof(target.base);target.base.dimension=dimension;
    const auto& action=Plugins::content().extensions.actions.at("p1_fixture:action");
    check(action.targetV3(view,target)&&target.handled&&target.base.valid,"P1 action V3 provider query failed");
    view.generation_provider="other:generator";target.base.valid=0;
    check(action.targetV3(view,target)&&!target.base.valid,"P1 action ignored provider identity");
    bool clouds=true;const auto environment=Plugins::applyEnvironment({},dimension,clouds);
    check(environment.particleIntensity==.35f&&environment.skyFlags==SkyNoAtmosphere&&environment.gradeStrength==.25f&&
          environment.gradeColor==glm::vec3(.9f,1.1f,1),"P1 registered presentation policies missing");
    const auto atlas=buildBlockAtlasData(assets);
    const auto& material=Plugins::content().materials.at("p1_fixture:material");
    check(material.variants==2&&material.frames==2&&material.images.size()==4&&atlas.texture.width>0,"P1 material sequence was not assembled");
    check(plugins.extension(nullptr,MC_WORLDGEN_EXTENSION,3)&&!plugins.extension(nullptr,MC_WORLDGEN_EXTENSION,4)&&
          plugins.extension(nullptr,MC_PLAYER_ACTIONS_EXTENSION,3)&&!plugins.extension(nullptr,MC_PLAYER_ACTIONS_EXTENSION,4),"P1 version negotiation differs");
    plugins.shutdown();
    Plugins::PluginManager safe({assets,root});safe.initialize(true);
    check(Plugins::content().extensions.dimensions.size()==1&&Plugins::content().extensions.translations.empty()&&
          Plugins::content().extensions.bedTravel.empty()&&Plugins::content().materials.empty(),"P1 safe mode published optional registries");
    safe.shutdown();
    Plugins::PluginManager desktop({assets,root/"desktop"});desktop.initialize();
    const auto desktopRequirements=Plugins::content().requirements;
    desktop.shutdown();
    Plugins::PluginManager packaged({{},root/"packaged"});packaged.initialize();
    check(Plugins::content().requirements==desktopRequirements,"Packaged title resources changed plugin identity");
    std::filesystem::path cached;
    for(const auto& info:packaged.plugins())if(info.manifest.id=="official_heaven") {
        check(info.active&&info.root.is_absolute(),"Packaged builtin resources did not activate");cached=info.root;
    }
    check(!cached.empty(),"Packaged resource cache missing");
    const auto contentTime=std::filesystem::last_write_time(cached/"content.json");
    packaged.shutdown();Plugins::PluginManager cachedAgain({{},root/"packaged"});cachedAgain.initialize();
    check(std::filesystem::last_write_time(cached/"content.json")==contentTime,"Intact builtin cache was rewritten");
    cachedAgain.shutdown();write(cached/"content.json","damaged cache");
    Plugins::PluginManager repaired({{},root/"packaged"});repaired.initialize();
    check(Plugins::content().requirements==desktopRequirements,"Builtin cache repair changed package identity");
    repaired.shutdown();std::filesystem::remove_all(root);
    std::cout<<"P1 native registration/localization/identity/material/rollback/safe-mode passed\n";return 0;
}
}
