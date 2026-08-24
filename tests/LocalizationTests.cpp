#include "game/ArabicShaper.h"
#include "game/Localization.h"
#include "game/Utf8.h"
#include "world/Biome.h"

#include <nlohmann/json.hpp>
#include <stb_truetype.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_set>
#include <vector>

#ifndef MINECRAFTC_SOURCE_DIR
#error MINECRAFTC_SOURCE_DIR must be defined
#endif

namespace {
void require(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    const std::filesystem::path root = MINECRAFTC_SOURCE_DIR;
    Localization localization;
    localization.load(root / "assets");
    require(localization.text("menu.home.singleplayer") == "Singleplayer",
            "English is the initial language");
    localization.setLanguage(Language::SimplifiedChinese);
    require(localization.text("menu.home.singleplayer") == "单人游戏",
            "Simplified Chinese strings load");
    require(localization.format("loading.chunks", {"2", "9"}) == "2 / 9 个区块",
            "localized positional formatting");
    require(localization.hasTranslation(Language::English, "menu.create.world_type") &&
            localization.hasTranslation(Language::SimplifiedChinese,
                                         "menu.create.world_type") &&
            localization.hasTranslation(Language::English, "common.superflat") &&
            localization.hasTranslation(Language::SimplifiedChinese,
                                         "common.superflat"),
            "world type strings are translated in both languages");
    constexpr const char* inputSettingsKeys[] = {
        "settings.key_bindings", "settings.key_bindings_title",
        "settings.keyboard_mouse", "settings.keyboard_mouse_title",
        "settings.controller", "settings.controller_title",
        "settings.touch_controls", "settings.touch_title",
        "settings.back_to_bindings", "settings.controller_help",
        "settings.controller_capture", "settings.controller_deadzone",
        "settings.controller_sensitivity", "settings.controller_invert_y",
        "settings.controller_vibration", "settings.controller_reset"
    };
    for (const char* key : inputSettingsKeys) {
        require(localization.hasTranslation(Language::English, key),
                "input setting is translated in English");
        require(localization.hasTranslation(Language::SimplifiedChinese, key),
                "input setting is translated in Simplified Chinese");
    }
    require(localization.hasTranslation(Language::English, "settings.frame_rate") &&
            localization.hasTranslation(
                Language::SimplifiedChinese, "settings.frame_rate"),
            "frame-rate setting is translated");
    constexpr const char* combatKeys[] = {
        "settings.attack_indicator", "settings.attack_crosshair",
        "settings.attack_hotbar", "settings.attack_off",
        "tooltip.attack_speed"
    };
    for (const Language language : languagesByEnglishName())
        for (const char* key : combatKeys)
            require(localization.hasTranslation(language, key),
                    "combat HUD and tooltip keys are translated");
    for (size_t action = 0; action < INPUT_ACTION_COUNT; ++action)
        require(localization.hasTranslation(
            Language::SimplifiedChinese, "action." + std::to_string(action)),
            "every input action is translated");
    for (uint16_t item = 0; item < static_cast<uint16_t>(ItemId::COUNT); ++item)
        require(localization.hasTranslation(
            Language::SimplifiedChinese, "item." + std::to_string(item)),
            "every item is translated");
    for (size_t index = 0;
         index < static_cast<size_t>(CreativeItemCategory::Count); ++index) {
        const auto& info = creativeCategoryInfo(
            static_cast<CreativeItemCategory>(index));
        require(localization.hasTranslation(Language::English,
                                            info.localizationKey) &&
                localization.hasTranslation(Language::SimplifiedChinese,
                                            info.localizationKey),
                "every creative tab name is translated");
    }
    for (int raw = 0; raw < BIOME_COUNT; ++raw) {
        const std::string key = "biome." +
            std::string(biomeCommandName(static_cast<Biome>(raw)));
        require(localization.hasTranslation(Language::English, key) &&
                localization.hasTranslation(Language::SimplifiedChinese, key),
                "every biome is translated");
    }

    // The language option lists every language sorted by the first letter of
    // its English name, and each code survives the settings round trip.
    const auto& order = languagesByEnglishName();
    require(order.size() == 10, "ten languages are supported");
    const char* expectedNames[] = {
        "Arabic", "Simplified Chinese", "English", "French", "German",
        "Japanese", "Korean", "Portuguese", "Russian", "Spanish"
    };
    for (size_t index = 0; index < order.size(); ++index) {
        require(languageEnglishName(order[index]) == expectedNames[index],
                "language option is sorted by the English name");
        require(parseLanguage(languageCode(order[index])) == order[index],
                "language code round-trips");
        require(!languageNativeName(order[index]).empty(),
                "every language has a native display name");
    }
    require(nextLanguage(Language::Spanish) == Language::Arabic &&
            nextLanguage(Language::Arabic) == Language::SimplifiedChinese,
            "language cycling follows the sorted order and wraps around");

    // Every language file must cover the full English key set, and every
    // non-English file must also name every registered item.
    std::unordered_set<std::string> englishKeys;
    {
        std::ifstream enFile(root / "assets" / "lang" / "en_us.json");
        nlohmann::json enJson;
        enFile >> enJson;
        require(enJson.is_object(), "en_us.json parses");
        for (auto it = enJson.begin(); it != enJson.end(); ++it)
            englishKeys.insert(it.key());
    }
    for (const Language language : order) {
        localization.setLanguage(language);
        require(localization.text("menu.home.singleplayer") !=
                    "[menu.home.singleplayer]",
                "menu strings resolve in every language");
        std::ifstream file(root / "assets" / "lang" /
            (std::string(languageCode(language)) + ".json"));
        nlohmann::json json;
        file >> json;
        require(json.is_object(), "language file parses");
        size_t missing = 0;
        for (const std::string& key : englishKeys)
            if (!json.contains(key)) ++missing;
        require(missing == 0, "language file covers every English key");
        if (language == Language::English) continue;
        size_t missingItems = 0;
        for (uint16_t item = 0; item < static_cast<uint16_t>(ItemId::COUNT); ++item)
            if (!json.contains("item." + std::to_string(item))) ++missingItems;
        require(missingItems == 0, "language file names every item");
        for (const char* key : inputSettingsKeys)
            require(localization.hasTranslation(language, key),
                    "input settings are translated in every language");
        for (size_t action = 0; action < INPUT_ACTION_COUNT; ++action)
            require(localization.hasTranslation(
                        language, "action." + std::to_string(action)),
                    "every input action is translated in every language");
        for (uint16_t item = 0; item < static_cast<uint16_t>(ItemId::COUNT); ++item)
            require(localization.hasTranslation(
                        language, "item." + std::to_string(item)),
                    "every item is translated in every language");
        for (int raw = 0; raw < BIOME_COUNT; ++raw)
            require(localization.hasTranslation(language,
                        "biome." + std::string(biomeCommandName(
                            static_cast<Biome>(raw)))),
                    "every biome is translated in every language");
        for (size_t index = 0;
             index < static_cast<size_t>(CreativeItemCategory::Count); ++index)
            require(localization.hasTranslation(language,
                        creativeCategoryInfo(static_cast<CreativeItemCategory>(
                            index)).localizationKey),
                    "every creative tab name is translated in every language");
    }
    localization.setLanguage(Language::English);
    require(localization.text("menu.home.settings") == "Settings",
            "switching back to English restores English strings");

    // Arabic strings are shaped into joined presentation forms and reordered
    // right to left so the left-to-right renderers draw them correctly.
    require(shapeArabic("مرحبا") == "ﺎﺒﺣﺮﻣ",
            "Arabic letters join into presentation forms in display order");
    require(shapeArabic("لا") == "ﻵ",
            "the lam-alef ligature is applied");
    require(shapeArabic("سرعة: 60") == "60 :ﺔﻋﺮﺳ",
            "digits move to the left of shaped Arabic text");
    localization.setLanguage(Language::Arabic);
    // الإعدادات joins its leading lam with the alef-hamza-below ligature.
    require(localization.text("menu.home.settings") ==
            "ﺕﺍﺩﺍﺪﻋﻹﺍ",
            "Arabic UI strings are shaped for display");
    localization.setLanguage(Language::English);

    std::string name = "World";
    appendUtf8(name, 0x4E16);
    appendUtf8(name, 0x754C);
    require(name == "World世界" && utf8CodepointCount(name) == 7,
            "UTF-8 append and count support mixed text");
    require(eraseLastUtf8Codepoint(name) && name == "World世",
            "UTF-8 erase removes one complete codepoint");
    const auto malformed = decodeUtf8(std::string("\xE4\xB8", 2));
    require(malformed.size() == 1 && malformed[0] == 0xFFFD,
            "truncated UTF-8 becomes one replacement codepoint");

    const auto fontPath =
        root / "assets" / "fonts" / "noto" / "NotoSansCJKsc-Regular.otf";
    std::ifstream input(fontPath, std::ios::binary);
    require(static_cast<bool>(input), "bundled CJK font exists");
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    input.seekg(0, std::ios::beg);
    std::vector<unsigned char> data(static_cast<size_t>(size));
    input.read(reinterpret_cast<char*>(data.data()), size);
    stbtt_fontinfo font{};
    require(stbtt_InitFont(&font, data.data(), 0) != 0,
            "bundled CJK font parses");
    for (uint32_t codepoint : decodeUtf8("简体中文世界"))
        require(stbtt_FindGlyphIndex(&font, static_cast<int>(codepoint)) != 0,
                "bundled font contains representative Chinese glyphs");
    for (uint32_t codepoint : decodeUtf8("Привет한글かなéßñ"))
        require(stbtt_FindGlyphIndex(&font, static_cast<int>(codepoint)) != 0,
                "bundled CJK font covers Cyrillic, Hangul, kana and Latin-1");

    const auto arabicFontPath =
        root / "assets" / "fonts" / "noto" / "NotoNaskhArabic-Regular.ttf";
    std::ifstream arabicInput(arabicFontPath, std::ios::binary);
    require(static_cast<bool>(arabicInput), "bundled Arabic font exists");
    arabicInput.seekg(0, std::ios::end);
    const auto arabicSize = arabicInput.tellg();
    arabicInput.seekg(0, std::ios::beg);
    std::vector<unsigned char> arabicData(static_cast<size_t>(arabicSize));
    arabicInput.read(reinterpret_cast<char*>(arabicData.data()), arabicSize);
    stbtt_fontinfo arabicFont{};
    require(stbtt_InitFont(&arabicFont, arabicData.data(), 0) != 0,
            "bundled Arabic font parses");
    for (uint32_t codepoint : decodeUtf8(shapeArabic("مرحبا بالعالم")))
        if (codepoint >= 128)
            require(stbtt_FindGlyphIndex(&arabicFont, static_cast<int>(codepoint)) != 0,
                    "Arabic font covers every shaped presentation form");

    // Every non-ASCII codepoint of every translation must rasterize from one
    // of the two bundled faces.
    for (const Language language : languagesByEnglishName()) {
        std::ifstream langFile(root / "assets" / "lang" /
            (std::string(languageCode(language)) + ".json"));
        nlohmann::json langJson;
        langFile >> langJson;
        for (auto it = langJson.begin(); it != langJson.end(); ++it) {
            if (!it.value().is_string()) continue;
            for (uint32_t codepoint : decodeUtf8(it.value().get<std::string>())) {
                if (codepoint < 128) continue;
                require(stbtt_FindGlyphIndex(&font, static_cast<int>(codepoint)) != 0 ||
                        stbtt_FindGlyphIndex(&arabicFont, static_cast<int>(codepoint)) != 0,
                        "every translation glyph resolves in a bundled font");
            }
        }
        // The language option label is built from the in-code native names, so
        // those glyphs must resolve too.
        for (uint32_t codepoint : decodeUtf8(languageNativeName(language))) {
            if (codepoint < 128) continue;
            require(stbtt_FindGlyphIndex(&font, static_cast<int>(codepoint)) != 0 ||
                    stbtt_FindGlyphIndex(&arabicFont, static_cast<int>(codepoint)) != 0,
                    "every native language name resolves in a bundled font");
        }
    }

    std::cout << "Localization and UTF-8 tests passed\n";
}
