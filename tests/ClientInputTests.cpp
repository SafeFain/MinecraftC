#include "game/ClientSettings.h"
#include "game/InventoryInteraction.h"
#include "ui/TouchControls.h"
#include "ui/UIRenderer.h"

#include <GLFW/glfw3.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace { void require(bool v,const char* m){if(!v){std::cerr<<"FAILED: "<<m<<'\n';std::exit(1);}} }

// TouchControls::render is linked into this logic test, but no OpenGL-backed
// renderer is constructed. These three inert definitions keep the test focused
// on touch capture and input output.
void UIRenderer::drawRect(float,float,float,float,const glm::vec4&) {}
void UIRenderer::renderText(const std::string&,float,float,float,const glm::vec3&) {}
glm::vec2 UIRenderer::measureText(const std::string&,float) { return {0,0}; }
std::string Localization::text(std::string_view key) const { return std::string(key); }

int main(){
    const auto root=std::filesystem::temp_directory_path()/"minecraftc-client-input-test";
    std::filesystem::remove_all(root);
    ClientSettings settings;
    settings.mouseSensitivity=.42f;settings.guiScale=3;settings.invertMouseY=true;
    settings.renderDistance=8;settings.renderClouds=false;
    settings.cloudRenderDistance=1024;settings.smoothLighting=false;
    settings.language=Language::SimplifiedChinese;
    settings.bindings[static_cast<size_t>(InputAction::Inventory)]={InputDevice::Mouse,3};
    settings.controlMode=ControlMode::Touch;settings.touchSensitivity=1.75f;
    settings.touchControlSize=1.25f;settings.touchControlOpacity=.8f;settings.touchLeftHanded=true;
    require(settings.save(root/"options.txt"),"settings save succeeds");
    const auto loaded=ClientSettings::load(root/"options.txt");
    require(loaded.mouseSensitivity==.42f&&loaded.guiScale==3&&loaded.invertMouseY&&
            loaded.renderDistance==8&&!loaded.renderClouds&&
            loaded.cloudRenderDistance==1024&&!loaded.smoothLighting,
            "client settings round trip");
    require(loaded.controlMode==ControlMode::Touch&&loaded.touchSensitivity==1.75f&&
            loaded.touchControlSize==1.25f&&loaded.touchControlOpacity==.8f&&loaded.touchLeftHanded,
            "touch settings round trip");
    require(loaded.language==Language::SimplifiedChinese,
            "client language round trips");
    {
        std::ofstream legacy(root/"legacy-options.txt");
        legacy<<"version=3\nrender_distance=8\n";
    }
    require(ClientSettings::load(root/"legacy-options.txt").language==Language::English,
            "legacy settings default to English");
    {
        std::ofstream invalid(root/"invalid-options.txt");
        invalid<<"version=4\nlanguage=unsupported\n";
    }
    require(ClientSettings::load(root/"invalid-options.txt").language==Language::English,
            "unsupported language falls back to English");
    require(loaded.bindings[static_cast<size_t>(InputAction::Inventory)]==InputBinding{InputDevice::Mouse,3},
            "mouse binding round trips");
    require(effectiveGuiScale(1920,1080,0)==3&&effectiveGuiScale(800,450,0)==1,
            "automatic GUI scale preserves minimum virtual size");

    InputState input;input.beginFrame();input.keyEvent(GLFW_KEY_W,GLFW_PRESS);
    input.update(loaded.bindings);
    require(input.held(InputAction::MoveForward)&&input.pressed(InputAction::MoveForward),
            "bound key exposes held and pressed state");
    input.beginFrame();input.keyEvent(GLFW_KEY_W,GLFW_RELEASE);input.update(loaded.bindings);
    require(input.released(InputAction::MoveForward),"release edge is exposed");
    input.beginFrame();input.clearVirtual();input.setVirtual(InputAction::MoveForward,.4f);
    input.update(loaded.bindings);
    require(input.held(InputAction::MoveForward)&&input.value(InputAction::MoveForward)==.4f,
            "virtual analog actions merge into input state");
    require(!inputActionCanUnbind(InputAction::Jump)&&inputActionCanUnbind(InputAction::Command),
            "core bindings are protected");

    TouchControls touch;TouchControlConfig config;touch.configure(1000,600,config);
    auto commands=touch.onTouch({1,TouchPhase::Begin,76,176});
    require(commands.empty(),"joystick capture emits no discrete command");
    input.beginFrame();input.clearVirtual();touch.applyTo(input);input.update(loaded.bindings);
    require(input.value(InputAction::MoveForward)>.99f&&input.held(InputAction::Sprint),
            "outer joystick produces forward movement and sprint");
    touch.onTouch({2,TouchPhase::Begin,500,300});
    touch.onTouch({2,TouchPhase::Move,520,310});
    require(touch.consumeLookDelta()==glm::vec2(20,10),"look touch reports relative delta");
    commands=touch.onTouch({3,TouchPhase::Begin,890,160});
    require(commands.size()==1&&commands[0].command==TouchCommand::AttackPress,
            "action button emits attack press while other touches remain active");
    commands=touch.onTouch({3,TouchPhase::End,0,0});
    require(commands.size()==1&&commands[0].command==TouchCommand::AttackRelease,
            "action button emits matching release");
    touch.cancelAll();input.beginFrame();input.clearVirtual();touch.applyTo(input);input.update(loaded.bindings);
    require(!input.held(InputAction::MoveForward)&&input.released(InputAction::MoveForward),
            "touch cancellation releases virtual movement");
    config.leftHanded=true;touch.configure(1000,600,config);
    touch.onTouch({4,TouchPhase::Begin,924,176});
    input.beginFrame();input.clearVirtual();touch.applyTo(input);input.update(loaded.bindings);
    require(input.value(InputAction::MoveForward)>.99f,
            "left-handed layout mirrors the movement joystick");
    touch.cancelAll();

    ItemStack cursor{ItemId::COAL,8,0},slot{ItemId::COAL,60,0};
    InventoryInteraction::click(cursor,slot,false);
    require(slot.count==64&&cursor.count==4,"click merges up to stack limit");
    ItemStack a{ItemId::COAL,10,0},b{},c{ItemId::COAL,62,0};
    require(InventoryInteraction::transfer(a,{&b,&c})==10&&a.empty()&&c.count==64&&b.count==8,
            "quick transfer merges before empty slots");
    ItemStack gatherCursor{ItemId::COAL,1,0},g1{ItemId::COAL,40,0},g2{ItemId::COAL,40,0};
    InventoryInteraction::gather(gatherCursor,{&g1,&g2});
    require(gatherCursor.count==64&&g1.empty()&&g2.count==17,"double click gather respects maximum");
    std::filesystem::remove_all(root);
    std::cout<<"Client input and inventory interaction tests passed\n";
}
