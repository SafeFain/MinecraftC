#include "game/Language.h"

#include <array>

std::string_view languageEnglishName(Language language) {
    switch (language) {
        case Language::Arabic: return "Arabic";
        case Language::English: return "English";
        case Language::French: return "French";
        case Language::German: return "German";
        case Language::Japanese: return "Japanese";
        case Language::Korean: return "Korean";
        case Language::Portuguese: return "Portuguese";
        case Language::Russian: return "Russian";
        case Language::SimplifiedChinese: return "Simplified Chinese";
        case Language::Spanish: return "Spanish";
        default: return "English";
    }
}

std::string_view languageNativeName(Language language) {
    switch (language) {
        case Language::Arabic: return "العربية";
        case Language::English: return "English";
        case Language::French: return "Français";
        case Language::German: return "Deutsch";
        case Language::Japanese: return "日本語";
        case Language::Korean: return "한국어";
        case Language::Portuguese: return "Português";
        case Language::Russian: return "Русский";
        case Language::SimplifiedChinese: return "简体中文";
        case Language::Spanish: return "Español";
        default: return "English";
    }
}

const std::array<Language, 10>& languagesByEnglishName() {
    static const std::array<Language, 10> order = {{
        Language::Arabic,           // A
        Language::SimplifiedChinese,// C
        Language::English,          // E
        Language::French,           // F
        Language::German,           // G
        Language::Japanese,         // J
        Language::Korean,           // K
        Language::Portuguese,       // P
        Language::Russian,          // R
        Language::Spanish           // S
    }};
    return order;
}

Language nextLanguage(Language language) {
    const auto& order = languagesByEnglishName();
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == language)
            return order[(i + 1) % order.size()];
    }
    return Language::English;
}

Language parseLanguage(std::string_view code) {
    if (code == "ar_sa") return Language::Arabic;
    if (code == "zh_cn") return Language::SimplifiedChinese;
    if (code == "fr_fr") return Language::French;
    if (code == "de_de") return Language::German;
    if (code == "ja_jp") return Language::Japanese;
    if (code == "ko_kr") return Language::Korean;
    if (code == "pt_br") return Language::Portuguese;
    if (code == "ru_ru") return Language::Russian;
    if (code == "es_es") return Language::Spanish;
    return Language::English;
}
