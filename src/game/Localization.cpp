#include "game/Localization.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

Localization::Strings Localization::loadFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Failed to open language file: " + path.u8string());
    nlohmann::json root;
    input >> root;
    if (!root.is_object())
        throw std::runtime_error("Language file root is not an object: " + path.u8string());
    Strings strings;
    for (const auto& entry : root.items()) {
        if (entry.value().is_string())
            strings.emplace(entry.key(), entry.value().get<std::string>());
    }
    return strings;
}

void Localization::load(const std::filesystem::path& assetRoot) {
    m_english = loadFile(assetRoot / "lang" / "en_us.json");
    m_chinese = loadFile(assetRoot / "lang" / "zh_cn.json");
}

const Localization::Strings& Localization::active() const {
    return m_language == Language::SimplifiedChinese ? m_chinese : m_english;
}

std::string Localization::text(std::string_view key) const {
    const std::string owned(key);
    const auto& selected = active();
    if (const auto found = selected.find(owned); found != selected.end()) return found->second;
    if (const auto found = m_english.find(owned); found != m_english.end()) return found->second;
    return "[" + owned + "]";
}

std::string Localization::format(
    std::string_view key, std::initializer_list<std::string> arguments) const {
    std::string result = text(key);
    size_t index = 0;
    for (const auto& argument : arguments) {
        const std::string marker = "{" + std::to_string(index++) + "}";
        size_t position = 0;
        while ((position = result.find(marker, position)) != std::string::npos) {
            result.replace(position, marker.size(), argument);
            position += argument.size();
        }
    }
    return result;
}

std::string Localization::itemName(ItemId item) const {
    const std::string key = "item." + std::to_string(static_cast<uint16_t>(item));
    const auto& selected = active();
    if (const auto found = selected.find(key); found != selected.end()) return found->second;
    return getItemProps(item).name;
}

std::string Localization::actionName(InputAction action) const {
    return text("action." + std::to_string(static_cast<size_t>(action)));
}

std::string Localization::bindingName(const InputBinding& binding) const {
    if (binding.device == InputDevice::None) return text("binding.unbound");
    if (binding.device == InputDevice::Mouse)
        return format("binding.mouse", {std::to_string(binding.code + 1)});
    if (binding.device == InputDevice::Wheel)
        return text(binding.code > 0 ? "binding.wheel_up" : "binding.wheel_down");
    const char* name = glfwGetKeyName(binding.code, 0);
    if (name) {
        std::string result(name);
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char value) { return static_cast<char>(std::toupper(value)); });
        return result;
    }
    switch (binding.code) {
        case GLFW_KEY_SPACE: return text("key.space");
        case GLFW_KEY_LEFT_SHIFT: return text("key.left_shift");
        case GLFW_KEY_LEFT_CONTROL: return text("key.left_ctrl");
        case GLFW_KEY_UP: return text("key.up");
        case GLFW_KEY_DOWN: return text("key.down");
        case GLFW_KEY_LEFT: return text("key.left");
        case GLFW_KEY_RIGHT: return text("key.right");
        case GLFW_KEY_ESCAPE: return "Esc";
        case GLFW_KEY_ENTER: return "Enter";
        case GLFW_KEY_TAB: return "Tab";
        default: return format("key.code", {std::to_string(binding.code)});
    }
}

bool Localization::hasTranslation(Language language, std::string_view key) const {
    const auto& strings = language == Language::SimplifiedChinese ? m_chinese : m_english;
    return strings.find(std::string(key)) != strings.end();
}
