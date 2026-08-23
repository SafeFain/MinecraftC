#pragma once

#include <array>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>

#include "core/Input.h"
#include "game/Item.h"
#include "game/Language.h"

class Localization {
public:
    void load(const std::filesystem::path& assetRoot);
    void setLanguage(Language language) { m_language = language; }
    Language language() const { return m_language; }

    std::string text(std::string_view key) const;
    std::string format(
        std::string_view key, std::initializer_list<std::string> arguments) const;
    std::string itemName(ItemId item) const;
    std::string actionName(InputAction action) const;
    std::string bindingName(const InputBinding& binding) const;
    bool hasTranslation(Language language, std::string_view key) const;

private:
    using Strings = std::unordered_map<std::string, std::string>;
    std::array<Strings, static_cast<size_t>(Language::Count)> m_languages;
    Language m_language = Language::English;

    const Strings& active() const;
    const Strings& languageStrings(Language language) const;
    std::string rawText(std::string_view key) const;
    std::string shaped(std::string value) const;
    static Strings loadFile(const std::filesystem::path& path);
};
