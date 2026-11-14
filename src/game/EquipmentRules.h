#pragma once
#include "game/InventoryModel.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace EquipmentRules {
// RNG belongs to the gameplay authority, never world-generation domains.
inline uint32_t random(uint64_t& state) {
    state += 0x9e3779b97f4a7c15ULL;
    uint64_t value = state;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return static_cast<uint32_t>((value ^ (value >> 31)) >> 32);
}
inline bool damage(ItemStack& stack, uint32_t amount, uint64_t& state) {
    const auto maximum = getItemProps(stack.id).maxDurability;
    if (stack.empty() || !maximum) return false;
    const auto level = stack.enchantment(EnchantmentId::Unbreaking);
    for (uint32_t i = 0; i < amount; ++i) {
        // Armor always takes wear 60% of the time, then applies Unbreaking.
        const bool armor = getItemProps(stack.id).kind == ItemKind::Armor;
        if (level && (!armor || random(state) % 10 >= 6) && random(state) % (level + 1) != 0) continue;
        if (++stack.damage >= maximum) { stack.clear(); return true; }
    }
    return false;
}
inline uint32_t repairWithExperience(InventoryModel& inventory, size_t mainHand,
                                    uint32_t points, uint64_t& state) {
    while (points) {
        std::array<ItemStack*, 6> eligible{};
        size_t count = 0;
        const auto add = [&](ItemStack& stack) {
            if (!stack.empty() && stack.damage && getItemProps(stack.id).maxDurability &&
                stack.enchantment(EnchantmentId::Mending)) eligible[count++] = &stack;
        };
        if (mainHand < InventoryModel::STORAGE_SIZE) add(inventory.slot(mainHand));
        add(inventory.offhand());
        for (auto& armor : inventory.armor()) add(armor);
        if (!count) break;
        auto& target = *eligible[random(state) % count];
        const auto repaired = static_cast<uint32_t>(std::min<uint64_t>(target.damage, uint64_t(points) * 2));
        target.damage = static_cast<uint16_t>(target.damage - repaired);
        points -= repaired / 2;
    }
    return points;
}
inline unsigned protection(const InventoryModel& inventory, EnchantmentId specific,
                           bool general = true, unsigned multiplier = 2) {
    unsigned value = 0;
    for (const auto& stack : inventory.armor()) if (!stack.empty()) {
        if (general) value += stack.enchantment(EnchantmentId::Protection);
        if (specific != EnchantmentId::Protection) value += multiplier * stack.enchantment(specific);
    }
    return std::min(value, 20u);
}
}
