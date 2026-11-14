#pragma once
#include "game/Item.h"
#include <cmath>
#include <glm/glm.hpp>

// Geometry/alpha clipping is supplied by the existing icon or model draw.
// This original animated violet sheen never modifies the authored atlas.
inline glm::vec4 enchantedSheen(const ItemStack& stack, float seconds) {
    if (stack.components.enchantments.empty() && stack.components.storedEnchantments.empty()) return glm::vec4(1);
    const float pulse = .5f + .5f * std::sin(seconds * 2.8f);
    return {1.f-.12f*pulse,1.f-.32f*pulse,1.f,1.f};
}
