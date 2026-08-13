#include "game/InventoryModel.h"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

}

int main() {
    require(static_cast<uint16_t>(ItemId::GRASS_BLOCK) ==
            static_cast<uint8_t>(BlockId::GRASS),
            "legacy block item ids remain aligned");
    require(static_cast<uint16_t>(ItemId::GUNPOWDER) == 126 &&
            static_cast<uint16_t>(ItemId::COW_SPAWN_EGG) == 127 &&
            static_cast<uint16_t>(ItemId::BLASTLING_SPAWN_EGG) == 134,
            "spawn eggs did not append after stable serialized item ids");
    require(itemForBlock(BlockId::DIAMOND_ORE) == ItemId::DIAMOND_ORE,
            "block maps to its inventory item");
    require(static_cast<uint8_t>(BlockId::ACACIA_SAPLING) == 63 &&
            static_cast<uint8_t>(BlockId::SNOW_LAYER) == 64 &&
            static_cast<uint8_t>(BlockId::FIRE) == 65,
            "weather blocks did not append after stable serialized ids");
    require(getBlockProps(BlockId::SNOW_LAYER).shape == RenderShape::SnowLayer &&
            !isSolid(BlockId::SNOW_LAYER) && !isSolid(BlockId::FIRE),
            "weather block geometry or collision properties are invalid");
    require(fireEncouragement(BlockId::LEAVES) == 30 &&
            burnOdds(BlockId::LEAVES) == 60 &&
            !isFlammable(BlockId::STONE),
            "fire flammability registry does not match the weather rules");
    require(getItemProps(ItemId::IRON_PICKAXE).maxStack == 1,
            "tools are non-stackable");
    require(getItemProps(ItemId::IRON_PICKAXE).maxDurability == 250,
            "iron tool durability follows the frozen ruleset");
    require(getItemProps(ItemId::STEAK).food == 8,
            "food values are represented in the registry");
    const auto creativeItems = creativeInventoryItems();
    require(creativeItems.size() == static_cast<size_t>(ItemId::COUNT) - 1,
            "creative inventory does not expose every registered item");
    require(creativeItems.front() == ItemId::GRASS_BLOCK &&
            creativeItems.back() == ItemId::BLASTLING_SPAWN_EGG,
            "creative inventory ordering does not follow stable item ids");
    require(getItemProps(ItemId::COW_SPAWN_EGG).kind == ItemKind::SpawnEgg &&
            getItemProps(ItemId::COW_SPAWN_EGG).spawnEggMob == SpawnEggMob::Cow &&
            getItemProps(ItemId::BLASTLING_SPAWN_EGG).spawnEggMob ==
                SpawnEggMob::Blastling,
            "spawn eggs are not registered with stable mob mappings");

    InventoryModel inventory;
    for (const auto& stack : inventory.storage())
        require(stack.empty(), "new player inventory starts with an empty hotbar and backpack");
    require(inventory.add({ItemId::COAL, 64, 0}) == 0,
            "full material stack is accepted");
    require(inventory.add({ItemId::COAL, 17, 0}) == 0,
            "overflow opens a second stack");
    require(inventory.count(ItemId::COAL) == 81,
            "inventory counts across stacks");
    require(inventory.remove(ItemId::COAL, 70),
            "atomic removal succeeds when enough items exist");
    require(inventory.count(ItemId::COAL) == 11,
            "removal spans stacks");
    require(!inventory.remove(ItemId::COAL, 12),
            "atomic removal rejects insufficient inventory");
    require(inventory.count(ItemId::COAL) == 11,
            "failed removal does not mutate inventory");

    for (size_t i = 0; i < InventoryModel::STORAGE_SIZE; ++i) {
        inventory.slot(i) = {ItemId::DIAMOND_PICKAXE, 1, 0};
    }
    require(inventory.add({ItemId::DIAMOND_PICKAXE, 1, 0}) == 1,
            "full inventory returns unaccepted items");

    std::cout << "Inventory and item registry tests passed\n";
    return 0;
}
