#include "game/Localization.h"
#include "core/AssetStore.h"
#include "game/ArabicShaper.h"
#include "plugins/ContentRegistry.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

Localization::Strings Localization::loadFile(const std::filesystem::path& path) {
    std::istringstream input(AssetStore::readTextPath(path));
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
    for(auto& language:m_languages)language.clear();
    for(auto& language:m_pluginOverrides)language.clear();
    m_languages[static_cast<size_t>(Language::English)] =
        loadFile(assetRoot / "lang" / "en_us.json");
    for (size_t index = 0; index < m_languages.size(); ++index) {
        const auto language = static_cast<Language>(index);
        try {
            m_pluginOverrides[index]=loadFile(assetRoot/"lang/overrides"/
                (std::string(languageCode(language))+".json"));
        }catch(const std::exception&){}
        if (language == Language::English) continue;
        try {
            m_languages[index] = loadFile(assetRoot / "lang" /
                (std::string(languageCode(language)) + ".json"));
        } catch (const std::exception&) {
            // A missing translation file leaves the map empty; lookups fall
            // back to English. This keeps partial installs fully usable.
        }
    }
}

const Localization::Strings& Localization::active() const {
    return languageStrings(m_language);
}

const Localization::Strings& Localization::languageStrings(Language language) const {
    return m_languages[static_cast<size_t>(language)];
}

std::string Localization::rawText(std::string_view key) const {
    std::string owned(key);
    const auto& extensions=Plugins::content().extensions;
    if(const auto alias=extensions.translationAliases.find(owned);alias!=extensions.translationAliases.end())owned=alias->second;
    const auto pluginText=[&](Language language)->const std::string* {
        const auto bundle=extensions.translations.find(std::string(languageCode(language)));
        if(bundle==extensions.translations.end())return nullptr;
        const auto found=bundle->second.find(owned);return found==bundle->second.end()?nullptr:&found->second;
    };
    const auto& selected = active();
    const auto* pluginSelected=pluginText(m_language);
    const auto* pluginEnglish=pluginText(Language::English);
    if(pluginSelected||pluginEnglish) {
        // Host language packs can correct provider text without changing a
        // gameplay package fingerprint. Only apply overrides to loaded keys.
        const auto& overrides=m_pluginOverrides[static_cast<size_t>(m_language)];
        if(const auto found=overrides.find(owned);found!=overrides.end())return found->second;
        return pluginSelected?*pluginSelected:*pluginEnglish;
    }
    if (const auto found = selected.find(owned); found != selected.end()) return found->second;
    const auto& english = languageStrings(Language::English);
    if (const auto found = english.find(owned); found != english.end()) return found->second;
    return "[" + owned + "]";
}

std::string Localization::shaped(std::string value) const {
    return m_language == Language::Arabic ? shapeArabic(value) : value;
}

std::string Localization::text(std::string_view key) const {
    return shaped(rawText(key));
}

std::string Localization::format(
    std::string_view key, std::initializer_list<std::string> arguments) const {
    std::string result = rawText(key);
    size_t index = 0;
    for (const auto& value : arguments) {
        const std::string argument=m_language==Language::Arabic?logicalArabic(value):value;
        const std::string marker = "{" + std::to_string(index++) + "}";
        size_t position = 0;
        while ((position = result.find(marker, position)) != std::string::npos) {
            result.replace(position, marker.size(), argument);
            position += argument.size();
        }
    }
    return shaped(std::move(result));
}

std::string Localization::itemName(ItemId item) const {
    if(const auto name=Plugins::content().extensions.nameKeys.find(Plugins::itemKey(item));
       name!=Plugins::content().extensions.nameKeys.end())return text(name->second);
    const std::string key = "item." + std::to_string(static_cast<uint16_t>(item));
    const auto& selected = active();
    if (const auto found = selected.find(key); found != selected.end())
        return shaped(found->second);
    const auto& english=languageStrings(Language::English);
    if(const auto found=english.find(key);found!=english.end())return shaped(found->second);
    return shaped(getItemProps(item).name);
}

std::string Localization::dimensionName(uint32_t dimension) const {
    const auto& d=Plugins::dimensionContent(dimension);
    return d.nameKey.empty()?shaped(d.name):text(d.nameKey);
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
    const char* name = physicalKeyName(binding.code);
    if (name) {
        std::string result(name);
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char value) { return static_cast<char>(std::toupper(value)); });
        return result;
    }
    switch (binding.code) {
        case Key::Space: return text("key.space");
        case Key::LeftShift: return text("key.left_shift");
        case Key::LeftControl: return text("key.left_ctrl");
        case Key::Up: return text("key.up");
        case Key::Down: return text("key.down");
        case Key::Left: return text("key.left");
        case Key::Right: return text("key.right");
        case Key::Escape: return "Esc";
        case Key::Enter: return "Enter";
        case Key::Tab: return "Tab";
        default: return format("key.code", {std::to_string(binding.code)});
    }
}

bool Localization::hasTranslation(Language language, std::string_view key) const {
    std::string resolved(key);const auto& e=Plugins::content().extensions;
    if(const auto alias=e.translationAliases.find(resolved);alias!=e.translationAliases.end())resolved=alias->second;
    if(const auto bundle=e.translations.find(std::string(languageCode(language)));bundle!=e.translations.end())
        if(bundle->second.count(resolved))return true;
    const auto& strings = languageStrings(language);
    return strings.find(resolved) != strings.end();
}
