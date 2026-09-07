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

VisualQuality defaultVisualQuality(DesktopPlatform platform) {
    if (platform == DesktopPlatform::Android || platform == DesktopPlatform::IOS)
        return VisualQuality::Low;
    return VisualQuality::Medium;
}

bool defaultLeafTransparency(VisualQuality quality) {
    return quality == VisualQuality::High || quality == VisualQuality::VeryHigh ||
           quality == VisualQuality::Ultra;
}

ClientSettings::ClientSettings() {
    const DesktopPlatform platform = currentDesktopPlatform();
    const bool mobile = platform == DesktopPlatform::Android ||
                        platform == DesktopPlatform::IOS;
    applyGraphicsPreset(mobile ? GraphicsPreset::Low : GraphicsPreset::Medium);
    if (mobile) {
        renderDistance = 6;
        lodAggressiveness = LodAggressiveness::PowerSaver;
        lodPrecision = LodPrecision::Low;
        cloudRenderDistance = 96;
        frameRateLimit = 60;
    }
    resetBindings();
    resetGamepadBindings();
}

void ClientSettings::applyGraphicsPreset(GraphicsPreset preset) {
    if (preset == GraphicsPreset::Custom) {
        markGraphicsCustom();
        return;
    }
    graphicsPreset = preset;
    visualQuality = visualQualityForPreset(preset);
    smoothLighting = preset != GraphicsPreset::Low;
    renderClouds = preset != GraphicsPreset::Low;
    transparentLeaves = preset == GraphicsPreset::High ||
        preset == GraphicsPreset::VeryHigh || preset == GraphicsPreset::Ultra;
    switch (preset) {
        case GraphicsPreset::Low: shadowQuality = ShadowQuality::Off; break;
        case GraphicsPreset::Medium: shadowQuality = ShadowQuality::Low; break;
        case GraphicsPreset::High: shadowQuality = ShadowQuality::Medium; break;
        case GraphicsPreset::VeryHigh:
        case GraphicsPreset::Ultra: shadowQuality = ShadowQuality::High; break;
        case GraphicsPreset::Custom: break;
    }
    enhancedVisuals = preset == GraphicsPreset::High ||
        preset == GraphicsPreset::VeryHigh || preset == GraphicsPreset::Ultra;
    enhancedVisual.enabled = enhancedVisuals;
    enhancedVisual.custom = false;
    enhancedVisual.bloom = true;
    enhancedVisual.ambientOcclusion = true;
    enhancedVisual.lightShafts = true;
    enhancedVisual.reflections = true;
    enhancedVisual.atmosphere = true;
    enhancedVisual.materialMotion = true;
    enhancedVisual.ambientParticles = true;
    enhancedVisual.bloomStrength = 100;
    enhancedVisual.ambientOcclusionStrength = 100;
    enhancedVisual.lightShaftStrength = 100;
    enhancedVisual.reflectionStrength = 100;
    enhancedVisual.atmosphereStrength = 100;
    enhancedVisual.materialMotionStrength = 100;
    enhancedVisual.ambientParticleStrength = 100;
    enhancedVisual.gi.enabled = preset == GraphicsPreset::VeryHigh ||
                                preset == GraphicsPreset::Ultra;
    enhancedVisual.gi.strength = 100;
    switch (preset) {
        case GraphicsPreset::Low:
            enhancedVisual.gi.distance = 32;
            enhancedVisual.gi.temporalStability = 50;
            break;
        case GraphicsPreset::Medium:
            enhancedVisual.gi.distance = 64;
            enhancedVisual.gi.temporalStability = 75;
            break;
        case GraphicsPreset::High:
        case GraphicsPreset::VeryHigh:
            enhancedVisual.gi.distance = 128;
            enhancedVisual.gi.temporalStability = 75;
            break;
        case GraphicsPreset::Ultra:
            enhancedVisual.gi.distance = 256;
            enhancedVisual.gi.temporalStability = 100;
            break;
        case GraphicsPreset::Custom: break;
    }
}

void ClientSettings::markGraphicsCustom() {
    graphicsPreset = GraphicsPreset::Custom;
    enhancedVisual.custom = true;
}

GraphicsPreset ClientSettings::nextGraphicsPreset() const {
    return presetForVisualQuality(nextVisualQuality(visualQuality));
}

void ClientSettings::resetBindings() {
    bindings = {key(Key::W), key(Key::S), key(Key::A), key(Key::D),
        key(Key::Space), key(Key::LeftShift), key(Key::LeftControl),
        key(Key::E), key(Key::T), mouse(MouseButton::Left),
        mouse(MouseButton::Right), key(Key::Num1), key(Key::Num2),
        key(Key::Num3), key(Key::Num4), key(Key::Num5), key(Key::Num6),
        key(Key::Num7), key(Key::Num8), key(Key::Num9), wheel(1), wheel(-1),
        key(Key::F5), key(Key::Q), key(Key::Slash), mouse(MouseButton::Middle),
        key(Key::F), key(Key::F11)};
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
    gamepadBindings[static_cast<size_t>(InputAction::Perspective)] = button(12); // D-pad down
}

void ClientSettings::validate() {
    enhancedVisual.enabled = enhancedVisuals;
    auto clampPercent = [](uint8_t& value) {
        value = static_cast<uint8_t>(std::min<int>(value, 100));
    };
    clampPercent(enhancedVisual.bloomStrength);
    clampPercent(enhancedVisual.ambientOcclusionStrength);
    clampPercent(enhancedVisual.lightShaftStrength);
    clampPercent(enhancedVisual.reflectionStrength);
    clampPercent(enhancedVisual.atmosphereStrength);
    clampPercent(enhancedVisual.materialMotionStrength);
    clampPercent(enhancedVisual.ambientParticleStrength);
    clampPercent(enhancedVisual.gi.strength);
    clampPercent(enhancedVisual.gi.temporalStability);
    clampPercent(masterVolume);
    clampPercent(musicVolume);
    clampPercent(weatherVolume);
    clampPercent(soundEffectsVolume);
    constexpr uint16_t giDistances[] = {32, 64, 128, 256};
    if (std::find(std::begin(giDistances), std::end(giDistances),
                  enhancedVisual.gi.distance) == std::end(giDistances))
        enhancedVisual.gi.distance = 64;
    constexpr int distances[] = {2,4,6,8,10,12,16};
    if (std::find(std::begin(distances), std::end(distances), renderDistance) == std::end(distances))
        renderDistance = 8;
    lodDistanceChunks = std::clamp(
        lodDistanceChunks, MIN_LOD_DISTANCE, MAX_LOD_DISTANCE);
    if (static_cast<int>(lodAggressiveness) <
            static_cast<int>(LodAggressiveness::PowerSaver) ||
        static_cast<int>(lodAggressiveness) >
            static_cast<int>(LodAggressiveness::Extreme))
        lodAggressiveness = LodAggressiveness::Balanced;
    if (static_cast<int>(lodPrecision) < static_cast<int>(LodPrecision::Low) ||
        static_cast<int>(lodPrecision) > static_cast<int>(LodPrecision::Ultra))
        lodPrecision = LodPrecision::Medium;
    constexpr int cloudDistances[] = {64,96,128,192,256,512,1024};
    if (std::find(std::begin(cloudDistances), std::end(cloudDistances),
                  cloudRenderDistance) == std::end(cloudDistances))
        cloudRenderDistance = 192;
    constexpr int cycles[] = {0,10,20,40};
    if (std::find(std::begin(cycles), std::end(cycles), dayCycleMinutes) == std::end(cycles))
        dayCycleMinutes = 20;
    mouseSensitivity = std::clamp(mouseSensitivity, 0.05f, 0.50f);
    if (static_cast<int>(shadowQuality) < static_cast<int>(ShadowQuality::Off) ||
        static_cast<int>(shadowQuality) > static_cast<int>(ShadowQuality::High))
        shadowQuality = ShadowQuality::Medium;
    if (visualQuality != VisualQuality::Low &&
         visualQuality != VisualQuality::Medium &&
         visualQuality != VisualQuality::High &&
         visualQuality != VisualQuality::VeryHigh &&
         visualQuality != VisualQuality::Ultra)
        visualQuality = defaultVisualQuality(currentDesktopPlatform());
    if (graphicsPreset != GraphicsPreset::Low &&
        graphicsPreset != GraphicsPreset::Medium &&
        graphicsPreset != GraphicsPreset::High &&
        graphicsPreset != GraphicsPreset::VeryHigh &&
        graphicsPreset != GraphicsPreset::Ultra &&
        graphicsPreset != GraphicsPreset::Custom)
        graphicsPreset = GraphicsPreset::Custom;
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
    frameRateLimit = std::clamp(frameRateLimit, MIN_FRAME_RATE, MAX_FRAME_RATE);
    if (attackIndicator != AttackIndicator::Crosshair &&
        attackIndicator != AttackIndicator::Hotbar &&
        attackIndicator != AttackIndicator::Off)
        attackIndicator = AttackIndicator::Crosshair;
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
            else if (name == "lod_enabled") settings.lodEnabled = std::stoi(value) != 0;
            else if (name == "lod_distance") settings.lodDistanceChunks = std::stoi(value);
            else if (name == "lod_aggressiveness")
                settings.lodAggressiveness = static_cast<LodAggressiveness>(std::stoi(value));
            else if (name == "lod_precision")
                settings.lodPrecision = static_cast<LodPrecision>(std::stoi(value));
            else if (name == "render_clouds")
                settings.renderClouds = std::stoi(value) != 0;
            else if (name == "cloud_render_distance")
                settings.cloudRenderDistance = std::stoi(value);
            else if (name == "day_cycle") settings.dayCycleMinutes = std::stoi(value);
            else if (name == "auto_jump") settings.autoJump = std::stoi(value) != 0;
            else if (name == "toggle_sneak") settings.toggleSneak = std::stoi(value) != 0;
            else if (name == "master_volume")
                settings.masterVolume = static_cast<uint8_t>(std::clamp(std::stoi(value), 0, 100));
            else if (name == "music_volume")
                settings.musicVolume = static_cast<uint8_t>(std::clamp(std::stoi(value), 0, 100));
            else if (name == "weather_volume")
                settings.weatherVolume = static_cast<uint8_t>(std::clamp(std::stoi(value), 0, 100));
            else if (name == "sound_effects_volume")
                settings.soundEffectsVolume = static_cast<uint8_t>(std::clamp(std::stoi(value), 0, 100));
            else if (name == "mouse_sensitivity") settings.mouseSensitivity = std::stof(value);
            else if (name == "invert_mouse_y") settings.invertMouseY = std::stoi(value) != 0;
            else if (name == "raw_mouse_input") { /* v5 compatibility */ }
            else if (name == "smooth_lighting") settings.smoothLighting = std::stoi(value) != 0;
            else if (name == "shadow_quality")
                settings.shadowQuality = static_cast<ShadowQuality>(std::stoi(value));
            else if (name == "visual_quality")
                settings.visualQuality = static_cast<VisualQuality>(std::stoi(value));
            else if (name == "graphics_preset")
                settings.graphicsPreset = static_cast<GraphicsPreset>(std::stoi(value));
            else if (name == "enhanced_visuals") {
                const int parsed = std::stoi(value);
                if (parsed == 0 || parsed == 1)
                    settings.enhancedVisuals = parsed == 1;
            }
            else if (name == "enhanced_custom") settings.enhancedVisual.custom = std::stoi(value) != 0;
            else if (name == "enhanced_bloom") settings.enhancedVisual.bloom = std::stoi(value) != 0;
            else if (name == "enhanced_ao") settings.enhancedVisual.ambientOcclusion = std::stoi(value) != 0;
            else if (name == "enhanced_shafts") settings.enhancedVisual.lightShafts = std::stoi(value) != 0;
            else if (name == "enhanced_reflections") settings.enhancedVisual.reflections = std::stoi(value) != 0;
            else if (name == "enhanced_atmosphere") settings.enhancedVisual.atmosphere = std::stoi(value) != 0;
            else if (name == "enhanced_material_motion") settings.enhancedVisual.materialMotion = std::stoi(value) != 0;
            else if (name == "enhanced_particles") settings.enhancedVisual.ambientParticles = std::stoi(value) != 0;
            else if (name == "enhanced_bloom_strength") settings.enhancedVisual.bloomStrength = static_cast<uint8_t>(std::clamp(std::stoi(value), 0, 100));
            else if (name == "enhanced_ao_strength") settings.enhancedVisual.ambientOcclusionStrength = static_cast<uint8_t>(std::clamp(std::stoi(value), 0, 100));
            else if (name == "enhanced_shaft_strength") settings.enhancedVisual.lightShaftStrength = static_cast<uint8_t>(std::clamp(std::stoi(value), 0, 100));
            else if (name == "enhanced_reflection_strength") settings.enhancedVisual.reflectionStrength = static_cast<uint8_t>(std::clamp(std::stoi(value), 0, 100));
            else if (name == "enhanced_atmosphere_strength") settings.enhancedVisual.atmosphereStrength = static_cast<uint8_t>(std::clamp(std::stoi(value), 0, 100));
            else if (name == "enhanced_material_strength") settings.enhancedVisual.materialMotionStrength = static_cast<uint8_t>(std::clamp(std::stoi(value), 0, 100));
            else if (name == "enhanced_particle_strength") settings.enhancedVisual.ambientParticleStrength = static_cast<uint8_t>(std::clamp(std::stoi(value), 0, 100));
            else if (name == "gi_enabled") settings.enhancedVisual.gi.enabled = std::stoi(value) != 0;
            else if (name == "gi_strength") settings.enhancedVisual.gi.strength = static_cast<uint8_t>(std::clamp(std::stoi(value), 0, 100));
            else if (name == "gi_distance") settings.enhancedVisual.gi.distance = static_cast<uint16_t>(std::stoi(value));
            else if (name == "gi_temporal_stability") settings.enhancedVisual.gi.temporalStability = static_cast<uint8_t>(std::clamp(std::stoi(value), 0, 100));
            else if (name == "transparent_leaves")
                settings.transparentLeaves = std::stoi(value) != 0;
            else if (name == "renderer") { /* v17 compatibility */ }
            else if (name == "gui_scale") settings.guiScale = std::stoi(value);
            else if (name == "frame_rate_limit") settings.frameRateLimit = std::stoi(value);
            else if (name == "attack_indicator")
                settings.attackIndicator = static_cast<AttackIndicator>(std::stoi(value));
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
    if (formatVersion < 21)
        settings.transparentLeaves = defaultLeafTransparency(settings.visualQuality);
    if (formatVersion < 24) {
        settings.graphicsPreset = GraphicsPreset::Custom;
        settings.enhancedVisual.custom = true;
        settings.enhancedVisual.gi.enabled = false;
        switch (settings.visualQuality) {
            case VisualQuality::Low: settings.enhancedVisual.gi.distance = 32; break;
            case VisualQuality::Medium: settings.enhancedVisual.gi.distance = 64; break;
            case VisualQuality::High: settings.enhancedVisual.gi.distance = 128; break;
            case VisualQuality::VeryHigh: settings.enhancedVisual.gi.distance = 128; break;
            case VisualQuality::Ultra: settings.enhancedVisual.gi.distance = 256; break;
        }
    }
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
           << "lod_enabled=" << lodEnabled << '\n'
           << "lod_distance=" << lodDistanceChunks << '\n'
           << "lod_aggressiveness=" << static_cast<int>(lodAggressiveness) << '\n'
           << "lod_precision=" << static_cast<int>(lodPrecision) << '\n'
           << "render_clouds=" << renderClouds << '\n'
           << "cloud_render_distance=" << cloudRenderDistance << '\n'
           << "day_cycle=" << dayCycleMinutes << '\n'
           << "auto_jump=" << autoJump << '\n'
           << "toggle_sneak=" << toggleSneak << '\n'
           << "master_volume=" << static_cast<int>(masterVolume) << '\n'
           << "music_volume=" << static_cast<int>(musicVolume) << '\n'
           << "weather_volume=" << static_cast<int>(weatherVolume) << '\n'
           << "sound_effects_volume=" << static_cast<int>(soundEffectsVolume) << '\n'
           << "mouse_sensitivity=" << mouseSensitivity << '\n'
           << "invert_mouse_y=" << invertMouseY << '\n'
           << "smooth_lighting=" << smoothLighting << '\n'
           << "shadow_quality=" << static_cast<int>(shadowQuality) << '\n'
           << "visual_quality=" << static_cast<int>(visualQuality) << '\n'
           << "graphics_preset=" << static_cast<int>(graphicsPreset) << '\n'
           << "enhanced_visuals=" << enhancedVisuals << '\n'
           << "enhanced_custom=" << enhancedVisual.custom << '\n'
           << "enhanced_bloom=" << enhancedVisual.bloom << '\n'
           << "enhanced_ao=" << enhancedVisual.ambientOcclusion << '\n'
           << "enhanced_shafts=" << enhancedVisual.lightShafts << '\n'
           << "enhanced_reflections=" << enhancedVisual.reflections << '\n'
           << "enhanced_atmosphere=" << enhancedVisual.atmosphere << '\n'
           << "enhanced_material_motion=" << enhancedVisual.materialMotion << '\n'
           << "enhanced_particles=" << enhancedVisual.ambientParticles << '\n'
           << "enhanced_bloom_strength=" << static_cast<int>(enhancedVisual.bloomStrength) << '\n'
           << "enhanced_ao_strength=" << static_cast<int>(enhancedVisual.ambientOcclusionStrength) << '\n'
           << "enhanced_shaft_strength=" << static_cast<int>(enhancedVisual.lightShaftStrength) << '\n'
           << "enhanced_reflection_strength=" << static_cast<int>(enhancedVisual.reflectionStrength) << '\n'
           << "enhanced_atmosphere_strength=" << static_cast<int>(enhancedVisual.atmosphereStrength) << '\n'
           << "enhanced_material_strength=" << static_cast<int>(enhancedVisual.materialMotionStrength) << '\n'
           << "enhanced_particle_strength=" << static_cast<int>(enhancedVisual.ambientParticleStrength) << '\n'
           << "gi_enabled=" << enhancedVisual.gi.enabled << '\n'
           << "gi_strength=" << static_cast<int>(enhancedVisual.gi.strength) << '\n'
           << "gi_distance=" << enhancedVisual.gi.distance << '\n'
           << "gi_temporal_stability=" << static_cast<int>(enhancedVisual.gi.temporalStability) << '\n'
           << "transparent_leaves=" << transparentLeaves << '\n'
           << "gui_scale=" << guiScale << '\n'
           << "frame_rate_limit=" << frameRateLimit << '\n'
           << "attack_indicator=" << static_cast<int>(attackIndicator) << '\n'
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
