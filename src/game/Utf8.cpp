#include "game/Utf8.h"

namespace {
constexpr uint32_t replacement = 0xFFFD;

bool continuation(unsigned char value) {
    return (value & 0xC0u) == 0x80u;
}
}

std::vector<uint32_t> decodeUtf8(std::string_view text) {
    std::vector<uint32_t> result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i]);
        if (first < 0x80u) {
            result.push_back(first);
            ++i;
            continue;
        }
        size_t length = 0;
        uint32_t value = 0;
        uint32_t minimum = 0;
        if ((first & 0xE0u) == 0xC0u) {
            length = 2; value = first & 0x1Fu; minimum = 0x80u;
        } else if ((first & 0xF0u) == 0xE0u) {
            length = 3; value = first & 0x0Fu; minimum = 0x800u;
        } else if ((first & 0xF8u) == 0xF0u) {
            length = 4; value = first & 0x07u; minimum = 0x10000u;
        } else {
            result.push_back(replacement);
            ++i;
            continue;
        }
        if (i + length > text.size()) {
            result.push_back(replacement);
            break;
        }
        bool valid = true;
        for (size_t offset = 1; offset < length; ++offset) {
            const auto byte = static_cast<unsigned char>(text[i + offset]);
            if (!continuation(byte)) { valid = false; break; }
            value = (value << 6u) | (byte & 0x3Fu);
        }
        if (!valid || value < minimum || value > 0x10FFFFu ||
            (value >= 0xD800u && value <= 0xDFFFu)) {
            result.push_back(replacement);
            ++i;
        } else {
            result.push_back(value);
            i += length;
        }
    }
    return result;
}

void appendUtf8(std::string& text, uint32_t codepoint) {
    if (codepoint > 0x10FFFFu || (codepoint >= 0xD800u && codepoint <= 0xDFFFu))
        codepoint = replacement;
    if (codepoint <= 0x7Fu) {
        text.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FFu) {
        text.push_back(static_cast<char>(0xC0u | (codepoint >> 6u)));
        text.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else if (codepoint <= 0xFFFFu) {
        text.push_back(static_cast<char>(0xE0u | (codepoint >> 12u)));
        text.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
        text.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else {
        text.push_back(static_cast<char>(0xF0u | (codepoint >> 18u)));
        text.push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3Fu)));
        text.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
        text.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
}

bool eraseLastUtf8Codepoint(std::string& text) {
    if (text.empty()) return false;
    size_t start = text.size() - 1;
    while (start > 0 && continuation(static_cast<unsigned char>(text[start]))) --start;
    text.erase(start);
    return true;
}

size_t utf8CodepointCount(std::string_view text) {
    return decodeUtf8(text).size();
}
