#include "core/Window.h"
#include "entity/EntityManager.h"
#include "game/SessionAccess.h"
#include "game/Command.h"
#include "renderer/backend/vulkan/VulkanRenderer.h"
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
#include <iostream>
#include <functional>
#include <chrono>

namespace {
struct Access final : IContainerAccess,ITradeAccess {
    BlockEntity block;
    Entity villager;
    Access() {
        villager.type=EntityType::Villager;
        villager.villager.profession=VillagerProfession::Farmer;
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
        const int width=argc>=4?std::stoi(argv[2]):960,height=argc>=4?std::stoi(argv[3]):600;
        const bool capture=argc==5 && std::string(argv[4])=="--capture";
        const auto assets=std::filesystem::absolute(argv[1]);
        Window window(width,height,"MinecraftC UI smoke",Window::SurfaceMode::Vulkan,false,false);
        VulkanRenderer renderer;renderer.initialize(window,assets);
        renderer.setVisualQuality(VisualQuality::Low);renderer.setEnhancedVisuals(false);
        UIRenderer ui;Localization localization;localization.load(assets);
        ui.setLocalization(localization);ui.initialize(renderer,renderer.getBlockAtlasTexture(),assets);
        ClientSettings settings;
        InventoryModel items;
        items.slot(0)={ItemId::STONE,64,0};items.slot(1)={ItemId::OAK_PLANKS,32,0};
        Access access;access.block.chest[0]=items.slot(0);
        MenuCallbacks callbacks;
        callbacks.onOpenSettings=[]{};callbacks.onQuit=[]{};
        callbacks.onCreateWorld=[](const std::string&,const std::string&,GameMode,WorldType,bool){};
        callbacks.onOpenWorld=[](const std::string&){};
        callbacks.onDeleteWorld=[](const std::string&){return true;};
        std::vector<WorldSummary> worlds;
        for (int i=0;i<6;++i) {
            WorldSummary world;world.id=std::to_string(i);world.displayName="Mountain valley "+std::to_string(i+1);
            world.seed=88123+i;world.compatible=i!=5;world.generationVersion=i!=5?16:15;worlds.push_back(world);
        }
        MainMenu main(callbacks,worlds,settings,localization,nullptr);
        SettingsMenu options(settings,[]{},[]{},localization);
        PauseMenu pause(callbacks,localization);SleepMenu sleep(callbacks,localization,false);
        CreativeInventory creative;SurvivalInventoryScreen survival(items);
        ContainerScreen container(items);container.open(access,{0,0,0});
        VillagerTradeScreen trades(items);trades.open(access,1);
        Hotbar hotbar;hotbar.setInventory(&items);
        TouchControls touch;touch.configure(width,height,{});
        const auto draw=[&](const std::string& name,const std::function<void()>& render,int frames=12) {
            size_t previousVertices=0,previousBatches=0;
            for (int frame=0;frame<frames;++frame) {
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
            if (capture) { std::string next;std::getline(std::cin,next); }
        };
        draw("home",[&]{main.render(ui,width,height);});
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
        draw("settings",[&]{options.render(ui,width,height);});
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
