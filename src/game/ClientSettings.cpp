#include "game/ClientSettings.h"
#include "core/Platform.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace {
InputBinding key(int code) { return {InputDevice::Keyboard, code}; }
InputBinding mouse(int code) { return {InputDevice::Mouse, code}; }
InputBinding wheel(int direction) { return {InputDevice::Wheel, direction}; }
}

ClientSettings::ClientSettings() { resetBindings(); }

void ClientSettings::resetBindings() {
    bindings = {key(GLFW_KEY_W), key(GLFW_KEY_S), key(GLFW_KEY_A), key(GLFW_KEY_D),
        key(GLFW_KEY_SPACE), key(GLFW_KEY_LEFT_SHIFT), key(GLFW_KEY_LEFT_CONTROL),
        key(GLFW_KEY_E), key(GLFW_KEY_T), mouse(GLFW_MOUSE_BUTTON_LEFT),
        mouse(GLFW_MOUSE_BUTTON_RIGHT), key(GLFW_KEY_1), key(GLFW_KEY_2),
        key(GLFW_KEY_3), key(GLFW_KEY_4), key(GLFW_KEY_5), key(GLFW_KEY_6),
        key(GLFW_KEY_7), key(GLFW_KEY_8), key(GLFW_KEY_9), wheel(1), wheel(-1)};
}

void ClientSettings::validate() {
    constexpr int distances[] = {2,4,6,8,10,12,16};
    if (std::find(std::begin(distances), std::end(distances), renderDistance) == std::end(distances))
        renderDistance = 8;
    constexpr int cloudDistances[] = {64,96,128,192,256,512};
    if (std::find(std::begin(cloudDistances), std::end(cloudDistances),
                  cloudRenderDistance) == std::end(cloudDistances))
        cloudRenderDistance = 192;
    constexpr int cycles[] = {0,10,20,40};
    if (std::find(std::begin(cycles), std::end(cycles), dayCycleMinutes) == std::end(cycles))
        dayCycleMinutes = 20;
    mouseSensitivity = std::clamp(mouseSensitivity, 0.05f, 0.50f);
    if (guiScale < 0 || guiScale > 4) guiScale = 0;
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
}

ClientSettings ClientSettings::load(const std::filesystem::path& path) {
    ClientSettings settings;
    std::ifstream input(path);
    if (!input) return settings;
    std::string line;
    while (std::getline(input, line)) {
        const size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string name = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        try {
            if (name == "render_distance") settings.renderDistance = std::stoi(value);
            else if (name == "render_clouds")
                settings.renderClouds = std::stoi(value) != 0;
            else if (name == "cloud_render_distance")
                settings.cloudRenderDistance = std::stoi(value);
            else if (name == "day_cycle") settings.dayCycleMinutes = std::stoi(value);
            else if (name == "auto_jump") settings.autoJump = std::stoi(value) != 0;
            else if (name == "mouse_sensitivity") settings.mouseSensitivity = std::stof(value);
            else if (name == "invert_mouse_y") settings.invertMouseY = std::stoi(value) != 0;
            else if (name == "raw_mouse_input") settings.rawMouseInput = std::stoi(value) != 0;
            else if (name == "smooth_lighting") settings.smoothLighting = std::stoi(value) != 0;
            else if (name == "gui_scale") settings.guiScale = std::stoi(value);
            else if (name.rfind("binding.", 0) == 0) {
                const size_t index = static_cast<size_t>(std::stoul(name.substr(8)));
                if (index >= settings.bindings.size()) continue;
                std::istringstream stream(value);
                int device = 0, code = 0;
                char comma = 0;
                if (stream >> device >> comma >> code && comma == ',')
                    settings.bindings[index] = {static_cast<InputDevice>(device), code};
            }
        } catch (const std::exception&) {}
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
           << "render_clouds=" << renderClouds << '\n'
           << "cloud_render_distance=" << cloudRenderDistance << '\n'
           << "day_cycle=" << dayCycleMinutes << '\n'
           << "auto_jump=" << autoJump << '\n'
           << "mouse_sensitivity=" << mouseSensitivity << '\n'
           << "invert_mouse_y=" << invertMouseY << '\n'
           << "raw_mouse_input=" << rawMouseInput << '\n'
           << "smooth_lighting=" << smoothLighting << '\n'
           << "gui_scale=" << guiScale << '\n';
    for (size_t i = 0; i < bindings.size(); ++i)
        output << "binding." << i << '=' << static_cast<int>(bindings[i].device)
               << ',' << bindings[i].code << '\n';
    output.close();
    if (!output) return false;
    if (Platform::replaceFileAtomically(temporary, path, error)) return true;
    std::filesystem::remove(temporary, error);
    return false;
}

int effectiveGuiScale(int width, int height, int configuredScale) {
    if (configuredScale >= 1 && configuredScale <= 4) return configuredScale;
    int scale = 1;
    while (scale < 4 && width / (scale + 1) >= 640 && height / (scale + 1) >= 360)
        ++scale;
    return scale;
}
