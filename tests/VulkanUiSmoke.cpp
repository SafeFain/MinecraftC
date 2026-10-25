#include "plugins/PluginManager.h"
#include "plugins/EnvironmentPresentation.h"
#include "plugins/ContentMutation.h"
#include "plugins/ContentQueries.h"
#include "plugins/GenerationValidation.h"
#include "renderer/HeldItemRenderer.h"
#include "renderer/ParticleSystem.h"
#include "audio/AudioSystem.h"
#include "core/AssetStore.h"
#include <glm/gtc/matrix_transform.hpp>
#include "ui/PluginMenu.h"
#include "ui/LanMenu.h"
#include <fstream>
#include "core/Window.h"
#include "entity/EntityManager.h"
#include "model/ModelRenderer.h"
#include "game/SessionAccess.h"
#include "game/Command.h"
#include "core/Utf8.h"
#include "renderer/backend/vulkan/VulkanRenderer.h"
#include "renderer/backend/vulkan/VulkanGiSmokeProbe.h"
#include "ui/UIRenderer.h"
#include "ui/UIStyle.h"
#include "ui/Menu.h"
#include "ui/SettingsMenu.h"
#include "ui/Inventory.h"
#include "ui/SurvivalInventory.h"
#include "ui/ContainerScreen.h"
#include "ui/VillagerTradeScreen.h"
#include "ui/Hotbar.h"
#include "ui/TouchControls.h"
#include "world/BlockEntity.h"
#include "game/NetherProgression.h"
#include <iostream>
#include <functional>
#include <chrono>
#include <set>

namespace {
struct Access final : IContainerAccess,ITradeAccess {
    BlockEntity block;
    Entity villager;
    Access() {
        villager.type=EntityType::Villager;
        villager.villager.profession=VillagerProfession::Mason;
        villager.villager.reputation=80;
        villager.villager.demand[0]=10;
        villager.villager.uses[1]=12;
        villager.villager.level=5;
    }
    BlockEntity* blockEntityAt(const glm::ivec3&) override { return &block; }
    const Entity* tradeEntity(uint64_t) const override { return &villager; }
    bool tradeUsable(uint64_t,const glm::dvec3&,const glm::vec3&,float) const override { return true; }
    void executeTrade(uint64_t,uint8_t,InventoryModel&) override {}
};
}

int main(int argc,char** argv) {
    if (argc<2 || argc>5) { std::cerr<<"Usage: vulkan_ui_smoke assets [width height] [--capture]\n";return 2; }
    try {
        const bool heavenModel=argc==3&&std::string(argv[2])=="--heaven-model";
        const bool heavenOff=argc==3&&std::string(argv[2])=="--heaven-off";
        const bool capabilities=heavenModel||(argc==3&&std::string(argv[2])=="--capabilities");
        const bool extensions=argc==4&&std::string(argv[2])=="--extensions";
        const bool contentPackage=argc==4&&std::string(argv[2])=="--content-package";
        const int width=!extensions&&!contentPackage&&argc>=4?std::stoi(argv[2]):960,height=!extensions&&!contentPackage&&argc>=4?std::stoi(argv[3]):600;
        const bool capture=argc==5 && std::string(argv[4])=="--capture";
        const auto assets=std::filesystem::absolute(argv[1]);
        const auto pluginRoot=std::filesystem::temp_directory_path()/"minecraftc-plugin-ui-smoke";
        std::filesystem::remove_all(pluginRoot);
        std::filesystem::create_directories(pluginRoot/"mods");
        if(contentPackage&&std::string(argv[3])!="builtin")std::filesystem::copy(std::filesystem::absolute(argv[3]),pluginRoot/"mods/package_demo",std::filesystem::copy_options::recursive);
        std::filesystem::copy(assets.parent_path()/"examples"/"plugins"/"block_shapes_data",pluginRoot/"mods"/"block_shapes_data",std::filesystem::copy_options::recursive);
        const auto legacy=pluginRoot/"mods"/"legacy_icon";std::filesystem::create_directories(legacy);
        {std::ofstream manifest(legacy/"mod.json");manifest<<R"({"id":"legacy_icon","version":"1.0.0","api":1,"data":"content.json"})";}
        {std::ofstream content(legacy/"content.json");content<<R"({"blocks":[{"key":"legacy_icon:cube","name":"Legacy cube","materials":"stone"}],"items":[{"key":"legacy_icon:item","name":"Independent icon","material":"dirt","placed_block":"legacy_icon:cube","kind":1}]})";}
        if(extensions) {
            const auto package=pluginRoot/"mods"/"render_demo";std::filesystem::create_directories(package);
            const auto source=assets.parent_path()/"examples"/"plugins"/"render_demo";
            for(const auto& entry:std::filesystem::directory_iterator(source))if(entry.is_regular_file()&&entry.path().extension()==".spv")std::filesystem::copy_file(entry.path(),package/entry.path().filename());
            const auto generation=pluginRoot/"mods"/"worldgen_ui";std::filesystem::create_directories(generation);
            {std::ofstream manifest(generation/"mod.json");manifest<<R"({"id":"worldgen_ui","version":"1.0.0","api":1,"data":"content.json"})";}
            {std::ofstream content(generation/"content.json");content<<R"({"generators":[{"key":"worldgen_ui:flat","name":"Plugin Plateau","revision":"1","height":48,"water_level":47}]})";}
            const auto module=std::filesystem::absolute(argv[3]);std::filesystem::copy_file(module,package/module.filename());
            std::ofstream manifest(package/"mod.json");manifest<<"{\"id\":\"render_demo\",\"version\":\"1.0.0\",\"api\":1,\"visual_only\":true,\"native\":{\"linux\":\""<<module.filename().string()<<"\",\"windows\":\""<<module.filename().string()<<"\",\"macos\":\""<<module.filename().string()<<"\"}}";
        }
        if(capabilities)std::filesystem::copy(assets.parent_path()/"examples/plugins/capability_demo",pluginRoot/"mods/capability_demo",std::filesystem::copy_options::recursive);
        {std::ofstream config(pluginRoot/"plugins.json");config<<"{\"official_content\":true,\"official_atmosphere\":true,\"capability_demo\":true,\"official_heaven\":"<<(heavenOff?"false":"true")<<"}";}
        Plugins::PluginManager plugins({assets,pluginRoot});plugins.initialize();
        if(Plugins::validDimension(1)==heavenOff)throw std::runtime_error("Official provider enabled state mismatch");
        Window window(width,height,"MinecraftC UI smoke",Window::SurfaceMode::Vulkan,false,false);
        VulkanRenderer renderer;renderer.initialize(window,assets);
        renderer.setVisualQuality(VisualQuality::Low);renderer.setEnhancedVisuals(false);
        plugins.renderDraw=[&](size_t owner,const MC_RenderDraw& draw){return renderer.pluginDraw(owner,draw);};
        plugins.renderScene=[&](size_t owner,uint64_t index,MC_RenderHandle pipeline){return renderer.pluginSceneDraw(owner,index,pipeline);};
        plugins.renderBuffer=[&](size_t owner,MC_RenderHandle handle,const void* data,uint32_t size){return renderer.pluginUpdateBuffer(owner,handle,data,size);};
        if(extensions) {
            for(const auto& info:plugins.plugins())if(info.enabled&&!info.active)throw std::runtime_error("Render package failed: "+info.status);
            const auto passes=Plugins::mutableContentState().extensions.renderPasses;
            model::ModelInstance posed;model::ModelHandle posedModel{};
            auto image=[&] {std::vector<uint8_t> pixels;for(int frame=0;frame<4;++frame){FrameData state;state.clearColor={.2f,.4f,.6f,1};renderer.beginFrame(state);
                if(posedModel){model::ModelDraw model;model.model=posedModel;model.instance=&posed;renderer.modelRenderer().queue(model);renderer.flushModels(glm::mat4(1));}
                renderer.finishScene(PostProcessState{});if(frame==3)VulkanGiSmokeProbe::requestCapture(renderer);renderer.endFrame();window.finishEventFrame();}renderer.waitIdle();pixels=VulkanGiSmokeProbe::readCapture(renderer);if(pixels.empty())throw std::runtime_error("Plugin image readback unavailable");return pixels;};
            Plugins::mutableContentState().extensions.renderPasses.clear();const auto baseline=image();Plugins::mutableContentState().extensions.renderPasses=passes;const auto custom=image();
            if(!Plugins::mutableContentState().runtimeFault.empty())throw std::runtime_error(Plugins::mutableContentState().runtimeFault);
            if(custom==baseline)throw std::runtime_error("Custom SPIR-V post pass did not affect pixels");
            renderer.resize(width,height);image();renderer.suspendPresentation();renderer.resumePresentation();image();
            if(!Plugins::mutableContentState().runtimeFault.empty())throw std::runtime_error(Plugins::mutableContentState().runtimeFault);
            MC_RenderHandle scenePipeline=0;size_t owner=0;
            for(const auto& resource:Plugins::mutableContentState().extensions.renderResources)if(resource.second.key=="render_demo:offscreen"){scenePipeline=resource.first;owner=resource.second.owner;}
            if(!scenePipeline)throw std::runtime_error("Scene test pipeline unavailable");
            Plugins::RenderPassContent entityPass;entityPass.stage=MC_RENDER_ENTITIES;entityPass.replace=true;
            bool sawScene=false;
            entityPass.callback=[&](const MC_RenderFrame& frame){for(uint64_t index=0;index<frame.scene_count;++index){sawScene=true;if(!renderer.pluginSceneDraw(owner,index,scenePipeline))throw std::runtime_error("Scene redraw rejected");}};
            Plugins::mutableContentState().extensions.renderPasses.push_back(std::move(entityPass));
            auto model=std::make_shared<model::ModelAsset>();model->materials.emplace_back();
            model::Primitive triangle;triangle.material=0;
            for(const auto& position:{glm::vec3(-.4f,-.4f,0),glm::vec3(.4f,-.4f,0),glm::vec3(0,.4f,0)}){model::Vertex vertex;vertex.position=position;triangle.vertices.push_back(vertex);}
            triangle.indices={0,1,2};model->primitives.push_back(std::move(triangle));
            model::Node node;node.skin=0;node.primitives={0};model->nodes.push_back(node);model->sceneRoots={0};model->skins.push_back({{0},{glm::mat4(1)}});
            posed.jointPalettes={{glm::mat4(1)}};posedModel=renderer.modelRenderer().upload(model);
            const auto unshifted=image();posed.jointPalettes[0][0][3][0]=.5f;const auto shifted=image();
            if(!Plugins::mutableContentState().runtimeFault.empty())throw std::runtime_error(Plugins::mutableContentState().runtimeFault);
            if(!sawScene||unshifted==shifted)throw std::runtime_error("Custom scene redraw lost skeletal pose");
            posedModel={};Plugins::mutableContentState().extensions.renderPasses=passes;
            std::cout<<"Plugin Vulkan posed scene redraw passed\n";
            std::cout<<"Plugin Vulkan shader/post pass, rebuild and resume passed\n";
        }
        if(capabilities) {
            const auto dimension=heavenModel?1u:Plugins::resolveDimension("capability_demo:islands");
            bool clouds=true;auto environment=Plugins::applyEnvironment(DayNightCycle{}.evaluate(),dimension,clouds);
            if(environment.skyStyle!=RenderSkyStyle::Layered||(!heavenModel&&clouds)||
               (heavenModel&&!(environment.skyFlags&SkyNoVoxelClouds)))throw std::runtime_error("Capability sky profile missing");
            AssetStore store(assets);AudioSystem audio;audio.initialize(&store);
            Plugins::contextProvider()=[dimension]{return Plugins::ActorContext{0,dimension,MC_LOCAL};};audio.setMusicMode(AudioMusicMode::Overworld,dimension);
            if(heavenModel){renderer.setVisualQuality(VisualQuality::Medium);renderer.setEnhancedVisuals(true);}
            HeldItemRenderer held;held.initialize(renderer,assets);
            const auto& type=Plugins::mutableContentState().extensions.particles.at("capability_demo:spark");
            std::vector<ParticleRenderData> particles;
            for(int i=0;i<8;++i){
                const auto& visual=heavenModel?Plugins::mutableContentState().extensions.particles.at(std::array<const char*,3>{{"official_heaven:mote","official_heaven:pollen","official_heaven:sparkle"}}[size_t(i%3)]):type;
                ParticleRenderData p;p.position={.6f+(i%4)*.1f,(i/4)*.1f,0};p.kind=visual.shape?float(5+visual.shape):static_cast<float>(ParticleKind::Plugin);
                p.texture=Plugins::materialTile(visual.material);p.size=visual.definition.scale;p.phase=.5f;p.color={visual.definition.color[0],visual.definition.color[1],visual.definition.color[2],visual.definition.color[3]};particles.push_back(p);
            }
            const glm::vec3 camera(0,0,3);const auto vp=glm::perspective(glm::radians(55.0f),window.aspectRatio(),.05f,100.0f)*glm::lookAt(camera,glm::vec3(0),glm::vec3(0,1,0));
            auto captureScene=[&](int content){std::vector<uint8_t> pixels;for(int frame=0;frame<4;++frame){renderer.beginFrame();renderer.setEnvironment(environment,camera);renderer.setViewProjection(vp);if(content<0)renderer.renderSky(environment,glm::inverse(vp),camera,clouds);
                if(content>0&&(content&1))held.renderDropped({heavenModel?ItemId::STARSTEP_SCEPTER:Plugins::resolveItem("capability_demo:staff"),1,0},vp,{0,0,0},0,0,{1,0});
                if(content>0&&(content&2))renderer.renderParticles(particles,vp,{1,0,0},{0,1,0},environment.particleIntensity>=0?environment.particleIntensity:1.f);
                PostProcessState post;post.environment=environment;post.inverseViewProjection=glm::inverse(vp);post.cameraPosition=camera;
                renderer.finishScene(post);if(frame==3)VulkanGiSmokeProbe::requestCapture(renderer);renderer.endFrame();window.finishEventFrame();}renderer.waitIdle();pixels=VulkanGiSmokeProbe::readCapture(renderer);if(pixels.empty())throw std::runtime_error("Capability Vulkan readback missing");return pixels;};
            captureScene(-1);const auto baseline=captureScene(0),modelImage=captureScene(1),particleImage=captureScene(2);if(baseline==modelImage)throw std::runtime_error("Plugin held model did not affect Vulkan pixels");if(baseline==particleImage)throw std::runtime_error("Plugin particles did not affect Vulkan pixels");
            if(heavenModel){const auto all=particles;for(int shape=1;shape<=3;++shape){particles.clear();for(const auto& p:all)if(p.kind==float(5+shape))particles.push_back(p);
                if(captureScene(2)==baseline)throw std::runtime_error("Official ambient particle shape did not reach Vulkan pixels");}particles=all;}
            for(auto& p:particles)p.color={1,0,0,1};
            if(captureScene(2)==particleImage)throw std::runtime_error("Particle RGBA did not reach Vulkan fragments");
            if(heavenModel) {
                DayNightCycle cycle;cycle.setPhase(.125f);environment=Plugins::applyEnvironment(cycle.evaluate(),dimension,clouds);
                const auto day=captureScene(-1);
                const auto gradedModel=captureScene(1);
                const auto grade=environment.gradeStrength;environment.gradeStrength=0;
                if(captureScene(1)==gradedModel)throw std::runtime_error("Registered Heaven grade did not affect Vulkan pixels");
                environment.gradeStrength=grade;cycle.setPhase(.75f);environment=Plugins::applyEnvironment(cycle.evaluate(),dimension,clouds);
                const auto night=captureScene(-1);
                const auto brightness=[](const auto& pixels){uint64_t sum=0;for(size_t i=0;i<pixels.size();i+=4)sum+=uint64_t(pixels[i])+pixels[i+1]+pixels[i+2];return double(sum);};
                if(brightness(day)<brightness(night)*1.05)throw std::runtime_error("Registered Heaven day/night sky brightness did not reach Vulkan pixels");
                std::cout<<"Official Heaven day/night sky and registered color-grade Vulkan readback passed\n";
            }
            renderer.resize(width,height);captureScene(3);renderer.suspendPresentation();renderer.resumePresentation();captureScene(3);
            held.reset();Plugins::contextProvider()={};std::cout<<"Plugin sky/cloud, held GLB, colored particles, WAV initialization and Vulkan rebuild/resume passed\n";
        }
        UIRenderer ui;Localization localization;localization.load(assets);
        ui.setLocalization(localization);ui.initialize(renderer,renderer.getBlockAtlasTexture(),assets);
        // Font-file coverage alone cannot catch translations omitted from the
        // actual GPU atlas. Verify both layout and emitted glyph geometry.
        std::set<uint32_t> pluginCjk;
        for(const auto& bundle:Plugins::mutableContentState().extensions.translations)
            for(const auto& entry:bundle.second)
                for(uint32_t cp:decodeUtf8(entry.second))
                    if(cp>=0x3400 && cp<=0x9FFF)pluginCjk.insert(cp);
        for(const auto& plugin:plugins.plugins())
            for(const auto& description:plugin.manifest.descriptions)
                for(uint32_t cp:decodeUtf8(description.second))
                    if(cp>=0x3400&&cp<=0x9FFF)pluginCjk.insert(cp);
        // Characters absent from all shipped dictionaries exercise runtime names
        // and chat, rather than merely retesting startup font collection.
        const auto runtimeGlyphs=decodeUtf8(u8"龘靐齉");
        for(uint32_t cp:runtimeGlyphs)pluginCjk.insert(cp);
        pluginCjk.insert(0x10FFFF); // Unsupported text must produce a visible fallback.
        renderer.beginFrame();ui.beginUIFrame(width,height);
        for(uint32_t cp:pluginCjk) {
            std::string glyph;appendUtf8(glyph,cp);
            const auto glyphWidth=ui.measureText(glyph,1).x;
            if(glyphWidth<=0)throw std::runtime_error("Plugin glyph missing from font atlas: "+glyph);
            if(cp>=0x3400&&cp<=0x9FFF&&std::abs(glyphWidth-ui.measureText(u8"中",1).x)>.01f)
                throw std::runtime_error("Supported CJK glyph used fallback metrics: "+glyph);
            const auto before=ui.frameStats().vertices;
            ui.renderText(glyph,8,8,1,{1,1,1});
            if(ui.frameStats().vertices!=before+4)throw std::runtime_error("Plugin glyph geometry missing: "+glyph);
        }
        ui.endUIFrame();renderer.endFrame();renderer.waitIdle();
        std::cout<<"Plugin metadata/runtime CJK atlas coverage passed: "<<pluginCjk.size()<<" glyphs\n";
        const auto captureText=[&](const std::string& text) {
            VulkanGiSmokeProbe::requestCapture(renderer);
            renderer.beginFrame();ui.beginUIFrame(width,height);
            ui.drawRect(0,0,static_cast<float>(width),static_cast<float>(height),{0,0,0,1});
            ui.renderText(text,20,40,3,{1,1,1});
            ui.endUIFrame();renderer.endFrame();renderer.waitIdle();
            const auto pixels=VulkanGiSmokeProbe::readCapture(renderer);
            if(pixels.size()!=static_cast<size_t>(width*height*4))throw std::runtime_error("Runtime glyph readback unavailable");
            return pixels;
        };
        const auto blank=captureText("");
        for(const auto* text:{u8"龘靐齉",u8"鱻麤灥",u8"𠀀",u8"\U0010FFFF"}) {
            const auto image=captureText(text);
            if(image==blank)throw std::runtime_error("Runtime glyph atlas update did not reach Vulkan pixels");
        }
        std::cout<<"Runtime font append, fallback and Vulkan pixel readback passed\n";
        ClientSettings settings;
        InventoryModel items;
        items.slot(0)={ItemId::STONE,64,0};items.slot(1)={ItemId::OAK_PLANKS,32,0};
        Access access;access.block.chest[0]=items.slot(0);
        MenuCallbacks callbacks;
        callbacks.onOpenSettings=[]{};callbacks.onQuit=[]{};
        callbacks.onCreateWorld=[](const std::string&,const std::string&,GameMode,WorldType,bool){};
        callbacks.onOpenWorld=[](const std::string&){};
        callbacks.onDeleteWorld=[](const std::string&){return true;};
        int sleepChoice=-1;callbacks.onSleepAction=[&](int choice){sleepChoice=choice;};
        callbacks.onBedTravel=[&](const std::string& key){if(key=="official_heaven:bed_travel")sleepChoice=2;};
        std::vector<WorldSummary> worlds;
        for (int i=0;i<6;++i) {
            WorldSummary world;world.id=std::to_string(i);world.displayName="Mountain valley "+std::to_string(i+1);
            world.seed=88123+i;world.compatible=i!=5;world.generationVersion=i!=5?16:15;worlds.push_back(world);
        }
        MainMenu main(callbacks,worlds,settings,localization,nullptr);
        SettingsMenu options(settings,[]{},[]{},localization);
        PauseMenu pause(callbacks,localization);SleepMenu sleep(callbacks,localization,DimensionId::Overworld);
        sleep.onKeyPress(Key::Down);sleep.onKeyPress(Key::Down);sleep.onKeyPress(Key::Enter);
        if(sleepChoice!=(heavenOff?0:2))throw std::runtime_error("Sleep menu Heaven entry did not follow provider state");
        CreativeInventory creative;SurvivalInventoryScreen survival(items);
        ContainerScreen container(items);container.open(access,{0,0,0});
        VillagerTradeScreen trades(items);trades.open(access,1);
        Hotbar hotbar;hotbar.setInventory(&items);
        TouchControls touch;touch.configure(width,height,{});
        const auto draw=[&](const std::string& name,const std::function<void()>& render,int frames=12) {
            const bool menuCapture=name.rfind("nether-",0)==0 || name=="quality-zh_cn" || name=="quality-scrolled" ||
                name=="settings" || name=="settings-audio" || name=="settings-bindings" ||
                name=="settings-keyboard-scroll" || name=="pause" || name=="about" ||
                name=="plugins-en_us" || name=="plugins-zh_cn" || name=="plugins-scrolled-zh_cn" ||
                name=="language-menu-back" || name=="create" || name=="lan" ||
                name=="create-default-zh_cn" || name=="create-advanced-zh_cn";
            size_t previousVertices=0,previousBatches=0;
            for (int frame=0;frame<frames;++frame) {
                if((menuCapture || (capture && name=="trades") || name=="plugin-mismatch-warning" || name=="registered-travel-zh_cn") && frame==frames-1)VulkanGiSmokeProbe::requestCapture(renderer);
                renderer.beginFrame();ui.advanceTime(1.0f/60);
                ui.beginUIFrame(width,height);render();ui.endUIFrame();renderer.endFrame();
                const auto stats=ui.frameStats();
                if (frame>1 && (stats.vertices!=previousVertices || stats.batches!=previousBatches))
                    throw std::runtime_error("UI batches grow across identical frames");
                if (stats.vertices==0 || stats.vertices>50000 || stats.batches>1000)
                    throw std::runtime_error("UI geometry budget invalid");
                previousVertices=stats.vertices;previousBatches=stats.batches;
            }
            renderer.waitIdle();
            std::cout<<"READY "<<name<<" vertices="<<previousVertices<<" batches="<<previousBatches<<std::endl;
            if(menuCapture) {
                const auto pixels=VulkanGiSmokeProbe::readCapture(renderer);
                if(pixels.size()!=static_cast<size_t>(width*height*4))throw std::runtime_error("Menu capture size invalid");
                std::ofstream out(std::filesystem::temp_directory_path()/(name+".ppm"),std::ios::binary);
                out<<"P6\n"<<width<<' '<<height<<"\n255\n";
                for(size_t i=0;i<pixels.size();i+=4)out.write(reinterpret_cast<const char*>(pixels.data()+i),3);
            }
            if(capture && name=="trades") {
                const auto pixels=VulkanGiSmokeProbe::readCapture(renderer);
                if(pixels.size()!=static_cast<size_t>(width*height*4))throw std::runtime_error("trade capture size invalid");
                std::ofstream out(std::filesystem::temp_directory_path()/"village-trades.ppm",std::ios::binary);
                out<<"P6\n"<<width<<' '<<height<<"\n255\n";
                for(size_t i=0;i<pixels.size();i+=4)out.write(reinterpret_cast<const char*>(pixels.data()+i),3);
            }
            if(name=="plugin-mismatch-warning") {
                const auto pixels=VulkanGiSmokeProbe::readCapture(renderer);
                if(pixels.size()!=static_cast<size_t>(width*height*4))throw std::runtime_error("Warning capture size invalid");
                std::ofstream out(std::filesystem::temp_directory_path()/"plugin-mismatch-warning.ppm",std::ios::binary);
                out<<"P6\n"<<width<<' '<<height<<"\n255\n";
                for(size_t i=0;i<pixels.size();i+=4)out.write(reinterpret_cast<const char*>(pixels.data()+i),3);
            }
            if(name=="registered-travel-zh_cn") {
                const auto pixels=VulkanGiSmokeProbe::readCapture(renderer);
                if(pixels.size()!=static_cast<size_t>(width*height*4))throw std::runtime_error("Chinese travel capture size invalid");
                std::ofstream out(std::filesystem::temp_directory_path()/"localized-travel.ppm",std::ios::binary);
                out<<"P6\n"<<width<<' '<<height<<"\n255\n";
                for(size_t i=0;i<pixels.size();i+=4)out.write(reinterpret_cast<const char*>(pixels.data()+i),3);
            }
            if (capture) { std::string next;std::getline(std::cin,next); }
        };
        const UiMenuFooter footer(width,height);
        const auto click=[&](Menu& menu,double x,double y) {
            menu.onMouseButton(MouseButton::Left,ButtonAction::Press,x,y);
            menu.onMouseButton(MouseButton::Left,ButtonAction::Release,x,y);
        };
        for (const auto language:languagesByEnglishName()) {
            localization.setLanguage(language);
            ClientSettings qualitySettings;
            qualitySettings.markGraphicsCustom();
            int changed=0,backs=0;
            SettingsMenu quality(qualitySettings,[&]{++changed;},[&]{++backs;},localization);
            quality.onKeyPress(Key::Tab);quality.onKeyPress(Key::Enter); // Video
            quality.onKeyPress(Key::Tab);quality.onKeyPress(Key::Tab);quality.onKeyPress(Key::Enter);
            if(changed!=0 || qualitySettings.graphicsPreset!=GraphicsPreset::Custom)
                throw std::runtime_error("Opening quality submenu changed Custom settings");
            draw("quality-initial",[&]{quality.render(ui,width,height);},3);
            const float tableW=std::min(968.0f,width-40.0f);
            const float cellW=tableW*.70f/5;
            const float headerH=height<300?26.0f:36.0f;
            for(size_t i=0;i<VISUAL_QUALITY_ORDER.size();++i) {
                click(quality,(width-tableW)*.5f+tableW*.30f+(i+.5f)*cellW,
                    height-70-headerH*.5f);
                if(changed!=static_cast<int>(i)+1 ||
                    qualitySettings.graphicsPreset!=presetForVisualQuality(VISUAL_QUALITY_ORDER[i]))
                    throw std::runtime_error("Quality header did not directly apply requested preset");
                draw("quality-selected",[&]{quality.render(ui,width,height);},3);
            }
            draw("quality-"+std::string(languageCode(language)),[&]{quality.render(ui,width,height);},3);
            for(int i=0;i<4;++i)quality.onKeyPress(Key::Right);
            quality.onKeyPress(Key::Enter);
            if(qualitySettings.graphicsPreset!=GraphicsPreset::Ultra || changed!=6)
                throw std::runtime_error("Keyboard quality selection order is incorrect");
            quality.onKeyPress(Key::End);
            draw("quality-scrolled",[&]{quality.render(ui,width,height);},3);
            click(quality,width*.5f,footer.y+footer.height*.5f);
            quality.onKeyPress(Key::Escape); // Video -> General
            draw("quality-parent",[&]{quality.render(ui,width,height);},3);
            click(quality,width*.5f,footer.y+footer.height*.5f);
            if(backs!=1)throw std::runtime_error("Fixed footer did not return through quality/video/general");
        }
        localization.setLanguage(Language::English);
        int lanBacks=0;
        LanMenuActions lanActions;lanActions.back=[&]{++lanBacks;};
        LanMenu lan(lanActions,localization,nullptr,"Player");
        draw("lan",[&]{lan.render(ui,width,height);},3);
        click(lan,width*.5f,footer.y+footer.height*.5f);
        if(lanBacks!=1)throw std::runtime_error("LAN footer is not at screen bottom");
        draw("home",[&]{main.render(ui,width,height);});
        for(const auto language:languagesByEnglishName()) {
            int pluginBacks=0;
            localization.setLanguage(language);PluginMenu pluginMenu(plugins,localization,[&]{++pluginBacks;});
            draw("plugins-"+std::string(languageCode(language)),[&]{pluginMenu.render(ui,width,height);});
            if(language==Language::SimplifiedChinese) {
                pluginMenu.onScroll(-1);pluginMenu.onScroll(-1);
                draw("plugins-scrolled-zh_cn",[&]{pluginMenu.render(ui,width,height);});
            }
            click(pluginMenu,width*.5f,footer.y+footer.height-2);
            if(pluginBacks!=1)throw std::runtime_error("Plugin list overlaps the fixed footer");
            SleepMenu localizedSleep(callbacks,localization,DimensionId::Overworld);
            draw("registered-travel-"+std::string(languageCode(language)),[&]{
                localizedSleep.render(ui,width,height);
                const auto title=localization.format("loading.enter_dimension",{localization.dimensionName(DimensionId::Overworld)});
                ui.renderText(title,12,height-24,1,{1,1,1});
            },3);
        }
        localization.setLanguage(Language::English);
        WorldSummary mismatch;mismatch.id="mismatch";mismatch.displayName="Plugin compatibility warning";
        mismatch.pluginCompatibility.token="smoke";mismatch.backupDirectory="saves/.plugin-recovery/world-backup/original";
        for(int i=0;i<12;++i)mismatch.pluginCompatibility.issues.push_back({"changed_plugin","example_"+std::to_string(i),"1.0:"+std::string(64,'a'),"2.0:"+std::string(64,'b')});
        auto warningCallbacks=callbacks;
        warningCallbacks.onBeginWorldRecovery=[](const std::string&,const std::string&,bool)->std::unique_ptr<WorldRecovery>{throw std::runtime_error("smoke failure");};
        MainMenu warning(warningCallbacks,{mismatch},settings,localization,nullptr);
        warning.onKeyPress(Key::Enter);warning.onKeyPress(Key::Enter);warning.onKeyPress(Key::Enter);
        draw("plugin-mismatch-warning",[&]{warning.render(ui,width,height);});
        warning.onScroll(-8);draw("plugin-mismatch-warning-scrolled",[&]{warning.render(ui,width,height);});
        warning.onKeyPress(Key::Down);warning.onKeyPress(Key::Enter);
        draw("plugin-mismatch-failure",[&]{warning.render(ui,width,height);});
        warning.onKeyPress(Key::Enter);warning.onKeyPress(Key::Enter);
        warning.onKeyPress(Key::Down);warning.onKeyPress(Key::Down);
        draw("plugin-mismatch-no-backup",[&]{warning.render(ui,width,height);});
        warning.onKeyPress(Key::Enter);
        draw("plugin-mismatch-no-backup-failure",[&]{warning.render(ui,width,height);});
        draw("plugin-content",[&]{UiTheme::menuBackground(ui,width,height);ui.drawItemIcon(40,40,64,64,{Plugins::resolveItem("official_content:crystal_block"),1,0});ui.drawItemIcon(120,40,64,64,{Plugins::resolveItem("official_content:crystal"),1,0});});
        const auto iconPasses=Plugins::mutableContentState().extensions.renderPasses;Plugins::mutableContentState().extensions.renderPasses.clear();
        auto iconImage=[&](const std::function<void()>& render) {
            for(int frame=0;frame<3;++frame) {
                if(frame==2)VulkanGiSmokeProbe::requestCapture(renderer);
                FrameData state;state.clearColor={0,0,0,1};renderer.beginFrame(state);
                ui.beginUIFrame(width,height);render();ui.endUIFrame();renderer.endFrame();
            }
            renderer.waitIdle();auto pixels=VulkanGiSmokeProbe::readCapture(renderer);
            if(pixels.size()!=static_cast<size_t>(width*height*4))throw std::runtime_error("Shape icon capture unavailable");
            return pixels;
        };
        auto shapeImage=[&](BlockId block){return iconImage([&]{ui.drawBlockIcon(16,16,64,64,block);});};
        const auto slabImage=shapeImage(Plugins::resolveBlock("block_shapes_data:slab"));
        const auto cubeImage=shapeImage(BlockId::STONE);
        if(slabImage==cubeImage)throw std::runtime_error("Slab icon did not render partial geometry");
        shapeImage(Plugins::resolveBlock("block_shapes_data:stair"));shapeImage(Plugins::resolveBlock("block_shapes_data:door"));
        shapeImage(Plugins::resolveBlock("block_shapes_data:button"));shapeImage(Plugins::resolveBlock("block_shapes_data:box"));
        const auto legacyItem=Plugins::resolveItem("legacy_icon:item");const auto legacyTile=Plugins::pluginItem(legacyItem)->tile;
        if(contentPackage) {
            for(const auto& info:plugins.plugins())if(info.manifest.id=="package_demo"&&!info.active)throw std::runtime_error(info.status);
            const auto block=Plugins::resolveBlock("package_demo:blue_brick");
            const auto brick=Plugins::resolveItem("package_demo:blue_brick"),crystal=Plugins::resolveItem("package_demo:crystal");
            const auto pixels=shapeImage(block);
            if(pixels==cubeImage)throw std::runtime_error("Package material did not affect Vulkan pixels");
            const auto blockItem=iconImage([&]{ui.drawItemIcon(16,16,64,64,{brick,1,0});});
            const auto tile=Plugins::pluginItem(brick)->tile;
            const auto material=iconImage([&]{ui.drawAtlasIcon(16,16,64,64,tile,glm::vec4(1));});
            if(tile!=getFaceTextureIndex(block,FaceDir::TOP)||blockItem!=material)throw std::runtime_error("Package block/item material mapping differs");
            iconImage([&]{ui.drawItemIcon(16,16,64,64,{crystal,1,0});});
            int grants=0;plugins.operations.giveItem=[&](uint16_t item,uint32_t count){if(item!=uint16_t(brick))throw std::runtime_error("Wrong package item grant");grants+=int(count);return true;};
            auto ready=Plugins::event(MC_WORLD_READY);plugins.dispatch(ready);
            if(!plugins.command("/package_demo:gift"))throw std::runtime_error("Package command missing");
            plugins.flushOperations();auto use=Plugins::event(MC_USE_PRE);use.item=uint16_t(crystal);plugins.dispatch(use);plugins.flushOperations();
            if(grants!=5||!use.cancelled)throw std::runtime_error("Package native interactions failed");
            std::cout<<"Content package Vulkan material/icon and native interaction readback passed\n";
            plugins.operations.giveItem={};
        }
        const auto itemPixels=iconImage([&]{ui.drawItemIcon(16,16,64,64,{legacyItem,1,0});});
        const auto materialPixels=iconImage([&]{ui.drawAtlasIcon(16,16,64,64,legacyTile,glm::vec4(1));});
        if(itemPixels!=materialPixels)throw std::runtime_error("Legacy cube item lost its independent icon material");
        Plugins::mutableContentState().extensions.renderPasses=iconPasses;
        std::cout<<"Plugin block shape Vulkan icon readback passed\n";

        main.onKeyPress(Key::Down);main.onKeyPress(Key::Down);main.onKeyPress(Key::Enter);
        for (const auto language : languagesByEnglishName()) {
            settings.language=language;localization.setLanguage(language);
            main.onKeyPress(Key::Escape);main.onKeyPress(Key::Down);
            main.onKeyPress(Key::Down);main.onKeyPress(Key::Enter);
            draw("language-menu-"+std::string(languageCode(language)),[&]{main.render(ui,width,height);});
        }
        main.onKeyPress(Key::Down);
        draw("language-menu-back",[&]{main.render(ui,width,height);});
        main.onKeyPress(Key::Escape);
        settings.language=Language::English;localization.setLanguage(settings.language);
        main.onKeyPress(Key::Enter);
        draw("worlds",[&]{main.render(ui,width,height);});
        main.onKeyPress(Key::Escape);main.onKeyPress(Key::Down);main.onKeyPress(Key::Down);
        main.onKeyPress(Key::Down);main.onKeyPress(Key::Down);main.onKeyPress(Key::Enter);
        draw("about",[&]{main.render(ui,width,height);});
        main.onKeyPress(Key::Escape);main.onKeyPress(Key::Enter);
        for (int i=0;i<std::clamp((height-192)/46,1,6)+2;++i) main.onKeyPress(Key::Down);
        main.onKeyPress(Key::Enter);
        draw("create",[&]{main.render(ui,width,height);});
        const bool configurableGeneration=std::any_of(Plugins::mutableContentState().extensions.generators.begin(),Plugins::mutableContentState().extensions.generators.end(),
            [](const auto& generator){return generator.second.dimension!=2&&generator.first!="official_heaven:heaven";})||
            std::any_of(Plugins::mutableContentState().extensions.modifiers.begin(),Plugins::mutableContentState().extensions.modifiers.end(),
            [](const auto& modifier){return modifier.second.dimension!=2;});
        if(!configurableGeneration) {
            bool created=false;
            auto fixedCallbacks=callbacks;
            fixedCallbacks.onGenerationConfig=[&](const auto& config) {
                for(const auto& dimension:Plugins::mutableContentState().extensions.dimensions)
                    if(!(config.at(dimension.first)==dimension.second.generation))
                        throw std::runtime_error("Hiding fixed dimension controls changed generation defaults");
            };
            fixedCallbacks.onCreateWorld=[&](const std::string&,const std::string&,GameMode,WorldType,bool){created=true;};
            MainMenu fixedMenu(fixedCallbacks,{},settings,localization,nullptr);
            fixedMenu.onKeyPress(Key::Enter);fixedMenu.onKeyPress(Key::Down);
            fixedMenu.onKeyPress(Key::Down);fixedMenu.onKeyPress(Key::Enter);
            // Five genuine fields, followed immediately by Create and Cancel.
            // Any fixed generator/parameter/toggle row would intercept this action.
            for(int row=0;row<5;++row)fixedMenu.onKeyPress(Key::Down);
            fixedMenu.onKeyPress(Key::Enter);
            if(!created)throw std::runtime_error("Fixed dimension controls remain in create-world menu");
            std::cout<<"Compact create-world menu preserves fixed Heaven/Nether defaults passed\n";
        }
        if(configurableGeneration) {
            localization.setLanguage(Language::SimplifiedChinese);
            std::string createdName;
            std::map<uint32_t,Plugins::GenerationConfig> createdGeneration;
            auto createCallbacks=callbacks;
            createCallbacks.onGenerationConfig=[&](const auto& config){createdGeneration=config;};
            createCallbacks.onCreateWorld=[&](const std::string& name,const std::string&,GameMode,WorldType,bool){createdName=name;};
            // Real alternatives remain switchable; fixed providers have no selector.
            int generatorRow=6; // Name, mode, type, seed, cheats and advanced toggle.
            int firstParameterRow=-1;
            uint32_t firstParameterDimension=0;
            for(const auto& dimension:Plugins::mutableContentState().extensions.dimensions) {
                if(dimension.first==2)continue;
                MainMenu generatorMenu(createCallbacks,{},settings,localization,nullptr);
                generatorMenu.onKeyPress(Key::Enter);generatorMenu.onKeyPress(Key::Down);
                generatorMenu.onKeyPress(Key::Down);generatorMenu.onKeyPress(Key::Enter);
                std::vector<std::string> choices;
                if(dimension.first==0||dimension.first==2)choices.emplace_back();
                for(const auto& generator:Plugins::mutableContentState().extensions.generators)
                    if(generator.second.dimension==dimension.first)choices.push_back(generator.first);
                const bool editable=std::any_of(Plugins::mutableContentState().extensions.generators.begin(),Plugins::mutableContentState().extensions.generators.end(),
                    [&](const auto& generator){return generator.second.dimension==dimension.first&&generator.first!="official_heaven:heaven";})||
                    std::any_of(Plugins::mutableContentState().extensions.modifiers.begin(),Plugins::mutableContentState().extensions.modifiers.end(),
                    [&](const auto& modifier){return modifier.second.dimension==dimension.first;});
                if(firstParameterRow<0&&editable) {
                    firstParameterRow=generatorRow+(choices.size()>1?1:0);
                    firstParameterDimension=dimension.first;
                }
                if(choices.size()<=1) {
                    for(const auto& modifier:Plugins::mutableContentState().extensions.modifiers)
                        if(modifier.second.dimension==dimension.first)++generatorRow;
                    continue;
                }
                auto expected=dimension.second.generation.generator;
                for(int activation=0;activation<3;++activation) {
                    // Rebuilding buttons restores selection to Name after each activation.
                    for(int row=0;row<generatorRow;++row)generatorMenu.onKeyPress(Key::Down);
                    generatorMenu.onKeyPress(Key::Enter);
                    if(!choices.empty()) {
                        const auto it=std::find(choices.begin(),choices.end(),expected);
                        expected=it==choices.end()?choices.front():choices[(size_t(it-choices.begin())+1)%choices.size()];
                    }
                }
                draw("create-generator-"+std::to_string(dimension.first),[&]{generatorMenu.render(ui,width,height);},1);
                // Return focus to Name, then wrap to the create action.
                generatorMenu.onKeyPress(Key::Tab);generatorMenu.onKeyPress(Key::Tab);
                generatorMenu.onKeyPress(Key::Up);generatorMenu.onKeyPress(Key::Up);generatorMenu.onKeyPress(Key::Enter);
                const auto& selected=createdGeneration.at(dimension.first);
                if(selected.generator!=expected||selected.parameters!=dimension.second.generation.parameters)
                    throw std::runtime_error("Dimension generator cycling changed defaults or selected the wrong generator");
                Plugins::validateGeneration(selected,dimension.first);
                if(selected.generator.empty()&&!selected.revision.empty())
                    throw std::runtime_error("Native generator retained a plugin revision");
                ++generatorRow;
                for(const auto& modifier:Plugins::mutableContentState().extensions.modifiers)
                    if(modifier.second.dimension==dimension.first)++generatorRow;
            }
            if(firstParameterRow<0)throw std::runtime_error("Configurable generation has no editable parameter field");
            std::cout<<"Create-world meaningful plugin generator activation passed\n";
            MainMenu createMenu(createCallbacks,{},settings,localization,nullptr);
            createMenu.onKeyPress(Key::Enter);createMenu.onKeyPress(Key::Down);
            createMenu.onKeyPress(Key::Down);createMenu.onKeyPress(Key::Enter);
            // Default Tab navigation must wrap from Seed to Name, not enter a
            // hidden JSON editor. The normal creation callback keeps defaults.
            createMenu.onKeyPress(Key::Tab);createMenu.onKeyPress(Key::Tab);
            createMenu.onKeyPress(Key::A,KeyModifier::Control);createMenu.onChar('N');
            draw("create-default-zh_cn",[&]{createMenu.render(ui,width,height);},3);
            createMenu.onKeyPress(Key::Up);createMenu.onKeyPress(Key::Up);createMenu.onKeyPress(Key::Enter);
            if(createdName!="N")throw std::runtime_error("Default creation focused a hidden parameter field");
            for(const auto& dimension:Plugins::mutableContentState().extensions.dimensions)
                if(createdGeneration.at(dimension.first).parameters!=dimension.second.generation.parameters)
                    throw std::runtime_error("Default creation changed generator parameters");
            // Advanced access remains optional, and collapsing while editing
            // must restore a visible focus without discarding the edited value.
            createMenu.onKeyPress(Key::Tab);createMenu.onKeyPress(Key::Tab);
            for(int i=0;i<5;++i)createMenu.onKeyPress(Key::Down);
            createMenu.onKeyPress(Key::Enter);
            draw("create-advanced-zh_cn",[&]{createMenu.render(ui,width,height);},3);
            createMenu.onKeyPress(Key::Tab);createMenu.onKeyPress(Key::Tab);
            createMenu.onKeyPress(Key::A,KeyModifier::Control);
            for(char c:std::string(R"({"ui":7})"))createMenu.onChar(static_cast<unsigned char>(c));
            // The first editable dimension may have only one fixed generator,
            // so its parameter row need not follow a generator selector.
            for(int row=firstParameterRow;row>5;--row)createMenu.onKeyPress(Key::Up);
            createMenu.onKeyPress(Key::Enter);
            createMenu.onKeyPress(Key::Tab);createMenu.onKeyPress(Key::Tab);
            createMenu.onKeyPress(Key::A,KeyModifier::Control);createMenu.onChar('C');
            createMenu.onKeyPress(Key::Up);createMenu.onKeyPress(Key::Up);createMenu.onKeyPress(Key::Enter);
            if(createdName!="C"||createdGeneration.at(firstParameterDimension).parameters!=R"({"ui":7})")
                throw std::runtime_error("Collapsing advanced generation lost text focus or parameters");
            localization.setLanguage(Language::English);
            std::cout<<"Create-world default/advanced parameter focus and preservation passed\n";
        }
        draw("settings",[&]{options.render(ui,width,height);});
        if(width>=440 && height>=500) {
            // The first row belongs near screen center, independently of Back.
            const bool autoJump=settings.autoJump;
            click(options,width*.5f-140,height*.5f+35);
            if(settings.autoJump==autoJump)
                throw std::runtime_error("Settings content is not centered above the fixed footer");
            click(options,width*.5f-140,height*.5f+35);
        }
        const auto choose=[&](int index) {
            const int columns=width<440?1:2;
            for (int i=0;i<index/columns;++i) options.onKeyPress(Key::Down);
            if (index%columns) options.onKeyPress(Key::Right);
            options.onKeyPress(Key::Enter);
        };
        choose(1);draw("settings-video",[&]{options.render(ui,width,height);});
        choose(4);draw("settings-effects",[&]{options.render(ui,width,height);});
        options.onKeyPress(Key::Escape);choose(12);
        draw("settings-lod",[&]{options.render(ui,width,height);});
        options.onKeyPress(Key::Escape);options.onKeyPress(Key::Escape);choose(2);
        draw("settings-audio",[&]{options.render(ui,width,height);});
        options.onKeyPress(Key::Escape);choose(3);
        draw("settings-bindings",[&]{options.render(ui,width,height);});
        choose(0);draw("settings-keyboard",[&]{options.render(ui,width,height);});
        // Reaching the last control must scroll it into view on narrow screens.
        options.onKeyPress(Key::Up);
        options.onKeyPress(Key::Up);
        draw("settings-keyboard-scroll",[&]{options.render(ui,width,height);});
        options.onKeyPress(Key::Escape);choose(1);
        draw("settings-controller",[&]{options.render(ui,width,height);});
        options.onKeyPress(Key::Escape);choose(2);
        draw("settings-touch",[&]{options.render(ui,width,height);});
        draw("pause",[&]{pause.render(ui,width,height);});
        draw("sleep",[&]{sleep.render(ui,width,height);});
        draw("creative",[&]{creative.render(ui,width,height,width-16,height-16);});
        draw("survival",[&]{survival.render(ui,width,height,0,0);});
        survival.setCraftingTable(true);
        draw("crafting",[&]{survival.render(ui,width,height,0,0);});
        draw("chest",[&]{container.render(ui,width,height,0,0);});
        access.block.type=BlockEntityType::Furnace;access.block.input=items.slot(0);
        access.block.burnRemaining=80;access.block.burnTotal=160;access.block.cookProgress=100;
        draw("furnace",[&]{container.render(ui,width,height,0,0);});
        access.block={};access.block.type=BlockEntityType::BrewingStand;
        access.block.chest[0]={ItemId::FIRE_RESISTANCE_POTION,1,0};access.block.chest[1]={ItemId::STRENGTH_POTION,1,0};access.block.chest[2]={ItemId::HEALING_POTION,1,0};
        access.block.chest[3]={ItemId::NETHER_WART,10,0};access.block.chest[4]={ItemId::BLAZE_POWDER,5,0};access.block.burnRemaining=16;access.block.cookProgress=200;
        for(const auto language:{Language::English,Language::SimplifiedChinese,Language::Arabic}) {
            localization.setLanguage(language);draw("nether-brewing-"+std::string(languageCode(language)),[&]{container.render(ui,width,height,0,0);});
        }
        access.block={};access.block.type=BlockEntityType::SmithingTable;
        access.block.chest[0]={ItemId::NETHERITE_UPGRADE_TEMPLATE,2,0};access.block.chest[1]={ItemId::DIAMOND_SWORD,1,417};access.block.chest[2]={ItemId::NETHERITE_INGOT,3,0};
        access.block.output=smithingResult(access.block.chest[0],access.block.chest[1],access.block.chest[2]);
        for(const auto language:{Language::English,Language::SimplifiedChinese,Language::Arabic}) {
            localization.setLanguage(language);draw("nether-smithing-"+std::string(languageCode(language)),[&]{container.render(ui,width,height,0,0);});
        }
        localization.setLanguage(Language::English);
        draw("trades",[&]{trades.render(ui,width,height,0,0);});
        draw("hud-touch",[&]{
            UiTheme::menuBackground(ui,width,height);hotbar.render(ui,width,height);touch.render(ui);
            for (int i=0;i<10;++i) UiTheme::sprite(ui,width*.5f-140+i*14,80,1.5f,
                UiTheme::HEART_FULL,UiTheme::HEART_PALETTE);
        });
        draw("tooltip",[&]{
            UiTheme::menuBackground(ui,width,height);
            UiTheme::tooltip(ui,width-8,height-8,"A long inventory tooltip that wraps safely within a small viewport");
        });
        int activations=0;
        Button feedbackButton(localization.text("menu.home.settings"),[&]{++activations;});
        const float feedbackWidth=std::min(320.0f,width-32.0f);
        feedbackButton.setPosition((width-feedbackWidth)*.5f,height*.5f-22);
        feedbackButton.setSize(feedbackWidth,44);
        const auto feedbackScene=[&]{
            UiTheme::menuBackground(ui,width,height);feedbackButton.render(ui);
        };
        draw("button-normal",feedbackScene);
        feedbackButton.setHovered(true);draw("button-hover",feedbackScene);
        feedbackButton.setPressed(true);draw("button-pressed",feedbackScene);
        feedbackButton.setPressed(false);draw("button-released",feedbackScene);
        feedbackButton.setHovered(false);feedbackButton.setSelected(true);
        draw("button-navigation",feedbackScene);
        feedbackButton.activate();draw("button-activation",feedbackScene,4);
        if (activations!=1) throw std::runtime_error("feedback delays or duplicates activation");
        feedbackButton.setEnabled(false);draw("button-disabled",feedbackScene);
        creative.onGamepadNavigate(1,0);
        draw("creative-focus",[&]{creative.render(ui,width,height,-10000,-10000);});
        survival.onGamepadNavigate(1,0);
        draw("survival-focus",[&]{survival.render(ui,width,height,-10000,-10000);});
        container.onGamepadNavigate(1,0);
        draw("container-focus",[&]{container.render(ui,width,height,-10000,-10000);});
        touch.onTouch({{1,1},TouchPhase::Begin,width-20.0,height-20.0});
        draw("hud-touch-pressed",[&]{
            UiTheme::menuBackground(ui,width,height);hotbar.render(ui,width,height);touch.render(ui);
        });
        touch.onTouch({{1,1},TouchPhase::End,width-20.0,height-20.0});
        for (Language language:languagesByEnglishName()) {
            localization.setLanguage(language);
            SettingsMenu translated(settings,[]{},[]{},localization);
            draw(std::string("language-")+std::string(languageCode(language)),[&]{translated.render(ui,width,height);});
            const auto measured=ui.measureText(localization.text("settings.title"),1);
            if (measured.x<=0 || measured.y!=14) throw std::runtime_error("localized font measurement failed");
        }
        for (const Language language : languagesByEnglishName()) {
            localization.setLanguage(language);
            const std::string input="/gamerule keep_inventory tr";
            const auto suggestions=commandSuggestions(input,input.size());
            if (suggestions.size()!=1 || suggestions[0].text!="true")
                throw std::runtime_error("GameRule completion failed during Vulkan smoke");
            draw("gamerule-chat-"+std::string(languageCode(language)),[&]{
                UiTheme::menuBackground(ui,width,height);
                ui.renderText(input,16,height-35,1,glm::vec3(1));
                ui.renderText("Tab: "+suggestions[0].text,16,height-60,1,glm::vec3(1));
                UiTheme::tooltip(ui,width-8,height-90,
                    localization.format("message.gamerule_set",{"keep_inventory","true"})+"\n"+
                    localization.text("message.gamerule_partial")+"\n"+
                    localization.text("message.gamerule_unavailable"));
            },3);
        }
        std::cout<<"Modern UI Vulkan smoke passed\n";
    } catch (const std::exception& error) { std::cerr<<"UI smoke failed: "<<error.what()<<'\n';return 1; }
}
