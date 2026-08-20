#include "game/Localization.h"
#include "game/Utf8.h"
#include "world/Biome.h"

#include <stb_truetype.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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
    for (size_t action = 0; action < INPUT_ACTION_COUNT; ++action)
        require(localization.hasTranslation(
            Language::SimplifiedChinese, "action." + std::to_string(action)),
            "every input action is translated");
    for (uint16_t item = 0; item < static_cast<uint16_t>(ItemId::COUNT); ++item)
        require(localization.hasTranslation(
            Language::SimplifiedChinese, "item." + std::to_string(item)),
            "every item is translated");
    for (int raw = 0; raw < BIOME_COUNT; ++raw) {
        const std::string key = "biome." +
            std::string(biomeCommandName(static_cast<Biome>(raw)));
        require(localization.hasTranslation(Language::English, key) &&
                localization.hasTranslation(Language::SimplifiedChinese, key),
                "every biome is translated");
    }

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

    std::cout << "Localization and UTF-8 tests passed\n";
}
