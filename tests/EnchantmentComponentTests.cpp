#include "game/ItemComponents.h"
#include <cstdlib>
#include <iostream>
#include <set>

static void require(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    std::set<std::string_view> keys;
    for (size_t i = 0; i < ENCHANTMENTS.size(); ++i) {
        const auto id = static_cast<EnchantmentId>(i);
        const auto& definition = ENCHANTMENTS[i];
        require(keys.insert(definition.key).second, "unique stable keys");
        EnchantmentLevels levels;
        require(levels.set(id, definition.maximumLevel), "maximum level accepted");
        require(!levels.set(id, definition.maximumLevel + 1), "overlevel rejected");
        require(levels.level(id) == definition.maximumLevel, "failed write preserves level");
        require(levels.set(id, 0) && levels.empty(), "zero clears level");
        for (size_t j = 0; j < ENCHANTMENTS.size(); ++j)
            require(compatibleEnchantments(id, static_cast<EnchantmentId>(j)) ==
                compatibleEnchantments(static_cast<EnchantmentId>(j), id), "symmetric exclusivity");
    }
    EnchantmentLevels conflict;
    conflict.set(EnchantmentId::Mending, 1); conflict.set(EnchantmentId::Infinity, 1);
    require(!conflict.compatible(), "mending excludes infinity");
    require(!conflict.set(EnchantmentId::Count, 1), "invalid ID rejected");
    ItemComponents a, b;
    require(a.empty() && a == b && a.fingerprint() == b.fingerprint(), "default canonical values");
    require(a.setCustomName("pickaxe"), "ASCII name accepted");
    const auto before = a.fingerprint();
    require(!a.setCustomName(std::string(51, 'x')), "long name rejected");
    require(a.fingerprint() == before, "invalid name preserves original");
    require(!ItemComponents::validName(std::string("a\0b", 3)), "embedded NUL rejected");
    require(!ItemComponents::validName("\xc0\xaf"), "overlong UTF8 rejected");
    require(!ItemComponents::validName("\xed\xa0\x80"), "surrogate rejected");
    require(!ItemComponents::validName("\xf4\x90\x80\x80"), "out of range scalar rejected");
    require(!ItemComponents::validName("\xe4\xb8"), "truncated UTF8 rejected");
    require(ItemComponents::validName("\xe4\xb8\xad"), "CJK accepted");
    b = a; b.enchantments.set(EnchantmentId::Unbreaking, 3);
    require(!(a == b) && a.fingerprint() != b.fingerprint(), "enchantment affects identity");
    b = a; b.storedEnchantments.set(EnchantmentId::Unbreaking, 3);
    require(!(a == b) && a.fingerprint() != b.fingerprint(), "stored enchantment affects identity");
    b = a; b.repairCost = 1;
    require(!(a == b) && a.fingerprint() != b.fingerprint(), "repair history affects identity");
}
