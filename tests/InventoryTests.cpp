#include "game/InventoryModel.h"

#include <algorithm>
#include <cmath>
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
    require(static_cast<uint8_t>(BlockId::WHITE_BED_HEAD_WEST) == 103 &&
            static_cast<uint8_t>(BlockId::AETHER_GRASS) == 106 &&
            static_cast<uint8_t>(BlockId::COUNT) == 166 &&
            getBlockProps(BlockId::WHITE_BED).shape == RenderShape::Bed &&
            std::abs(blockCollisionHeight(BlockId::WHITE_BED) - 9.0f / 16.0f) <
                0.0001f,
            "bed states did not append safely or use the low bed bounds");
    for (BedDirection direction : {BedDirection::North, BedDirection::East,
                                   BedDirection::South, BedDirection::West}) {
        const BlockId foot = bedBlock(BedPart::Foot, direction);
        const BlockId head = bedBlock(BedPart::Head, direction);
        require(isBed(foot) && isBed(head) &&
                    bedPartnerOffset(foot) == bedDirectionOffset(direction) &&
                    bedPartnerOffset(head) == -bedDirectionOffset(direction) &&
                    itemForBlock(foot) == ItemId::WHITE_BED &&
                    itemForBlock(head) == ItemId::WHITE_BED,
                "bed direction, partner, or inventory mapping is inconsistent");
    }
    require(bedDirectionFromHorizontal({0.0f, -1.0f}) == BedDirection::North &&
            bedDirectionFromHorizontal({1.0f, 0.0f}) == BedDirection::East &&
            bedDirectionFromHorizontal({0.0f, 1.0f}) == BedDirection::South &&
            bedDirectionFromHorizontal({-1.0f, 0.0f}) == BedDirection::West,
            "horizontal player facing does not map to all four bed directions");
    require(shouldRenderCubeFace(BlockId::STONE, BlockId::WHITE_BED),
            "a partial-height bed incorrectly hides its solid neighbor face");
    require(!pointInsideBlockCollision(BlockId::AIR, 0.0f) &&
            pointInsideBlockCollision(BlockId::WHITE_BED, 0.5f) &&
            !pointInsideBlockCollision(BlockId::WHITE_BED, 0.75f),
            "shared point collision does not match the low bed bounds");
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
            creativeItems[static_cast<size_t>(ItemId::BLASTLING_SPAWN_EGG) - 1] ==
                ItemId::BLASTLING_SPAWN_EGG &&
            creativeItems[static_cast<size_t>(ItemId::AETHER_GRASS) - 1] ==
                ItemId::AETHER_GRASS &&
            creativeItems.back() == ItemId::CLOUDSTONE_STAIRS,
            "creative inventory ordering does not follow stable item ids");

    // Minecraft-style creative tabs: every registered item belongs to exactly
    // one category and the category list agrees with the catalog ordering.
    size_t categoryCounts[static_cast<size_t>(CreativeItemCategory::Count)] = {};
    size_t categorized = 0;
    for (uint16_t raw = 1; raw < static_cast<uint16_t>(ItemId::COUNT); ++raw) {
        const ItemId id = static_cast<ItemId>(raw);
        const auto category = creativeInventoryCategory(id);
        require(category != CreativeItemCategory::Count,
                "every registered item has a creative category");
        const auto& bucket = creativeInventoryItemsIn(category);
        require(std::find(bucket.begin(), bucket.end(), id) != bucket.end(),
                "category membership agrees with its item list");
        categoryCounts[static_cast<size_t>(category)]++;
        categorized++;
    }
    require(categorized == creativeItems.size(),
            "creative categories cover exactly the full creative catalog");
    require(categoryCounts[static_cast<size_t>(
                CreativeItemCategory::BuildingBlocks)] == 50 &&
            categoryCounts[static_cast<size_t>(
                CreativeItemCategory::Nature)] == 27 &&
            categoryCounts[static_cast<size_t>(
                CreativeItemCategory::Functional)] == 7 &&
            categoryCounts[static_cast<size_t>(
                CreativeItemCategory::Tools)] == 21 &&
            categoryCounts[static_cast<size_t>(
                CreativeItemCategory::Combat)] == 24 &&
            categoryCounts[static_cast<size_t>(
                CreativeItemCategory::Food)] == 10 &&
            categoryCounts[static_cast<size_t>(
                CreativeItemCategory::Materials)] == 15 &&
            categoryCounts[static_cast<size_t>(
                CreativeItemCategory::SpawnEggs)] == 8,
            "creative category sizes do not match the tab assignment");
    require(creativeInventoryCategory(ItemId::STONE) ==
                CreativeItemCategory::BuildingBlocks &&
            creativeInventoryCategory(ItemId::TORCH) ==
                CreativeItemCategory::Functional &&
            creativeInventoryCategory(ItemId::DIAMOND_SWORD) ==
                CreativeItemCategory::Combat &&
            creativeInventoryCategory(ItemId::COW_SPAWN_EGG) ==
                CreativeItemCategory::SpawnEggs &&
            creativeInventoryCategory(ItemId::FLOWER) ==
                CreativeItemCategory::Nature &&
            creativeInventoryCategory(ItemId::BREAD) ==
                CreativeItemCategory::Food &&
            creativeInventoryCategory(ItemId::DIAMOND) ==
                CreativeItemCategory::Materials &&
            creativeInventoryCategory(ItemId::IRON_PICKAXE) ==
                CreativeItemCategory::Tools,
            "representative items map to their Minecraft-style tabs");
    for (size_t index = 0;
         index < static_cast<size_t>(CreativeItemCategory::Count); ++index) {
        const auto& info = creativeCategoryInfo(
            static_cast<CreativeItemCategory>(index));
        require(info.category == static_cast<CreativeItemCategory>(index) &&
                info.localizationKey[0] != '\0' &&
                getItemProps(info.icon).maxStack > 0,
                "every creative tab has a valid icon and localization key");
    }
    require(itemForBlock(BlockId::AETHER_GRASS) == ItemId::AETHER_GRASS &&
                itemForBlock(BlockId::STARFLOWER) == ItemId::STARFLOWER &&
                itemForBlock(BlockId::CLOUD_BLOOM) == ItemId::CLOUD_BLOOM &&
                itemForBlock(BlockId::GLOWSHROOM) == ItemId::GLOWSHROOM &&
                getItemProps(ItemId::STAR_CRYSTAL).maxStack == 64 &&
                getFaceTexture(BlockId::AETHER_GRASS, FaceDir::TOP) ==
                    BlockTexture::AetherGrassTop &&
                getFaceTexture(BlockId::SKYROOT_WOOD, FaceDir::TOP) ==
                    BlockTexture::SkyrootLogTop &&
                getFaceTexture(BlockId::CLOUD_BLOOM, FaceDir::TOP) ==
                    BlockTexture::CloudBloom &&
                getFaceTexture(BlockId::GLOWSHROOM, FaceDir::TOP) ==
                    BlockTexture::Glowshroom &&
                getLightEmission(BlockId::STAR_CRYSTAL) == 8 &&
                getLightEmission(BlockId::STARFLOWER) == 5 &&
                getLightEmission(BlockId::CLOUD_BLOOM) == 4 &&
                getLightEmission(BlockId::GLOWSHROOM) == 6,
            "Heaven materials lack inventory, atlas, or light mappings");
    for (uint8_t material=0;
         material<static_cast<uint8_t>(ArchitecturalMaterial::Count);++material) {
        for (BlockHalf half : {BlockHalf::Bottom,BlockHalf::Top}) {
            const auto family=static_cast<ArchitecturalMaterial>(material);
            const BlockId slab=slabBlock(family,half);
            ArchitecturalBlockState decoded;
            require(decodeArchitecturalBlock(slab,decoded)&&
                        decoded.material==family&&decoded.half==half&&
                        decoded.shape==RenderShape::Slab&&
                        blockCollisionBoxes(slab).count==1&&
                        itemForBlock(slab)!=ItemId::EMPTY,
                    "architectural slab state failed round-trip or collision mapping");
            for (BedDirection direction : {BedDirection::North,BedDirection::East,
                                            BedDirection::South,BedDirection::West}) {
                const BlockId stair=stairBlock(family,half,direction);
                require(decodeArchitecturalBlock(stair,decoded)&&
                            decoded.material==family&&decoded.half==half&&
                            decoded.direction==direction&&
                            decoded.shape==RenderShape::Stair&&
                            blockCollisionBoxes(stair).count==2&&
                            itemForBlock(stair)!=ItemId::EMPTY,
                        "architectural stair state failed round-trip or collision mapping");
            }
        }
    }
    require(isFullCollisionBlock(BlockId::COBBLESTONE) &&
                !isFullCollisionBlock(BlockId::COBBLESTONE_SLAB_TOP) &&
                !isFullCollisionBlock(BlockId::COBBLESTONE_STAIRS_TOP_NORTH),
            "partial architectural shapes were mistaken for full support blocks");
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
