#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "game/Utf8.h"

inline std::vector<std::string> wrapTextPixels(
    const std::string& text, float maxWidth,
    const std::function<float(const std::string&)>& measure) {
    std::vector<std::string> lines;
    std::string line;
    auto emit = [&] {
        while (!line.empty() && line.back() == ' ') line.pop_back();
        lines.push_back(line);
        line.clear();
    };

    for (uint32_t codepoint : decodeUtf8(text)) {
        if (codepoint == '\n') {
            emit();
            continue;
        }
        std::string encoded;
        appendUtf8(encoded, codepoint);
        std::string candidate = line + encoded;
        if (line.empty() || measure(candidate) <= maxWidth) {
            line = std::move(candidate);
            continue;
        }

        if (codepoint == ' ') {
            emit();
            continue;
        }

        const size_t space = line.find_last_of(' ');
        if (space != std::string::npos) {
            std::string remainder = line.substr(space + 1);
            line.erase(space);
            emit();
            line = std::move(remainder);
        } else {
            emit();
        }
        if (codepoint != ' ' || !line.empty()) line += encoded;
    }
    if (!line.empty() || lines.empty()) emit();
    return lines;
}
