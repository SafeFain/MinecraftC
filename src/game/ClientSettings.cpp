#include "game/ClientSettings.h"
#include "core/Platform.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>
#include <cmath>

namespace {
InputBinding key(int code) { return {InputDevice::Keyboard, code}; }
InputBinding mouse(int code) { return {InputDevice::Mouse, code}; }
InputBinding wheel(int direction) { return {InputDevice::Wheel, direction}; }
GamepadBinding button(int code) { return {GamepadBindingType::Button, code}; }
GamepadBinding axis(int code, bool positive) {
    return {positive ? GamepadBindingType::AxisPositive : GamepadBindingType::AxisNegative, code};
}
}

RendererBackend defaultRendererBackend(DesktopPlatform platform) {
    return platform == DesktopPlatform::Android
        ? RendererBackend::Vulkan : RendererBackend::OpenGL;
}

RendererBackend migrateRendererBackend(DesktopPlatform platform,
                                        int sourceFormatVersion,
                                        RendererBackend stored) {
    if (platform == DesktopPlatform::Android &&
        sourceFormatVersion < ClientSettings::FORMAT_VERSION)
        return RendererBackend::Vulkan;
    return stored;
}

ClientSettings::ClientSettings() {
    rendererBackend = defaultRendererBackend(currentDesktopPlatform());
    resetBindings();
    resetGamepadBindings();
}

void ClientSettings::resetBindings() {
    bindings = {key(Key::W), key(Key::S), key(Key::A), key(Key::D),
        key(Key::Space), key(Key::LeftShift), key(Key::LeftControl),
        key(Key::E), key(Key::T), mouse(MouseButton::Left),
        mouse(MouseButton::Right), key(Key::Num1), key(Key::Num2),
        key(Key::Num3), key(Key::Num4), key(Key::Num5), key(Key::Num6),
        key(Key::Num7), key(Key::Num8), key(Key::Num9), wheel(1), wheel(-1)};
}

void ClientSettings::resetGamepadBindings() {
    gamepadBindings.fill({});
    gamepadBindings[static_cast<size_t>(InputAction::MoveForward)] = axis(1, false);
    gamepadBindings[static_cast<size_t>(InputAction::MoveBackward)] = axis(1, true);
    gamepadBindings[static_cast<size_t>(InputAction::MoveLeft)] = axis(0, false);
    gamepadBindings[static_cast<size_t>(InputAction::MoveRight)] = axis(0, true);
    gamepadBindings[static_cast<size_t>(InputAction::Jump)] = button(0); // South/A
    gamepadBindings[static_cast<size_t>(InputAction::Sneak)] = button(1); // East/B
    gamepadBindings[static_cast<size_t>(InputAction::Sprint)] = button(7); // L3
    gamepadBindings[static_cast<size_t>(InputAction::Inventory)] = button(3); // North/Y
    gamepadBindings[static_cast<size_t>(InputAction::Command)] = button(11); // D-pad up
    gamepadBindings[static_cast<size_t>(InputAction::Attack)] = axis(5, true); // RT
    gamepadBindings[static_cast<size_t>(InputAction::Use)] = axis(4, true); // LT
    gamepadBindings[static_cast<size_t>(InputAction::PreviousSlot)] = button(9); // LB
    gamepadBindings[static_cast<size_t>(InputAction::NextSlot)] = button(10); // RB
}

void ClientSettings::validate() {
    constexpr int distances[] = {2,4,6,8,10,12,16};
    if (std::find(std::begin(distances), std::end(distances), renderDistance) == std::end(distances))
        renderDistance = 8;
    constexpr int cloudDistances[] = {64,96,128,192,256,512,1024};
    if (std::find(std::begin(cloudDistances), std::end(cloudDistances),
                  cloudRenderDistance) == std::end(cloudDistances))
        cloudRenderDistance = 192;
    constexpr int cycles[] = {0,10,20,40};
    if (std::find(std::begin(cycles), std::end(cycles), dayCycleMinutes) == std::end(cycles))
        dayCycleMinutes = 20;
    mouseSensitivity = std::clamp(mouseSensitivity, 0.05f, 0.50f);
    if (static_cast<int>(controlMode) < static_cast<int>(ControlMode::Auto) ||
        static_cast<int>(controlMode) > static_cast<int>(ControlMode::Touch))
        controlMode = ControlMode::Auto;
    touchSensitivity = std::clamp(touchSensitivity, 0.5f, 3.0f);
    gamepadDeadzone = std::clamp(gamepadDeadzone, 0.05f, 0.50f);
    gamepadLookSensitivity = std::clamp(gamepadLookSensitivity, 0.25f, 3.0f);
    gamepadRumble = std::clamp(gamepadRumble, 0.0f, 1.0f);
    constexpr float sizes[] = {.75f,1.0f,1.25f,1.5f};
    if (std::none_of(std::begin(sizes),std::end(sizes),[this](float v){return std::abs(v-touchControlSize)<.001f;}))
        touchControlSize=1.0f;
    constexpr float opacities[] = {.35f,.5f,.65f,.8f,1.0f};
    if (std::none_of(std::begin(opacities),std::end(opacities),[this](float v){return std::abs(v-touchControlOpacity)<.001f;}))
        touchControlOpacity=.65f;
    if (guiScale < 0 || guiScale > 4) guiScale = 0;
    if (rendererBackend != RendererBackend::OpenGL &&
        rendererBackend != RendererBackend::Vulkan)
        rendererBackend = RendererBackend::OpenGL;
    ClientSettings defaults;
    for (size_t i = 0; i < bindings.size(); ++i) {
        auto& binding = bindings[i];
        const bool valid = binding.device == InputDevice::None ||
            (binding.device == InputDevice::Keyboard && binding.code >= 0 && binding.code < 512) ||
            (binding.device == InputDevice::Mouse && binding.code >= 0 && binding.code < 16) ||
            (binding.device == InputDevice::Wheel && (binding.code == -1 || binding.code == 1));
        if (!valid || (!inputActionCanUnbind(static_cast<InputAction>(i)) &&
                       binding.device == InputDevice::None))
            binding = defaults.bindings[i];
    }
    for (size_t i = 0; i < gamepadBindings.size(); ++i) {
        auto& binding = gamepadBindings[i];
        const bool valid = binding.type == GamepadBindingType::None ||
            (binding.type == GamepadBindingType::Button && binding.code >= 0 && binding.code < 32) ||
            ((binding.type == GamepadBindingType::AxisPositive || binding.type == GamepadBindingType::AxisNegative) &&
             binding.code >= 0 && binding.code < 16);
        if (!valid || (!inputActionCanUnbind(static_cast<InputAction>(i)) &&
                       binding.type == GamepadBindingType::None))
            binding = defaults.gamepadBindings[i];
    }
}

ClientSettings ClientSettings::load(const std::filesystem::path& path) {
    ClientSettings settings;
    std::array<bool, INPUT_ACTION_COUNT> bindingRead{};
    std::array<bool, INPUT_ACTION_COUNT> gamepadBindingRead{};
    std::ifstream input(path);
    if (!input) return settings;
    std::string line;
    int formatVersion = 0;
    while (std::getline(input, line)) {
        const size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string name = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        try {
            if (name == "version") formatVersion = std::stoi(value);
            else if (name == "render_distance") settings.renderDistance = std::stoi(value);
            else if (name == "render_clouds")
                settings.renderClouds = std::stoi(value) != 0;
            else if (name == "cloud_render_distance")
                settings.cloudRenderDistance = std::stoi(value);
            else if (name == "day_cycle") settings.dayCycleMinutes = std::stoi(value);
            else if (name == "auto_jump") settings.autoJump = std::stoi(value) != 0;
            else if (name == "mouse_sensitivity") settings.mouseSensitivity = std::stof(value);
            else if (name == "invert_mouse_y") settings.invertMouseY = std::stoi(value) != 0;
            else if (name == "raw_mouse_input") { /* v5 compatibility */ }
            else if (name == "smooth_lighting") settings.smoothLighting = std::stoi(value) != 0;
            else if (name == "renderer") settings.rendererBackend = value == "vulkan"
                ? RendererBackend::Vulkan : RendererBackend::OpenGL;
            else if (name == "gui_scale") settings.guiScale = std::stoi(value);
            else if (name == "language") settings.language = parseLanguage(value);
            else if (name == "control_mode") settings.controlMode = static_cast<ControlMode>(std::stoi(value));
            else if (name == "touch_sensitivity") settings.touchSensitivity = std::stof(value);
            else if (name == "touch_size") settings.touchControlSize = std::stof(value);
            else if (name == "touch_opacity") settings.touchControlOpacity = std::stof(value);
            else if (name == "touch_left_handed") settings.touchLeftHanded = std::stoi(value) != 0;
            else if (name == "gamepad_deadzone") settings.gamepadDeadzone = std::stof(value);
            else if (name == "gamepad_look_sensitivity") settings.gamepadLookSensitivity = std::stof(value);
            else if (name == "invert_gamepad_y") settings.invertGamepadY = std::stoi(value) != 0;
            else if (name == "gamepad_rumble") settings.gamepadRumble = std::stof(value);
            else if (name.rfind("gamepad_binding.", 0) == 0) {
                const size_t index = static_cast<size_t>(std::stoul(name.substr(16)));
                if (index >= settings.gamepadBindings.size()) continue;
                std::istringstream stream(value);
                int type = 0, code = 0; char comma = 0;
                if (stream >> type >> comma >> code && comma == ',') {
                    settings.gamepadBindings[index] = {static_cast<GamepadBindingType>(type), code};
                    gamepadBindingRead[index] = true;
                }
            }
            else if (name.rfind("binding.", 0) == 0) {
                const size_t index = static_cast<size_t>(std::stoul(name.substr(8)));
                if (index >= settings.bindings.size()) continue;
                std::istringstream stream(value);
                int device = 0, code = 0;
                char comma = 0;
                if (stream >> device >> comma >> code && comma == ',') {
                    settings.bindings[index] = {static_cast<InputDevice>(device), code};
                    bindingRead[index] = true;
                }
            }
        } catch (const std::exception&) {}
    }
    ClientSettings defaults;
    if (formatVersion < 6) {
        for (size_t i = 0; i < settings.bindings.size(); ++i) {
            auto& binding = settings.bindings[i];
            if (!bindingRead[i] || binding.device != InputDevice::Keyboard) continue;
            const int migrated = migrateLegacyGlfwKey(binding.code);
            if (migrated != Key::Unknown) binding.code = migrated;
            else binding = inputActionCanUnbind(static_cast<InputAction>(i))
                ? InputBinding{} : defaults.bindings[i];
        }
    }
    if(formatVersion<FORMAT_VERSION)for(size_t i=0;i<settings.gamepadBindings.size();++i)
        if(!gamepadBindingRead[i])settings.gamepadBindings[i]=defaults.gamepadBindings[i];
    if (formatVersion < 8 && std::abs(settings.touchSensitivity - 1.0f) < .001f)
        settings.touchSensitivity = defaults.touchSensitivity;
    settings.rendererBackend = migrateRendererBackend(
        currentDesktopPlatform(), formatVersion, settings.rendererBackend);
    settings.validate();
    return settings;
}

bool ClientSettings::save(const std::filesystem::path& path) const {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) return false;
    output << "version=" << FORMAT_VERSION << '\n'
           << "render_distance=" << renderDistance << '\n'
           << "render_clouds=" << renderClouds << '\n'
           << "cloud_render_distance=" << cloudRenderDistance << '\n'
           << "day_cycle=" << dayCycleMinutes << '\n'
           << "auto_jump=" << autoJump << '\n'
           << "mouse_sensitivity=" << mouseSensitivity << '\n'
           << "invert_mouse_y=" << invertMouseY << '\n'
           << "smooth_lighting=" << smoothLighting << '\n'
           << "renderer=" << (rendererBackend == RendererBackend::Vulkan
                ? "vulkan" : "opengl") << '\n'
           << "gui_scale=" << guiScale << '\n'
           << "language=" << languageCode(language) << '\n';
    output << "control_mode=" << static_cast<int>(controlMode) << '\n'
           << "touch_sensitivity=" << touchSensitivity << '\n'
           << "touch_size=" << touchControlSize << '\n'
           << "touch_opacity=" << touchControlOpacity << '\n'
           << "touch_left_handed=" << touchLeftHanded << '\n';
    output << "gamepad_deadzone=" << gamepadDeadzone << '\n'
           << "gamepad_look_sensitivity=" << gamepadLookSensitivity << '\n'
           << "invert_gamepad_y=" << invertGamepadY << '\n'
           << "gamepad_rumble=" << gamepadRumble << '\n';
    for (size_t i = 0; i < bindings.size(); ++i)
        output << "binding." << i << '=' << static_cast<int>(bindings[i].device)
               << ',' << bindings[i].code << '\n';
    for (size_t i = 0; i < gamepadBindings.size(); ++i)
        output << "gamepad_binding." << i << '=' << static_cast<int>(gamepadBindings[i].type)
               << ',' << gamepadBindings[i].code << '\n';
    output.close();
    if (!output) return false;
    if (Platform::replaceFileAtomically(temporary, path, error)) return true;
    std::filesystem::remove(temporary, error);
    return false;
}

int effectiveGuiScale(int width, int height, int configuredScale) {
    if (configuredScale >= 1 && configuredScale <= 4) return configuredScale;
    int scale = 1;
    while (scale < 4 && width / (scale + 1) >= 800 && height / (scale + 1) >= 450)
        ++scale;
    return scale;
}
