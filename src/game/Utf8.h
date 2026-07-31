#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

std::vector<uint32_t> decodeUtf8(std::string_view text);
void appendUtf8(std::string& text, uint32_t codepoint);
bool eraseLastUtf8Codepoint(std::string& text);
size_t utf8CodepointCount(std::string_view text);
