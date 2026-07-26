#pragma once

#include <array>
#include <cstddef>

#include "game/Item.h"

class InventoryModel {
public:
    static constexpr size_t HOTBAR_SIZE = 9;
    static constexpr size_t MAIN_SIZE = 27;
    static constexpr size_t STORAGE_SIZE = HOTBAR_SIZE + MAIN_SIZE;
    static constexpr size_t ARMOR_SIZE = 4;

    const ItemStack& slot(size_t index) const { return m_storage.at(index); }
    ItemStack& slot(size_t index) { return m_storage.at(index); }
    const std::array<ItemStack, STORAGE_SIZE>& storage() const { return m_storage; }
    const std::array<ItemStack, ARMOR_SIZE>& armor() const { return m_armor; }
    std::array<ItemStack, ARMOR_SIZE>& armor() { return m_armor; }
    const ItemStack& offhand() const { return m_offhand; }
    ItemStack& offhand() { return m_offhand; }

    uint32_t count(ItemId id) const;
    uint32_t add(ItemStack stack);
    bool remove(ItemId id, uint32_t count);
    void clear();

private:
    std::array<ItemStack, STORAGE_SIZE> m_storage{};
    std::array<ItemStack, ARMOR_SIZE> m_armor{};
    ItemStack m_offhand{};
};

