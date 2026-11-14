#pragma once

#include "game/Enchantment.h"
#include <string>
#include <stdexcept>

// Per-stack value state; all inventory copies and codecs preserve it.
class ItemComponents {
public:
    EnchantmentLevels enchantments;
    EnchantmentLevels storedEnchantments;
    uint32_t repairCost = 0;
    const std::string& customName() const { return m_name; }
    bool setCustomName(std::string_view name) {
        if (!validName(name)) return false;
        m_name.assign(name);
        return true;
    }
    bool operator==(const ItemComponents& other) const {
        return enchantments == other.enchantments &&
            storedEnchantments == other.storedEnchantments &&
            repairCost == other.repairCost && m_name == other.m_name;
    }
    bool empty() const {
        return enchantments.empty() && storedEnchantments.empty() && !repairCost && m_name.empty();
    }
    uint64_t fingerprint() const {
        uint64_t hash = 1469598103934665603ULL;
        const auto byte = [&hash](uint8_t value) { hash ^= value; hash *= 1099511628211ULL; };
        for (auto value : enchantments.values()) byte(value);
        for (auto value : storedEnchantments.values()) byte(value);
        for (unsigned shift = 0; shift < 32; shift += 8) byte(static_cast<uint8_t>(repairCost >> shift));
        for (unsigned char c : m_name) byte(c);
        return hash;
    }
    // Reject malformed UTF-8, controls, overlong sequences, surrogates and
    // out-of-range scalars; enforce the anvil's 50-character limit.
    static bool validName(std::string_view name) {
        if (name.size() > 200) return false;
        size_t characters = 0;
        for (size_t i = 0; i < name.size();) {
            const auto first = static_cast<uint8_t>(name[i++]);
            uint32_t scalar = first;
            unsigned continuation = 0;
            uint32_t minimum = 0;
            if (first >= 0xc2 && first <= 0xdf) { continuation = 1; scalar &= 0x1f; minimum = 0x80; }
            else if (first >= 0xe0 && first <= 0xef) { continuation = 2; scalar &= 0x0f; minimum = 0x800; }
            else if (first >= 0xf0 && first <= 0xf4) { continuation = 3; scalar &= 7; minimum = 0x10000; }
            else if (first >= 0x80) return false;
            if (i + continuation > name.size()) return false;
            while (continuation--) {
                const auto next = static_cast<uint8_t>(name[i++]);
                if ((next & 0xc0) != 0x80) return false;
                scalar = (scalar << 6) | (next & 0x3f);
            }
            if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff) ||
                scalar < 0x20 || (scalar >= 0x7f && scalar <= 0x9f) || ++characters > 50) return false;
        }
        return true;
    }
private:
    std::string m_name;
};

// One bounded little-endian representation shared by disk and wire codecs.
template<class WriteByte>
void writeItemComponents(const ItemComponents& value, WriteByte write) {
    for (auto level : value.enchantments.values()) write(level);
    for (auto level : value.storedEnchantments.values()) write(level);
    for (unsigned shift = 0; shift < 32; shift += 8) write(static_cast<uint8_t>(value.repairCost >> shift));
    write(static_cast<uint8_t>(value.customName().size()));
    for (unsigned char byte : value.customName()) write(byte);
}
template<class ReadByte>
ItemComponents readItemComponents(ReadByte read) {
    ItemComponents value;
    for (auto* levels : {&value.enchantments, &value.storedEnchantments})
        for (size_t i = 0; i < ENCHANTMENTS.size(); ++i)
            if (!levels->set(static_cast<EnchantmentId>(i), read())) throw std::runtime_error("Invalid enchantment level");
    for (unsigned shift = 0; shift < 32; shift += 8) value.repairCost |= uint32_t(read()) << shift;
    const auto count = read();
    if (count > 200) throw std::runtime_error("Item name exceeds bound");
    std::string name;
    for (unsigned i = 0; i < count; ++i) name.push_back(static_cast<char>(read()));
    if (!value.setCustomName(name)) throw std::runtime_error("Invalid item name");
    return value;
}
