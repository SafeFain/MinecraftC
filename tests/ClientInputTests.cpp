#include "game/ClientSettings.h"
#include "game/InventoryInteraction.h"

#include <GLFW/glfw3.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace { void require(bool v,const char* m){if(!v){std::cerr<<"FAILED: "<<m<<'\n';std::exit(1);}} }

int main(){
    const auto root=std::filesystem::temp_directory_path()/"minecraftc-client-input-test";
    std::filesystem::remove_all(root);
    ClientSettings settings;
    settings.mouseSensitivity=.42f;settings.guiScale=3;settings.invertMouseY=true;
    settings.renderDistance=8;settings.renderClouds=false;
    settings.cloudRenderDistance=512;settings.smoothLighting=false;
    settings.bindings[static_cast<size_t>(InputAction::Inventory)]={InputDevice::Mouse,3};
    require(settings.save(root/"options.txt"),"settings save succeeds");
    const auto loaded=ClientSettings::load(root/"options.txt");
    require(loaded.mouseSensitivity==.42f&&loaded.guiScale==3&&loaded.invertMouseY&&
            loaded.renderDistance==8&&!loaded.renderClouds&&
            loaded.cloudRenderDistance==512&&!loaded.smoothLighting,
            "client settings round trip");
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
    require(!inputActionCanUnbind(InputAction::Jump)&&inputActionCanUnbind(InputAction::Command),
            "core bindings are protected");

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
