#include "game/ClientSettings.h"
#include "game/InventoryInteraction.h"
#include "audio/AudioSystem.h"
#include "ui/TouchControls.h"
#include "ui/UIRenderer.h"
#include "ui/SettingsMenu.h"

#include "core/InputCodes.h"
#include "core/RuntimeClock.h"
#include "core/AssetStore.h"
#include "core/GamepadManager.h"
#include <SDL3/SDL.h>
#include "core/TextEditBuffer.h"
#include "core/Window.h"
#include "platform/Clipboard.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
void require(bool v,const char* m){if(!v){std::cerr<<"FAILED: "<<m<<'\n';std::exit(1);}}
class FakeClipboard final : public platform::Clipboard {
public:
    bool writeText(std::string_view value) override { text.assign(value); return true; }
    bool readText(std::string& value) override { value = text; return true; }
    std::string text;
};
}

// TouchControls::render is linked into this logic test, but no graphics-backed
// renderer is constructed. These three inert definitions keep the test focused
// on touch capture and input output.
void UIRenderer::drawRect(float,float,float,float,const glm::vec4&) {}
void UIRenderer::renderText(const std::string&,float,float,float,const glm::vec3&) {}
glm::vec2 UIRenderer::measureText(const std::string&,float) { return {0,0}; }
std::string Localization::text(std::string_view key) const { return std::string(key); }

int main(){
    require(SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy"),
            "SDL dummy audio driver hint is accepted");
    {
        AudioSystem audio;
        require(audio.initialize(),"SDL dummy audio device initializes");
        require(!audio.paused(),"audio starts resumed");
        require(audio.musicMode()==AudioMusicMode::Menu,
                "main-menu music is the default audio mode");
        audio.setMusicMode(AudioMusicMode::Overworld);
        require(audio.musicMode()==AudioMusicMode::Overworld,
                "overworld music mode can be selected");
        audio.setMusicMode(AudioMusicMode::Heaven);
        require(audio.musicMode()==AudioMusicMode::Heaven,
                "Heaven music mode is separate from Overworld music");
        audio.setPaused(true);
        require(audio.paused(),"audio device pauses with the game");
        audio.setPaused(true);
        require(audio.paused(),"repeated audio pause is idempotent");
        audio.setPaused(false);
        require(!audio.paused(),"audio device resumes with the game");
    }
    require(settingsParentPage(SettingsPage::KeyboardMouse)==SettingsPage::KeyBindings&&
            settingsParentPage(SettingsPage::Controller)==SettingsPage::KeyBindings&&
            settingsParentPage(SettingsPage::Touch)==SettingsPage::KeyBindings,
            "input device settings return to the key bindings hub");
    require(settingsParentPage(SettingsPage::KeyBindings)==SettingsPage::General&&
            settingsParentPage(SettingsPage::Video)==SettingsPage::General&&
            settingsParentPage(SettingsPage::Lod)==SettingsPage::Video,
            "top-level settings pages return to general settings");
    const SettingsButtonLayout bindingLayout = settingsButtonLayout(620.0f,468.0f,13,true);
    require(bindingLayout.firstButtonY+bindingLayout.buttonHeight<=
                bindingLayout.helpY-9.9f&&bindingLayout.firstButtonY>=0.0f,
            "binding controls stay below help text in a short window");
    require(bindingLayout.rowCount==7&&bindingLayout.buttonWidth==280.0f,
            "settings height is based on two-column row count");
    const SettingsButtonLayout generalLayout=settingsButtonLayout(480.0f,468.0f,5,false);
    require(generalLayout.firstButtonY+generalLayout.buttonHeight<=450.1f,
            "settings without help retain title clearance");
    const glm::vec2 firstButton=settingsButtonPosition(generalLayout,0,5);
    const glm::vec2 secondButton=settingsButtonPosition(generalLayout,1,5);
    const glm::vec2 lastButton=settingsButtonPosition(generalLayout,4,5);
    require(firstButton.x>=13.9f&&
            secondButton.x+generalLayout.buttonWidth<=466.1f&&
            firstButton.y==secondButton.y&&lastButton.x>firstButton.x&&
            lastButton.x<secondButton.x,
            "settings use bounded columns and center an unpaired final action");
    require(settingsGridNeighbor(0,5,1,0)==1&&
            settingsGridNeighbor(1,5,0,1)==3&&
            settingsGridNeighbor(3,5,0,1)==1&&
            settingsGridNeighbor(2,5,0,1)==4&&
            settingsGridNeighbor(4,5,0,-1)==2&&
            settingsGridNeighbor(4,5,1,0)==4,
            "settings grid navigation follows visible rows and columns");
    const SettingsButtonLayout backLayout = settingsButtonLayout(
        620.0f, 468.0f, 6, false, true);
    const glm::vec2 precedingButton = settingsButtonPosition(
        backLayout, 4, 6, true);
    const glm::vec2 backButton = settingsButtonPosition(
        backLayout, 5, 6, true);
    require(backLayout.rowCount==4&&backButton.y<precedingButton.y&&
            backButton.x>backLayout.leftX&&
            backButton.x<backLayout.leftX+backLayout.buttonWidth+
                backLayout.columnGap,
            "an even-count settings page puts Back alone on the bottom row");
    require(settingsGridNeighbor(3,6,0,1,true)==5&&
            settingsGridNeighbor(4,6,0,1,true)==5&&
            settingsGridNeighbor(5,6,0,-1,true)==4&&
            settingsGridNeighbor(5,6,0,1,true)==0&&
            settingsGridNeighbor(5,6,1,0,true)==5,
            "grid navigation treats the standalone Back row as the final row");
    require(frameRateFromSlider(10.0f,10.0f,170.0f)==30&&
            frameRateFromSlider(180.0f,10.0f,170.0f)==200&&
            frameRateFromSlider(95.0f,10.0f,170.0f)==115&&
            frameRateSliderFraction(30)==0.0f&&frameRateSliderFraction(200)==1.0f,
            "frame-rate slider continuously maps its full 30-200 range");
    require(defaultVisualQuality(DesktopPlatform::Android)==VisualQuality::Low&&
            defaultVisualQuality(DesktopPlatform::IOS)==VisualQuality::Low&&
            defaultVisualQuality(DesktopPlatform::Linux)==VisualQuality::Medium&&
            defaultVisualQuality(DesktopPlatform::Windows)==VisualQuality::Medium&&
            defaultVisualQuality(DesktopPlatform::MacOS)==VisualQuality::Medium,
            "mobile defaults reduce fill-rate cost without changing desktop quality");
    require(!defaultLeafTransparency(VisualQuality::Low)&&
            !defaultLeafTransparency(VisualQuality::Medium)&&
            defaultLeafTransparency(VisualQuality::High)&&
            defaultLeafTransparency(VisualQuality::Ultra),
            "low and medium quality presets disable transparent leaves");
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
        require(assets.readBinary("empty.bin").empty(),
                "empty assets remain readable without exposing backend IO streams");
        bool missing=false;try{assets.readBinary("missing.bin");}catch(const std::exception&){missing=true;}
        require(missing,"missing title assets report failure");}
    ClientSettings settings;
    settings.mouseSensitivity=.42f;settings.guiScale=3;settings.frameRateLimit=137;
    settings.invertMouseY=true;
    settings.renderDistance=8;settings.renderClouds=false;
    settings.lodEnabled=false;settings.lodDistanceChunks=640;
    settings.lodAggressiveness=LodAggressiveness::Fast;
    settings.lodPrecision=LodPrecision::High;
    settings.cloudRenderDistance=1024;settings.smoothLighting=false;
    settings.shadowQuality=ShadowQuality::High;
    settings.visualQuality=VisualQuality::Ultra;
    settings.transparentLeaves=true;
    settings.attackIndicator=AttackIndicator::Hotbar;
    settings.language=Language::SimplifiedChinese;
    settings.bindings[static_cast<size_t>(InputAction::Inventory)]={InputDevice::Mouse,3};
    settings.controlMode=ControlMode::Touch;settings.touchSensitivity=1.75f;
    settings.gamepadDeadzone=.22f;settings.gamepadLookSensitivity=1.5f;
    settings.invertGamepadY=true;settings.gamepadRumble=.6f;
    settings.touchControlSize=1.25f;settings.touchControlOpacity=.8f;settings.touchLeftHanded=true;
    require(settings.save(root/"options.txt"),"settings save succeeds");
    const auto loaded=ClientSettings::load(root/"options.txt");
    require(loaded.mouseSensitivity==.42f&&loaded.guiScale==3&&
            loaded.frameRateLimit==137&&loaded.invertMouseY&&
            loaded.renderDistance==8&&!loaded.renderClouds&&
            !loaded.lodEnabled&&loaded.lodDistanceChunks==640&&
            loaded.lodAggressiveness==LodAggressiveness::Fast&&
            loaded.lodPrecision==LodPrecision::High&&
            loaded.cloudRenderDistance==1024&&!loaded.smoothLighting,
            "client settings round trip");
    ClientSettings frameRateRange;frameRateRange.frameRateLimit=12;frameRateRange.validate();
    require(frameRateRange.frameRateLimit==ClientSettings::MIN_FRAME_RATE,
            "frame-rate validation clamps the lower bound");
    frameRateRange.frameRateLimit=500;frameRateRange.validate();
    require(frameRateRange.frameRateLimit==ClientSettings::MAX_FRAME_RATE,
            "frame-rate validation clamps the upper bound");
    ClientSettings lodRange;lodRange.lodDistanceChunks=1;lodRange.validate();
    require(lodRange.lodDistanceChunks==ClientSettings::MIN_LOD_DISTANCE,
            "LOD distance validation clamps the lower bound");
    lodRange.lodDistanceChunks=99999;lodRange.validate();
    require(lodRange.lodDistanceChunks==ClientSettings::MAX_LOD_DISTANCE,
            "LOD distance validation clamps the hard upper bound");
    require(parseLodDistance("32")==32&&parseLodDistance("4096")==4096&&
            !parseLodDistance("31")&&!parseLodDistance("4097")&&
            !parseLodDistance("12x")&&!parseLodDistance("+32")&&
            !parseLodDistance(" 32")&&lodDistanceNeedsWarning(513)&&
            !lodDistanceNeedsWarning(512),
            "LOD numeric input validates its range and warning threshold");
    require(loaded.shadowQuality==ShadowQuality::High,
            "shadow quality preference round trips");
    require(loaded.visualQuality==VisualQuality::Ultra,
            "visual quality preference round trips");
    require(loaded.transparentLeaves,
            "transparent-leaf preference round trips");
    require(loaded.attackIndicator==AttackIndicator::Hotbar,
            "attack indicator preference round trips");
    require(loaded.controlMode==ControlMode::Touch&&loaded.touchSensitivity==1.75f&&
            loaded.touchControlSize==1.25f&&loaded.touchControlOpacity==.8f&&loaded.touchLeftHanded,
            "touch settings round trip");
    ClientSettings touchRange;touchRange.touchSensitivity=4.0f;touchRange.validate();
    require(touchRange.touchSensitivity==3.0f,"touch sensitivity accepts a 3.0 maximum");
    touchRange.touchSensitivity=0.0f;touchRange.validate();
    require(touchRange.touchSensitivity==.5f,"touch sensitivity retains its 0.5 minimum");
    require(loaded.gamepadDeadzone==.22f&&loaded.gamepadLookSensitivity==1.5f&&
            loaded.invertGamepadY&&loaded.gamepadRumble==.6f,
            "gamepad settings round trip");
    require(loaded.language==Language::SimplifiedChinese,
            "client language round trips");
    ClientSettings russian;russian.language=Language::Russian;
    require(russian.save(root/"russian-options.txt"),"russian settings save succeeds");
    require(ClientSettings::load(root/"russian-options.txt").language==Language::Russian,
            "new language codes round trip through settings");
    {
        std::ofstream legacy(root/"legacy-options.txt");
        legacy<<"version=18\nrenderer=opengl\nrender_distance=8\n";
    }
    const ClientSettings legacySettings=ClientSettings::load(root/"legacy-options.txt");
    require(legacySettings.language==Language::English&&legacySettings.lodEnabled&&
            legacySettings.lodDistanceChunks==128&&
            legacySettings.lodAggressiveness==LodAggressiveness::Balanced&&
            legacySettings.lodPrecision==LodPrecision::Medium,
            "v18 settings migrate to the LOD defaults");
    require(legacySettings.renderDistance==8,
            "legacy renderer setting did not prevent other settings from loading");
    require(!legacySettings.transparentLeaves,
            "legacy medium-quality settings migrate to opaque leaves");
    require(legacySettings.save(root/"migrated-options.txt"),
            "legacy settings migration save succeeds");
    std::ifstream migratedInput(root/"migrated-options.txt");
    const std::string migratedText(
        (std::istreambuf_iterator<char>(migratedInput)), {});
    migratedInput.close();
    require(migratedText.find("version=21\n")!=std::string::npos&&
            migratedText.find("renderer=")==std::string::npos,
            "legacy renderer setting was not removed during migration");
    {
        std::ofstream previous(root/"v20-options.txt");
        previous<<"version=20\nvisual_quality=2\n";
    }
    require(ClientSettings::load(root/"v20-options.txt").transparentLeaves,
            "v20 high-quality settings migrate to transparent leaves");
    {
        std::ofstream invalid(root/"invalid-options.txt");
        invalid<<"version=4\nlanguage=unsupported\n";
    }
    require(ClientSettings::load(root/"invalid-options.txt").language==Language::English,
            "unsupported language falls back to English");
    {
        std::ofstream invalidAttack(root/"invalid-attack-options.txt");
        invalidAttack<<"version=17\nattack_indicator=99\n";
    }
    require(ClientSettings::load(root/"invalid-attack-options.txt").attackIndicator==
                AttackIndicator::Crosshair,
            "invalid attack indicator falls back to crosshair");
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
    {std::ofstream v7(root/"v7-touch.txt");v7<<"version=7\ntouch_sensitivity=1\n";}
    require(ClientSettings::load(root/"v7-touch.txt").touchSensitivity==1.5f,
            "v7 default touch sensitivity migrates to the faster v8 default");
    require(loaded.bindings[static_cast<size_t>(InputAction::Inventory)]==InputBinding{InputDevice::Mouse,3},
            "mouse binding round trips");
    require(ClientSettings{}.bindings[static_cast<size_t>(InputAction::Perspective)]==
                InputBinding{InputDevice::Keyboard,Key::F5},
            "perspective action defaults to F5");
    require(ClientSettings{}.bindings[static_cast<size_t>(InputAction::DropItem)]==
                InputBinding{InputDevice::Keyboard,Key::Q},
            "drop-item action defaults to Q");
    require(ClientSettings{}.bindings[static_cast<size_t>(InputAction::DirectCommand)]==
                InputBinding{InputDevice::Keyboard,Key::Slash}&&
            ClientSettings{}.bindings[static_cast<size_t>(InputAction::PickBlock)]==
                InputBinding{InputDevice::Mouse,MouseButton::Middle}&&
            ClientSettings{}.bindings[static_cast<size_t>(InputAction::SwapOffhand)]==
                InputBinding{InputDevice::Keyboard,Key::F}&&
            ClientSettings{}.bindings[static_cast<size_t>(InputAction::Fullscreen)]==
                InputBinding{InputDevice::Keyboard,Key::F11},
            "Java command, pick-block, offhand and fullscreen defaults are installed");
    require(effectiveGuiScale(1920,1080,0)==2&&effectiveGuiScale(800,450,0)==1&&
            effectiveGuiScale(1920,1080,4)==4,
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
    FakeClipboard clipboard;
    TextEditBuffer clipboardEdit("copy", 16, &clipboard);
    clipboardEdit.selectAll();
    require(clipboardEdit.copySelection() && clipboard.text == "copy",
            "text editing writes through the injected clipboard service");
    clipboard.text = "paste";
    clipboardEdit.setText({});
    require(clipboardEdit.pasteClipboard() && clipboardEdit.text() == "paste",
            "text editing reads through the injected clipboard service");
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
    require(touch.consumeLookDelta()==glm::vec2(30,15),
            "look touch uses the faster default sensitivity");
    commands=touch.onTouch({{0,5},TouchPhase::Begin,430,560});
    require(commands.size()==1&&commands[0].command==TouchCommand::OpenCommand,
            "top-center touch button opens command input");
    commands=touch.onTouch({{0,6},TouchPhase::Begin,500,560});
    require(commands.size()==1&&commands[0].command==TouchCommand::ChangePerspective,
            "top touch view button changes perspective");
    require(touchInventoryCloseRect(1000,600).contains(960,560)&&
            !touchInventoryCloseRect(1000,600).contains(900,500),
            "inventory close button stays in the top-right touch-safe area");
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
    ItemStack creativeSlot{};
    InventoryInteraction::setCreativeItem(creativeSlot,ItemId::COAL);
    require(creativeSlot.id==ItemId::COAL&&creativeSlot.count==64&&creativeSlot.damage==0,
            "creative catalog supplies a full material stack to the real hotbar slot");
    InventoryInteraction::setCreativeItem(creativeSlot,ItemId::DIAMOND_PICKAXE);
    require(creativeSlot.id==ItemId::DIAMOND_PICKAXE&&creativeSlot.count==1,
            "creative catalog respects non-stackable item limits");
    ItemStack droppedFrom{ItemId::COAL,2,7};
    const ItemStack dropped=InventoryInteraction::takeOne(droppedFrom);
    require(dropped.id==ItemId::COAL&&dropped.count==1&&dropped.damage==7&&
            droppedFrom.id==ItemId::COAL&&droppedFrom.count==1,
            "dropping takes one item and preserves stack metadata");
    InventoryInteraction::takeOne(droppedFrom);
    require(droppedFrom.empty(),"dropping the final item clears the selected slot");
    std::filesystem::remove_all(root);
    std::cout<<"Client input and inventory interaction tests passed\n";
}
