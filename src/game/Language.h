#pragma once

#include <cstdint>
#include <string_view>

enum class Language : uint8_t {
    English,
    SimplifiedChinese
};

constexpr std::string_view languageCode(Language language) {
    return language == Language::SimplifiedChinese ? "zh_cn" : "en_us";
}

Language parseLanguage(std::string_view code);
