#pragma once

#include <array>
#include <cstdint>
#include <string_view>

// UI languages. The enum order is the order of the language option: every
// language sorted by the first letter of its English name (Arabic, Chinese,
// English, French, German, Japanese, Korean, Portuguese, Russian, Spanish).
// Values are never serialized; only languageCode() strings reach disk.
enum class Language : uint8_t {
    Arabic,
    SimplifiedChinese,
    English,
    French,
    German,
    Japanese,
    Korean,
    Portuguese,
    Russian,
    Spanish,

    Count
};

constexpr std::string_view languageCode(Language language) {
    switch (language) {
        case Language::Arabic: return "ar_sa";
        case Language::English: return "en_us";
        case Language::French: return "fr_fr";
        case Language::German: return "de_de";
        case Language::Japanese: return "ja_jp";
        case Language::Korean: return "ko_kr";
        case Language::Portuguese: return "pt_br";
        case Language::Russian: return "ru_ru";
        case Language::SimplifiedChinese: return "zh_cn";
        case Language::Spanish: return "es_es";
        default: return "en_us";
    }
}

// Display names: the English name is used for sorting, the native name for
// the language option label so every language is shown in its own script.
std::string_view languageEnglishName(Language language);
std::string_view languageNativeName(Language language);

// All supported languages sorted by the first letter of their English name.
const std::array<Language, 10>& languagesByEnglishName();

// The next language in the sorted order, wrapping around at the end.
Language nextLanguage(Language language);

// Parses a language= code from options.txt; unknown values fall back to
// English, matching the historical zh_cn/en_us behavior.
Language parseLanguage(std::string_view code);
