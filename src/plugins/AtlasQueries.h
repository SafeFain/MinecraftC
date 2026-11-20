#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Plugins {
std::optional<uint16_t> blockFaceTile(uint16_t block, size_t face);
uint16_t itemTile(uint16_t item);
uint16_t registeredMaterialTile(const std::string& key);
}
