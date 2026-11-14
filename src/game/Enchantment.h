#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// Stable component identities. Append new entries; serialized ordinals never move.
enum class EnchantmentId : uint8_t {
    Unbreaking, Mending, VanishingCurse, Efficiency, Fortune, SilkTouch,
    Sharpness, Smite, BaneOfArthropods, Knockback, FireAspect, Looting,
    SweepingEdge, Power, Punch, Flame, Infinity, Protection, FireProtection,
    BlastProtection, ProjectileProtection, FeatherFalling, Respiration,
    AquaAffinity, Thorns, DepthStrider, FrostWalker, SoulSpeed, SwiftSneak,
    BindingCurse, LuckOfTheSea, Lure, Count
};

struct EnchantmentDefinition {
    std::string_view key;
    uint8_t maximumLevel;
    uint8_t weight;
    uint8_t anvilCost;
    uint8_t exclusiveGroup;
    bool treasure;
    bool curse;
    bool tradeable;
};

inline constexpr std::array<EnchantmentDefinition, 32> ENCHANTMENTS{{
    {"minecraft:unbreaking", 3, 5, 2, 0, false, false, true},
    {"minecraft:mending", 1, 2, 4, 1, true, false, true},
    {"minecraft:vanishing_curse", 1, 1, 8, 0, true, true, true},
    {"minecraft:efficiency", 5, 10, 1, 0, false, false, true},
    {"minecraft:fortune", 3, 2, 4, 2, false, false, true},
    {"minecraft:silk_touch", 1, 1, 8, 2, false, false, true},
    {"minecraft:sharpness", 5, 10, 1, 3, false, false, true},
    {"minecraft:smite", 5, 5, 2, 3, false, false, true},
    {"minecraft:bane_of_arthropods", 5, 5, 2, 3, false, false, true},
    {"minecraft:knockback", 2, 5, 2, 0, false, false, true},
    {"minecraft:fire_aspect", 2, 2, 4, 0, false, false, true},
    {"minecraft:looting", 3, 2, 4, 0, false, false, true},
    {"minecraft:sweeping_edge", 3, 2, 4, 0, false, false, true},
    {"minecraft:power", 5, 10, 1, 0, false, false, true},
    {"minecraft:punch", 2, 2, 4, 0, false, false, true},
    {"minecraft:flame", 1, 2, 4, 0, false, false, true},
    {"minecraft:infinity", 1, 1, 8, 1, false, false, true},
    {"minecraft:protection", 4, 10, 1, 4, false, false, true},
    {"minecraft:fire_protection", 4, 5, 2, 4, false, false, true},
    {"minecraft:blast_protection", 4, 2, 4, 4, false, false, true},
    {"minecraft:projectile_protection", 4, 5, 2, 4, false, false, true},
    {"minecraft:feather_falling", 4, 5, 2, 0, false, false, true},
    {"minecraft:respiration", 3, 2, 4, 0, false, false, true},
    {"minecraft:aqua_affinity", 1, 2, 4, 0, false, false, true},
    {"minecraft:thorns", 3, 1, 8, 0, false, false, true},
    {"minecraft:depth_strider", 3, 2, 4, 5, false, false, true},
    {"minecraft:frost_walker", 2, 2, 4, 5, true, false, true},
    {"minecraft:soul_speed", 3, 1, 8, 0, true, false, false},
    {"minecraft:swift_sneak", 3, 1, 8, 0, true, false, false},
    {"minecraft:binding_curse", 1, 1, 8, 0, true, true, true},
    {"minecraft:luck_of_the_sea", 3, 2, 4, 0, false, false, true},
    {"minecraft:lure", 3, 2, 4, 0, false, false, true},
}};
static_assert(ENCHANTMENTS.size() == static_cast<size_t>(EnchantmentId::Count));

inline constexpr bool validEnchantment(EnchantmentId id) {
    return static_cast<size_t>(id) < ENCHANTMENTS.size();
}
inline constexpr bool compatibleEnchantments(EnchantmentId a, EnchantmentId b) {
    if (!validEnchantment(a) || !validEnchantment(b) || a == b) return false;
    const auto group = ENCHANTMENTS[static_cast<size_t>(a)].exclusiveGroup;
    return !group || group != ENCHANTMENTS[static_cast<size_t>(b)].exclusiveGroup;
}

// Dense bounded values provide canonical ordering and cannot contain duplicate IDs.
class EnchantmentLevels {
public:
    uint8_t level(EnchantmentId id) const {
        return validEnchantment(id) ? m_levels[static_cast<size_t>(id)] : 0;
    }
    bool set(EnchantmentId id, uint8_t value) {
        if (!validEnchantment(id) || value > ENCHANTMENTS[static_cast<size_t>(id)].maximumLevel) return false;
        m_levels[static_cast<size_t>(id)] = value;
        return true;
    }
    bool compatible() const {
        for (size_t i = 0; i < m_levels.size(); ++i)
            for (size_t j = i + 1; j < m_levels.size(); ++j)
                if (m_levels[i] && m_levels[j] && !compatibleEnchantments(
                    static_cast<EnchantmentId>(i), static_cast<EnchantmentId>(j))) return false;
        return true;
    }
    bool empty() const {
        for (auto value : m_levels) if (value) return false;
        return true;
    }
    const std::array<uint8_t, 32>& values() const { return m_levels; }
    bool operator==(const EnchantmentLevels& other) const { return m_levels == other.m_levels; }
private:
    std::array<uint8_t, 32> m_levels{};
};
