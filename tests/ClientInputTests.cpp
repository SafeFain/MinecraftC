#include "game/ClientSettings.h"
#include "game/InventoryInteraction.h"
#include "ui/TouchControls.h"
#include "ui/UIRenderer.h"

#include "core/InputCodes.h"
#include "core/RuntimeClock.h"
#include "core/AssetStore.h"
#include "core/GamepadManager.h"
#include <SDL3/SDL.h>
#include "core/TextEditBuffer.h"
#include "core/Window.h"
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
    const WindowSafeArea safe = projectWindowSafeArea(
        20, 10, 600, 330, 640, 360, 1280, 720);
    require(safe.x==40&&safe.y==40&&safe.width==1200&&safe.height==660,
            "top-left logical safe area projects to bottom-left framebuffer coordinates");
    require(SDL_InitSubSystem(SDL_INIT_JOYSTICK|SDL_INIT_GAMEPAD),
            "SDL gamepad test subsystem initializes");
    SDL_VirtualJoystickDesc virtualDesc{};SDL_INIT_INTERFACE(&virtualDesc);
    virtualDesc.type=SDL_JOYSTICK_TYPE_GAMEPAD;virtualDesc.naxes=6;virtualDesc.nbuttons=15;
    virtualDesc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_LEFTX)|(1u<<SDL_GAMEPAD_AXIS_LEFTY);
    virtualDesc.button_mask=1u<<SDL_GAMEPAD_BUTTON_SOUTH;virtualDesc.name="MinecraftC Test Gamepad";
    const SDL_JoystickID virtualId=SDL_AttachVirtualJoystick(&virtualDesc);
    require(virtualId!=0,"virtual SDL gamepad attaches");
    {GamepadManager manager;require(manager.available(),"gamepad manager opens an existing virtual gamepad");
        SDL_Joystick* joystick=SDL_OpenJoystick(virtualId);require(joystick!=nullptr,"virtual joystick opens");
        require(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTY,-32768),"virtual axis update succeeds");
        SDL_UpdateJoysticks();std::array<bool,32> buttons{};std::array<float,16> axes{};manager.sample(buttons,axes);
        require(axes[SDL_GAMEPAD_AXIS_LEFTY]<-.99f,"active gamepad sampling exposes virtual axis state");
        SDL_CloseJoystick(joystick);}
    require(SDL_DetachVirtualJoystick(virtualId),"virtual SDL gamepad detaches");
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK|SDL_INIT_GAMEPAD|SDL_INIT_HAPTIC);
    require(normalizeGamepadAxis(.1f,.18f)==0.0f&&
            normalizeGamepadAxis(1.0f,.18f)==1.0f&&
            normalizeGamepadAxis(-1.0f,.18f)==-1.0f,
            "gamepad deadzone normalization is signed and bounded");
    require(AssetStore::validPath("textures/generated/a.png")&&
            !AssetStore::validPath("../secret")&&!AssetStore::validPath("/absolute")&&
            !AssetStore::validPath("a/./b")&&!AssetStore::validPath("a\\b"),
            "asset paths remain title-root-relative and normalized");
    const auto root=std::filesystem::temp_directory_path()/"minecraftc-client-input-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root/"assets");
    {std::ofstream text(root/"assets"/"sample.txt",std::ios::binary);text<<"hello";}
    {std::ofstream empty(root/"assets"/"empty.bin",std::ios::binary);}
    {AssetStore assets(root/"assets");
        require(assets.readText("sample.txt")=="hello"&&assets.readBinary("empty.bin").empty(),
                "title storage reads text and empty binary assets");
        auto stream=assets.openMemory("empty.bin");require(static_cast<bool>(stream),"empty assets expose a valid IOStream");
        bool missing=false;try{assets.readBinary("missing.bin");}catch(const std::exception&){missing=true;}
        require(missing,"missing title assets report failure");}
    ClientSettings settings;
    settings.mouseSensitivity=.42f;settings.guiScale=3;settings.invertMouseY=true;
    settings.renderDistance=8;settings.renderClouds=false;
    settings.cloudRenderDistance=1024;settings.smoothLighting=false;
    settings.language=Language::SimplifiedChinese;
    settings.bindings[static_cast<size_t>(InputAction::Inventory)]={InputDevice::Mouse,3};
    settings.controlMode=ControlMode::Touch;settings.touchSensitivity=1.75f;
    settings.gamepadDeadzone=.22f;settings.gamepadLookSensitivity=1.5f;
    settings.invertGamepadY=true;settings.gamepadRumble=.6f;
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
    require(loaded.gamepadDeadzone==.22f&&loaded.gamepadLookSensitivity==1.5f&&
            loaded.invertGamepadY&&loaded.gamepadRumble==.6f,
            "gamepad settings round trip");
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
    {
        std::ofstream legacy(root/"v5-bindings.txt");
        legacy<<"version=5\nbinding.0=1,90\nbinding.7=1,69\n"
              <<"binding.8=1,999\nbinding.4=1,999\n";
    }
    const auto migrated=ClientSettings::load(root/"v5-bindings.txt");
    require(migrated.bindings[0]==InputBinding{InputDevice::Keyboard,29}&&
            migrated.bindings[7]==InputBinding{InputDevice::Keyboard,Key::E},
            "v5 GLFW keyboard bindings migrate to stable physical keys");
    require(migrated.bindings[8]==InputBinding{}&&
            migrated.bindings[4]==ClientSettings{}.bindings[4],
            "unknown legacy bindings unbind optional actions and restore required defaults");
    {std::ofstream v6(root/"v6-bindings.txt");v6<<"version=6\nbinding.0=1,"<<Key::W<<"\n";}
    require(ClientSettings::load(root/"v6-bindings.txt").bindings[0]==InputBinding{InputDevice::Keyboard,Key::W},
            "v6 physical bindings are preserved while gamepad defaults are added");
    require(loaded.bindings[static_cast<size_t>(InputAction::Inventory)]==InputBinding{InputDevice::Mouse,3},
            "mouse binding round trips");
    require(effectiveGuiScale(1920,1080,0)==3&&effectiveGuiScale(800,450,0)==1,
            "automatic GUI scale preserves minimum virtual size");

    InputState input;input.beginFrame();input.keyEvent(Key::W,ButtonAction::Press);
    input.update(loaded.bindings);
    require(input.held(InputAction::MoveForward)&&input.pressed(InputAction::MoveForward),
            "bound key exposes held and pressed state");
    input.beginFrame();input.keyEvent(Key::W,ButtonAction::Release);input.update(loaded.bindings);
    require(input.released(InputAction::MoveForward),"release edge is exposed");
    input.beginFrame();input.keyEvent(Key::W,ButtonAction::Press);input.update(loaded.bindings);
    input.beginFrame();input.clearPhysical();input.update(loaded.bindings);
    require(input.released(InputAction::MoveForward),
            "focus-loss physical reset releases held actions");
    input.beginFrame();input.clearVirtual();input.setVirtual(InputAction::MoveForward,.4f);
    input.update(loaded.bindings);
    require(input.held(InputAction::MoveForward)&&input.value(InputAction::MoveForward)==.4f,
            "virtual analog actions merge into input state");
    require(!inputActionCanUnbind(InputAction::Jump)&&inputActionCanUnbind(InputAction::Command),
            "core bindings are protected");
    std::array<bool,32> gamepadButtons{};std::array<float,16> gamepadAxes{};
    gamepadAxes[1]=-1.0f;
    input.beginFrame();input.clearVirtual();input.updateGamepad(
        loaded.gamepadBindings,gamepadButtons,gamepadAxes,loaded.gamepadDeadzone);
    input.update(loaded.bindings);
    require(input.value(InputAction::MoveForward)==1.0f,
            "independent negative-axis gamepad binding drives movement");
    gamepadAxes.fill(0.0f);
    input.updateGamepad(loaded.gamepadBindings,gamepadButtons,gamepadAxes,
                        loaded.gamepadDeadzone);

    TextEditBuffer edit("A\xE4\xB8\xAD",8);
    edit.moveLeft();edit.backspace();
    require(edit.text()=="\xE4\xB8\xAD","UTF-8 cursor backspace removes one codepoint");
    edit.moveHome();edit.insert("B\nC");edit.selectAll();
    require(edit.selectedText()=="BC\xE4\xB8\xAD","text insertion strips line breaks and selection is byte-safe");
    edit.moveEnd();edit.moveLeft(true);edit.eraseForward();
    require(edit.text()=="BC","forward deletion erases the selected multibyte codepoint");
    require(RuntimeClock::elapsed(20,10)==0&&RuntimeClock::milliseconds(2'500'000)==2&&
            RuntimeClock::fromSeconds(.5)==500'000'000,
            "runtime clock conversions saturate backwards elapsed time");

    TouchControls touch;TouchControlConfig config;touch.configure(1000,600,config);
    auto commands=touch.onTouch({{0,1},TouchPhase::Begin,76,176});
    require(commands.empty(),"joystick capture emits no discrete command");
    input.beginFrame();input.clearVirtual();touch.applyTo(input);input.update(loaded.bindings);
    require(input.value(InputAction::MoveForward)>.99f&&input.held(InputAction::Sprint),
            "outer joystick produces forward movement and sprint");
    touch.onTouch({{0,2},TouchPhase::Begin,500,300});
    touch.onTouch({{0,2},TouchPhase::Move,520,310});
    require(touch.consumeLookDelta()==glm::vec2(20,10),"look touch reports relative delta");
    commands=touch.onTouch({{0,3},TouchPhase::Begin,890,160});
    require(commands.size()==1&&commands[0].command==TouchCommand::AttackPress,
            "action button emits attack press while other touches remain active");
    commands=touch.onTouch({{0,3},TouchPhase::End,0,0});
    require(commands.size()==1&&commands[0].command==TouchCommand::AttackRelease,
            "action button emits matching release");
    commands=touch.onTouch({{1,7},TouchPhase::Begin,890,160});
    require(commands.size()==1&&commands[0].command==TouchCommand::AttackPress,
            "touch identity includes the SDL touch device");
    commands=touch.onTouch({{2,7},TouchPhase::Begin,890,100});
    require(commands.size()==1&&commands[0].command==TouchCommand::UsePress,
            "equal finger ids from different devices remain independent");
    touch.cancelAll();
    touch.cancelAll();input.beginFrame();input.clearVirtual();touch.applyTo(input);input.update(loaded.bindings);
    require(!input.held(InputAction::MoveForward)&&input.released(InputAction::MoveForward),
            "touch cancellation releases virtual movement");
    config.leftHanded=true;touch.configure(1000,600,config);
    touch.onTouch({{0,4},TouchPhase::Begin,924,176});
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
