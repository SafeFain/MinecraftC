#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace Plugins {
struct AtlasBindings {
    std::map<std::string, uint16_t> materials;
    std::map<uint16_t, std::array<uint16_t, 6>> blocks;
    std::map<uint16_t, uint16_t> items;
};

// Publish only after the replacement atlas has been built and uploaded.
// Workers can continue reading the current bindings during device recovery.
void publishAtlasBindings(std::shared_ptr<const AtlasBindings> bindings);
}
