#include "game/CombatRules.h"
#include "game/Item.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void near(float actual, float expected, const char* message) {
    require(std::abs(actual - expected) < 0.0001f, message);
}
}

int main() {
    near(CombatRules::attackCooldownTicks(1.6f), 12.5f,
         "sword cooldown uses Java attack speed");
    near(CombatRules::attackStrength(0.0f, 4.0f), 0.1f,
         "attack strength includes half-tick offset");
    near(CombatRules::scaledAttackDamage(10.0f, 0.0f), 2.0f,
         "uncharged attacks deal twenty percent damage");
    near(CombatRules::scaledAttackDamage(10.0f, 0.5f), 4.0f,
         "attack damage follows the quadratic curve");
    near(CombatRules::scaledAttackDamage(10.0f, 1.0f), 10.0f,
         "fully charged attacks deal full damage");
    require(!CombatRules::strongAttack(0.9f) &&
            CombatRules::strongAttack(0.9001f),
            "strong attack threshold is strictly above 0.9");
    require(CombatRules::criticalAttack(true, true, false, false, false) &&
            !CombatRules::criticalAttack(true, true, false, false, true),
            "critical attack requires falling without sprinting");
    require(CombatRules::sweepingAttack(true, true, true, false, false) &&
            !CombatRules::sweepingAttack(true, true, true, true, false),
            "sweep requires a grounded non-sprinting sword attack");
    near(CombatRules::armorDamage(10.0f, 20.0f, 0.0f), 4.0f,
         "armor reduction uses the 1.9 formula");
    require(CombatRules::armorDurabilityDamage(12.0f) == 3 &&
            CombatRules::shieldDurabilityDamage(2.9f) == 0 &&
            CombatRules::shieldDurabilityDamage(3.0f) == 4,
            "armor and shield durability formulas match Java rules");
    require(CombatRules::sourceInFront({0, 0, 0}, {0, 0, 1}, {0, 0, 2}) &&
            !CombatRules::sourceInFront({0, 0, 0}, {0, 0, 1}, {0, 0, -2}),
            "shield front hemisphere is directional");

    struct Expected { ItemId id; float damage; float speed; };
    const Expected tools[] = {
        {ItemId::WOODEN_SWORD,4,1.6f},{ItemId::STONE_SWORD,5,1.6f},
        {ItemId::IRON_SWORD,6,1.6f},{ItemId::GOLDEN_SWORD,4,1.6f},
        {ItemId::DIAMOND_SWORD,7,1.6f},
        {ItemId::WOODEN_AXE,7,.8f},{ItemId::STONE_AXE,9,.8f},
        {ItemId::IRON_AXE,9,.9f},{ItemId::GOLDEN_AXE,7,1.0f},
        {ItemId::DIAMOND_AXE,9,1.0f},
        {ItemId::WOODEN_PICKAXE,2,1.2f},{ItemId::STONE_PICKAXE,3,1.2f},
        {ItemId::IRON_PICKAXE,4,1.2f},{ItemId::GOLDEN_PICKAXE,2,1.2f},
        {ItemId::DIAMOND_PICKAXE,5,1.2f},
        {ItemId::WOODEN_SHOVEL,2.5f,1},{ItemId::STONE_SHOVEL,3.5f,1},
        {ItemId::IRON_SHOVEL,4.5f,1},{ItemId::GOLDEN_SHOVEL,2.5f,1},
        {ItemId::DIAMOND_SHOVEL,5.5f,1},
        {ItemId::WOODEN_HOE,1,1},{ItemId::STONE_HOE,1,2},
        {ItemId::IRON_HOE,1,3},{ItemId::GOLDEN_HOE,1,1},
        {ItemId::DIAMOND_HOE,1,4}
    };
    for (const auto& expected : tools) {
        const auto& props = getItemProps(expected.id);
        near(props.attackDamage, expected.damage, "tool attack damage table");
        near(props.attackSpeed, expected.speed, "tool attack speed table");
    }
    std::cout << "Combat rules tests passed\n";
}
