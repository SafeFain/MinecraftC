#pragma once
#include "core/LanDiscovery.h"
#include <algorithm>
#include <cctype>

namespace Platform::DiscoveryValidation {
inline bool text(const std::string& value, size_t maximum) {
    return !value.empty() && value.size() <= maximum && std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
inline uint32_t number(const std::string& value, uint32_t maximum) {
    if (value.empty() || value.size() > 10) return 0;
    uint64_t result = 0;
    for (char c : value) { if (c < '0' || c > '9') return 0; result = result * 10 + static_cast<unsigned>(c - '0'); }
    return result <= maximum ? static_cast<uint32_t>(result) : 0;
}
inline bool signature(const std::string& value) {
    return value.size()<=20 && std::all_of(value.begin(),value.end(),[](char c){return c>='0'&&c<='9';});
}
inline bool valid(const LanAdvertisement& room) {
    return room.instance.size() == 32 && std::all_of(room.instance.begin(), room.instance.end(), [](unsigned char c) { return std::isxdigit(c); }) &&
        text(room.name, 128) && text(room.version, 64) && room.port && room.protocol && room.generation && signature(room.contentSignature) &&
        room.capacity >= 2 && room.capacity <= 8 && room.players >= 1 && room.players <= room.capacity;
}
inline bool valid(const LanDiscoveredRoom& room) {
    return text(room.instance, 256) && text(room.name, 128) && text(room.version, 64) && text(room.address, 128) &&
        room.port && room.protocol && room.generation && signature(room.contentSignature) && room.capacity >= 2 && room.capacity <= 8 && room.players >= 1 && room.players <= room.capacity;
}
}
