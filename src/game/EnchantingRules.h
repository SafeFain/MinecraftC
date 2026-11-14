#pragma once
#include "game/EquipmentRules.h"
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace EnchantingRules {
inline int armorSlot(ItemId item) {
    if (getItemProps(item).kind != ItemKind::Armor) return -1;
    if (item >= ItemId::NETHERITE_HELMET && item <= ItemId::NETHERITE_BOOTS)
        return static_cast<int>(item) - static_cast<int>(ItemId::NETHERITE_HELMET);
    return (static_cast<int>(item) - static_cast<int>(ItemId::LEATHER_HELMET)) % 4;
}
inline bool supports(ItemId item, EnchantmentId id, bool table = false) {
    if (!validEnchantment(id) || item == ItemId::EMPTY) return false;
    if (item == ItemId::BOOK || item == ItemId::ENCHANTED_BOOK) return true;
    const auto& props = getItemProps(item);
    const auto tool = props.tool;
    const int armor = armorSlot(item);
    const bool mining = tool == ToolKind::Pickaxe || tool == ToolKind::Axe || tool == ToolKind::Shovel || tool == ToolKind::Hoe;
    const bool sword = tool == ToolKind::Sword;
    switch (id) {
    case EnchantmentId::Unbreaking: case EnchantmentId::Mending: case EnchantmentId::VanishingCurse:
        return props.maxDurability && (!table || mining || sword || armor >= 0 || item == ItemId::BOW || item == ItemId::FISHING_ROD);
    case EnchantmentId::Efficiency: case EnchantmentId::Fortune: case EnchantmentId::SilkTouch: return mining;
    case EnchantmentId::Sharpness: case EnchantmentId::Smite: case EnchantmentId::BaneOfArthropods:
        return sword || (!table && tool == ToolKind::Axe);
    case EnchantmentId::Knockback: case EnchantmentId::FireAspect: case EnchantmentId::Looting: case EnchantmentId::SweepingEdge: return sword;
    case EnchantmentId::Power: case EnchantmentId::Punch: case EnchantmentId::Flame: case EnchantmentId::Infinity: return item == ItemId::BOW;
    case EnchantmentId::Protection: case EnchantmentId::FireProtection: case EnchantmentId::BlastProtection: case EnchantmentId::ProjectileProtection: case EnchantmentId::BindingCurse: return armor >= 0;
    case EnchantmentId::Thorns: return table ? armor == 1 : armor >= 0;
    case EnchantmentId::Respiration: case EnchantmentId::AquaAffinity: return armor == 0;
    case EnchantmentId::FeatherFalling: case EnchantmentId::DepthStrider: case EnchantmentId::FrostWalker: case EnchantmentId::SoulSpeed: return armor == 3;
    case EnchantmentId::SwiftSneak: return armor == 2;
    case EnchantmentId::LuckOfTheSea: case EnchantmentId::Lure: return item == ItemId::FISHING_ROD;
    default: return false;
    }
}
inline bool validComponents(const ItemStack& stack) {
    if (stack.components.repairCost > INT32_MAX) return false;
    if (stack.empty()) return stack.components.empty();
    if (stack.id != ItemId::ENCHANTED_BOOK && !stack.components.storedEnchantments.empty()) return false;
    if (stack.id == ItemId::ENCHANTED_BOOK && !stack.components.enchantments.empty()) return false;
    if (!stack.components.enchantments.compatible()) return false;
    // Applicability is an acquisition rule: Creative anvils may enchant other items.
    return true;
}
inline unsigned enchantability(ItemId item) {
    if (item == ItemId::BOOK || item == ItemId::BOW || item == ItemId::FISHING_ROD) return 1;
    const auto& props = getItemProps(item);
    if (props.kind == ItemKind::Armor) {
        if (item >= ItemId::LEATHER_HELMET && item <= ItemId::LEATHER_BOOTS) return 15;
        if (item >= ItemId::IRON_HELMET && item <= ItemId::IRON_BOOTS) return 9;
        if (item >= ItemId::GOLDEN_HELMET && item <= ItemId::GOLDEN_BOOTS) return 25;
        return item >= ItemId::NETHERITE_HELMET && item <= ItemId::NETHERITE_BOOTS ? 15 : 10;
    }
    if (props.tool == ToolKind::None || props.tool == ToolKind::Shield || props.tool == ToolKind::FishingRod) return 0;
    switch (props.tier) {
    case ToolTier::Wood: return 15; case ToolTier::Stone: return 5; case ToolTier::Iron: return 14;
    case ToolTier::Gold: return 22; case ToolTier::Diamond: return 10; case ToolTier::Netherite: return 15;
    default: return 0;
    }
}
// Java 1.21.11 vanilla enchantment min_cost and max_cost coefficients.
inline constexpr std::array<std::array<unsigned, 3>, 32> COSTS{{
    {{5,8,50}}, {{25,25,50}}, {{25,0,25}}, {{1,10,50}}, {{15,9,50}}, {{15,0,50}},
    {{1,11,20}}, {{5,8,20}}, {{5,8,20}}, {{5,20,50}}, {{10,20,50}}, {{15,9,50}},
    {{5,9,15}}, {{1,10,15}}, {{12,20,25}}, {{20,0,30}}, {{20,0,30}},
    {{1,11,11}}, {{10,8,8}}, {{5,8,8}}, {{3,6,6}}, {{5,6,6}}, {{10,10,30}},
    {{1,0,40}}, {{10,20,50}}, {{10,10,15}}, {{10,10,15}}, {{10,10,15}}, {{25,25,50}},
    {{25,0,25}}, {{15,9,50}}, {{15,9,50}}
}};
inline unsigned minimumCost(EnchantmentId id, unsigned level) {
    const auto& cost = COSTS.at(static_cast<size_t>(id));
    return cost[0] + (level - 1) * cost[1];
}
inline ItemStack randomEnchant(ItemStack item, unsigned cost, uint64_t& seed, bool treasure = false) {
    const auto ability = enchantability(item.id);
    if (!ability) return item;
    const auto draw = [&](unsigned bound) { return EquipmentRules::random(seed) % bound; };
    cost += 1 + draw(ability / 4 + 1) + draw(ability / 4 + 1);
    const double variation = (double(draw(1u << 24)) + draw(1u << 24)) / (1u << 24) - 1;
    cost = static_cast<unsigned>(std::max(1., std::floor(cost * (1 + variation * .15) + .5)));
    struct Candidate { EnchantmentId id; uint8_t level; };
    std::vector<Candidate> candidates;
    for (size_t i = 0; i < ENCHANTMENTS.size(); ++i) {
        const auto id = static_cast<EnchantmentId>(i); const auto& definition = ENCHANTMENTS[i];
        if ((!treasure && definition.treasure) || (treasure && (id == EnchantmentId::SoulSpeed || id == EnchantmentId::SwiftSneak)) || !supports(item.id, id, true)) continue;
        for (unsigned level = definition.maximumLevel; level > 0; --level) {
            const auto minimum = minimumCost(id, level);
            if (cost >= minimum && cost <= minimum + COSTS[i][2]) { candidates.push_back({id, static_cast<uint8_t>(level)}); break; }
        }
    }
    auto& levels = item.id == ItemId::BOOK || item.id == ItemId::ENCHANTED_BOOK ? item.components.storedEnchantments : item.components.enchantments;
    std::vector<Candidate> selected;
    while (!candidates.empty()) {
        unsigned total = 0; for (const auto candidate : candidates) total += ENCHANTMENTS[static_cast<size_t>(candidate.id)].weight;
        unsigned roll = draw(total); Candidate choice = candidates.back();
        for (const auto candidate : candidates) { const auto weight = ENCHANTMENTS[static_cast<size_t>(candidate.id)].weight; if (roll < weight) { choice = candidate; break; } roll -= weight; }
        selected.push_back(choice);
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](Candidate candidate) { return !compatibleEnchantments(candidate.id, choice.id); }), candidates.end());
        if (draw(50) > cost) break;
        cost /= 2;
    }
    if (item.id == ItemId::BOOK && selected.size() > 1) selected.erase(selected.begin() + draw(static_cast<unsigned>(selected.size())));
    for (const auto chosen : selected) levels.set(chosen.id, chosen.level);
    if (!levels.empty() && item.id == ItemId::BOOK) { item.id = ItemId::ENCHANTED_BOOK; item.count = 1; }
    return item;
}
struct Offer { ItemStack result; unsigned requiredLevel = 0, cost = 0; EnchantmentId clue = EnchantmentId::Count; uint8_t clueLevel = 0; };
inline std::array<Offer, 3> tableOffers(const ItemStack& item, unsigned shelves, uint32_t seed) {
    std::array<Offer, 3> offers{};
    if (item.empty() || !enchantability(item.id) || !item.components.enchantments.empty() || item.id == ItemId::ENCHANTED_BOOK) return offers;
    shelves = std::min(shelves, 15u);
    uint64_t random = seed;
    for (unsigned slot = 0; slot < 3; ++slot) {
        const auto base = 1 + EquipmentRules::random(random) % 8 + shelves / 2 + EquipmentRules::random(random) % (shelves + 1);
        const unsigned level = slot == 0 ? std::max(base / 3, 1u) : slot == 1 ? base * 2 / 3 + 1 : std::max(base, shelves * 2);
        if (level < slot + 1) continue;
        uint64_t selectionSeed = uint64_t(seed) + slot;
        auto& offer = offers[slot]; offer.result = randomEnchant(item.withCount(1), level, selectionSeed); offer.requiredLevel = level; offer.cost = slot + 1;
        const auto& values = offer.result.id == ItemId::ENCHANTED_BOOK ? offer.result.components.storedEnchantments : offer.result.components.enchantments;
        std::vector<EnchantmentId> clues; for (size_t i = 0; i < ENCHANTMENTS.size(); ++i) if (values.level(static_cast<EnchantmentId>(i))) clues.push_back(static_cast<EnchantmentId>(i));
        if (clues.empty()) { offer = {}; continue; }
        offer.clue = clues[EquipmentRules::random(selectionSeed) % clues.size()]; offer.clueLevel = values.level(offer.clue);
    }
    return offers;
}
inline uint32_t nextPenalty(uint32_t value) { return static_cast<uint32_t>(std::min<uint64_t>(uint64_t(value) * 2 + 1, INT32_MAX)); }
struct Result { ItemStack item; uint32_t cost = 0, consumed = 0, experienceMinimum = 0; bool tooExpensive = false; };
inline ItemId repairMaterial(ItemId item) {
    if (item >= ItemId::LEATHER_HELMET && item <= ItemId::LEATHER_BOOTS) return ItemId::LEATHER;
    if (item >= ItemId::IRON_HELMET && item <= ItemId::IRON_BOOTS) return ItemId::IRON_INGOT;
    if (item >= ItemId::GOLDEN_HELMET && item <= ItemId::GOLDEN_BOOTS) return ItemId::GOLD_INGOT;
    if (item >= ItemId::DIAMOND_HELMET && item <= ItemId::DIAMOND_BOOTS) return ItemId::DIAMOND;
    if (item >= ItemId::NETHERITE_HELMET && item <= ItemId::NETHERITE_BOOTS) return ItemId::NETHERITE_INGOT;
    const auto& props = getItemProps(item);
    if (props.tool == ToolKind::Shield) return ItemId::OAK_PLANKS;
    switch (props.tier) {
    case ToolTier::Wood: return ItemId::OAK_PLANKS; case ToolTier::Stone: return ItemId::COBBLESTONE;
    case ToolTier::Iron: return ItemId::IRON_INGOT; case ToolTier::Gold: return ItemId::GOLD_INGOT;
    case ToolTier::Diamond: return ItemId::DIAMOND; case ToolTier::Netherite: return ItemId::NETHERITE_INGOT;
    default: return ItemId::EMPTY;
    }
}
inline Result anvil(const ItemStack& left, const ItemStack& right, const std::optional<std::string>& name, bool creative) {
    Result result;
    if (left.empty()) return result;
    auto output = left; uint64_t cost = uint64_t(left.components.repairCost) + right.components.repairCost;
    unsigned work = 0, renamed = 0; bool changed = false;
    const auto maximum = getItemProps(left.id).maxDurability;
    if (!right.empty()) {
        if (maximum && right.id == repairMaterial(left.id)) {
            const auto repair = std::max(1u, unsigned(maximum) / 4);
            while (output.damage && result.consumed < right.count) { output.damage = static_cast<uint16_t>(output.damage - std::min<unsigned>(repair, output.damage)); ++result.consumed; ++work; }
            if (!result.consumed) return {};
            changed = true;
        } else {
            const bool book = right.id == ItemId::ENCHANTED_BOOK;
            if (!book && (right.id != left.id || !maximum)) return {};
            if (!book) {
                const int remaining = maximum - left.damage + maximum - right.damage + maximum * 12 / 100;
                const auto damage = static_cast<uint16_t>(std::max(0, int(maximum) - remaining));
                if (damage < output.damage) { output.damage = damage; work += 2; changed = true; }
            }
            const auto& incoming = book ? right.components.storedEnchantments : right.components.enchantments;
            bool accepted = false, incompatible = false;
            for (size_t i = 0; i < ENCHANTMENTS.size(); ++i) {
                const auto id = static_cast<EnchantmentId>(i); if (!incoming.level(id)) continue;
                bool applicable = creative || left.id == ItemId::ENCHANTED_BOOK || supports(left.id, id);
                auto& target = left.id == ItemId::ENCHANTED_BOOK ? output.components.storedEnchantments : output.components.enchantments;
                for (size_t j = 0; j < ENCHANTMENTS.size(); ++j) if (j != i && target.level(static_cast<EnchantmentId>(j)) && !compatibleEnchantments(id, static_cast<EnchantmentId>(j))) { applicable = false; ++work; }
                if (!applicable) { incompatible = true; continue; }
                const auto previous = target.level(id);
                const auto level = std::min<unsigned>(ENCHANTMENTS[i].maximumLevel, previous == incoming.level(id) ? previous + 1 : std::max(previous, incoming.level(id)));
                target.set(id, static_cast<uint8_t>(level)); accepted = true; changed = true;
                work += std::max(1u, unsigned(ENCHANTMENTS[i].anvilCost) / (book ? 2 : 1)) * level;
                if (left.count > 1) work = 40;
            }
            if (incompatible && !accepted) return {};
            if (changed) result.consumed = right.count;
        }
    }
    if (name && *name != output.components.customName()) {
        if (!output.components.setCustomName(*name)) return {};
        renamed = 1; ++work; changed = true;
    }
    if (!changed || !work) return {};
    cost += work;
    if (renamed == work && cost >= 40) cost = 39;
    result.cost = static_cast<uint32_t>(std::min<uint64_t>(cost, INT32_MAX));
    result.tooExpensive = !creative && cost >= 40;
    if (renamed != work) output.components.repairCost = nextPenalty(std::max(left.components.repairCost, right.components.repairCost));
    result.item = output;
    return result;
}
inline Result grindstone(const ItemStack& first, const ItemStack& second) {
    Result result;
    const auto& left = first.empty() ? second : first;
    if (left.empty()) return result;
    const auto& right = first.empty() ? first : second;
    const auto maximum = getItemProps(left.id).maxDurability;
    if (left.id != ItemId::ENCHANTED_BOOK && !maximum && left.components.enchantments.empty()) return result;
    if (left.count != 1 || (!right.empty() && (right.id != left.id || right.count != 1))) return result;
    auto output = left;
    if (!right.empty()) {
        if (!maximum) return result;
        output.damage = static_cast<uint16_t>(std::max(0, int(left.damage) + int(right.damage) - int(maximum) - int(maximum) * 5 / 100));
    } else if (left.components.enchantments.empty() && left.components.storedEnchantments.empty()) return result;
    output.components.enchantments = {}; output.components.storedEnchantments = {}; output.components.repairCost = 0;
    for (const auto* source : {&left, &right}) for (size_t i = 0; i < ENCHANTMENTS.size(); ++i) {
        const auto id = static_cast<EnchantmentId>(i);
        const auto level = std::max(source->components.enchantments.level(id), source->components.storedEnchantments.level(id));
        if (!level) continue;
        if (ENCHANTMENTS[i].curse) {
            auto& target = output.id == ItemId::ENCHANTED_BOOK ? output.components.storedEnchantments : output.components.enchantments;
            target.set(id, std::max(target.level(id), level));
        } else result.experienceMinimum += minimumCost(id, level);
    }
    const auto& remaining = output.id == ItemId::ENCHANTED_BOOK ? output.components.storedEnchantments : output.components.enchantments;
    if (output.id == ItemId::ENCHANTED_BOOK && remaining.empty()) output.id = ItemId::BOOK;
    for (auto level : remaining.values()) if (level) output.components.repairCost = nextPenalty(output.components.repairCost);
    result.item = output; result.consumed = 1;
    return result;
}
}
