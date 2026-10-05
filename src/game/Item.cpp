#include "plugins/ContentRegistry.h"
#include "game/Item.h"

#include <array>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace {

constexpr size_t itemCount = static_cast<size_t>(ItemId::COUNT);

std::array<ItemProperties, itemCount> buildRegistry() {
    std::array<ItemProperties, itemCount> items{};
    items[0] = {"Empty", ItemKind::Material, 0};

    const std::array<const char*, 35> blockNames = {{
        "Grass Block", "Dirt", "Stone", "Oak Log", "Oak Leaves", "Sand",
        "Bedrock", "Water", "Snow", "Oak Planks", "Deepslate", "Cactus",
        "Coal Ore", "Iron Ore", "Gold Ore", "Diamond Ore", "Lava", "Ice",
        "Gravel", "Clay", "Red Sand", "Terracotta", "Podzol", "Moss",
        "Tall Grass", "Flower", "Reeds", "Birch Log", "Birch Leaves",
        "Spruce Log", "Spruce Leaves", "Jungle Log", "Jungle Leaves",
        "Acacia Log", "Acacia Leaves"
    }};
    for (size_t i = 0; i < blockNames.size(); ++i) {
        items[i + 1] = {blockNames[i], ItemKind::Block, 64, 0,
                        ToolKind::None, ToolTier::None, 0.0f, 0.0f, 0, 0.0f,
                        static_cast<BlockId>(i + 1)};
    }

    auto set = [&](ItemId id, ItemProperties props) {
        items[static_cast<size_t>(id)] = props;
    };
    set(ItemId::COBBLESTONE, {"Cobblestone", ItemKind::Block, 64, 0,
                              ToolKind::None, ToolTier::None, 0, 0, 0, 0,
                              BlockId::COBBLESTONE});
    set(ItemId::STICK, {"Stick"});
    set(ItemId::COAL, {"Coal"});
    set(ItemId::RAW_IRON, {"Raw Iron"});
    set(ItemId::IRON_INGOT, {"Iron Ingot"});
    set(ItemId::RAW_GOLD, {"Raw Gold"});
    set(ItemId::GOLD_INGOT, {"Gold Ingot"});
    set(ItemId::DIAMOND, {"Diamond"});
    set(ItemId::STRING, {"String"});
    set(ItemId::FEATHER, {"Feather"});
    set(ItemId::LEATHER, {"Leather"});
    set(ItemId::BONE, {"Bone"});
    set(ItemId::ARROW, {"Arrow"});
    set(ItemId::WHEAT_SEEDS, {"Wheat Seeds"});
    set(ItemId::WHEAT, {"Wheat"});
    set(ItemId::BREAD, {"Bread", ItemKind::Food, 64, 0, ToolKind::None,
                        ToolTier::None, 0, 0, 5, 6.0f});
    set(ItemId::RAW_BEEF, {"Raw Beef", ItemKind::Food, 64, 0, ToolKind::None,
                           ToolTier::None, 0, 0, 3, 1.8f});
    set(ItemId::STEAK, {"Steak", ItemKind::Food, 64, 0, ToolKind::None,
                        ToolTier::None, 0, 0, 8, 12.8f});
    set(ItemId::RAW_PORKCHOP, {"Raw Porkchop", ItemKind::Food, 64, 0,
                               ToolKind::None, ToolTier::None, 0, 0, 3, 1.8f});
    set(ItemId::COOKED_PORKCHOP, {"Cooked Porkchop", ItemKind::Food, 64, 0,
                                  ToolKind::None, ToolTier::None, 0, 0, 8, 12.8f});
    set(ItemId::RAW_CHICKEN, {"Raw Chicken", ItemKind::Food, 64, 0,
                              ToolKind::None, ToolTier::None, 0, 0, 2, 1.2f});
    set(ItemId::COOKED_CHICKEN, {"Cooked Chicken", ItemKind::Food, 64, 0,
                                 ToolKind::None, ToolTier::None, 0, 0, 6, 7.2f});
    set(ItemId::MUTTON, {"Raw Mutton", ItemKind::Food, 64, 0, ToolKind::None,
                         ToolTier::None, 0, 0, 2, 1.2f});
    set(ItemId::COOKED_MUTTON, {"Cooked Mutton", ItemKind::Food, 64, 0,
                                ToolKind::None, ToolTier::None, 0, 0, 6, 9.6f});
    set(ItemId::ROTTEN_FLESH, {"Rotten Flesh", ItemKind::Food, 64, 0,
                               ToolKind::None, ToolTier::None, 0, 0, 4, 0.8f});
    set(ItemId::WHITE_WOOL, {"White Wool"});

    set(ItemId::CRAFTING_TABLE, {"Crafting Table", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::CRAFTING_TABLE});
    set(ItemId::FURNACE, {"Furnace", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::FURNACE});
    set(ItemId::CHEST, {"Chest", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::CHEST});
    set(ItemId::TORCH, {"Torch", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::TORCH});
    set(ItemId::WHITE_BED, {"White Bed", ItemKind::Block, 1, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::WHITE_BED});
    set(ItemId::FARMLAND, {"Farmland", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::FARMLAND});

    struct TierData { ToolTier tier; uint16_t durability; };
    const std::array<TierData, 5> tiers = {{
        {ToolTier::Wood, 59}, {ToolTier::Stone, 131}, {ToolTier::Iron, 250},
        {ToolTier::Gold, 32}, {ToolTier::Diamond, 1561}
    }};
    const ItemId firstTools[] = {
        ItemId::WOODEN_PICKAXE, ItemId::STONE_PICKAXE, ItemId::IRON_PICKAXE,
        ItemId::GOLDEN_PICKAXE, ItemId::DIAMOND_PICKAXE
    };
    const char* tierNames[] = {"Wooden", "Stone", "Iron", "Golden", "Diamond"};
    for (size_t tierIndex = 0; tierIndex < tiers.size(); ++tierIndex) {
        const uint16_t base = static_cast<uint16_t>(firstTools[tierIndex]);
        const ToolKind kinds[] = {ToolKind::Pickaxe, ToolKind::Axe, ToolKind::Shovel,
                                  ToolKind::Hoe, ToolKind::Sword};
        const char* names[] = {" Pickaxe", " Axe", " Shovel", " Hoe", " Sword"};
        for (uint16_t offset = 0; offset < 5; ++offset) {
            ItemProperties props;
            props.name = names[offset];
            props.kind = offset == 4 ? ItemKind::Weapon : ItemKind::Tool;
            props.maxStack = 1;
            props.maxDurability = tiers[tierIndex].durability;
            props.tool = kinds[offset];
            props.tier = tiers[tierIndex].tier;
            static constexpr float damages[5][5] = {
                {2.0f, 7.0f, 2.5f, 1.0f, 4.0f},
                {3.0f, 9.0f, 3.5f, 1.0f, 5.0f},
                {4.0f, 9.0f, 4.5f, 1.0f, 6.0f},
                {2.0f, 7.0f, 2.5f, 1.0f, 4.0f},
                {5.0f, 9.0f, 5.5f, 1.0f, 7.0f}
            };
            static constexpr float speeds[5][5] = {
                {1.2f, 0.8f, 1.0f, 1.0f, 1.6f},
                {1.2f, 0.8f, 1.0f, 2.0f, 1.6f},
                {1.2f, 0.9f, 1.0f, 3.0f, 1.6f},
                {1.2f, 1.0f, 1.0f, 1.0f, 1.6f},
                {1.2f, 1.0f, 1.0f, 4.0f, 1.6f}
            };
            props.attackDamage = damages[tierIndex][offset];
            props.attackSpeed = speeds[tierIndex][offset];
            props.name = std::string(tierNames[tierIndex]) + names[offset];
            set(static_cast<ItemId>(base + offset), props);
        }
    }

    set(ItemId::FISHING_ROD, {"Fishing Rod", ItemKind::Tool, 1, 64, ToolKind::FishingRod});
    set(ItemId::RAW_COD, {"Raw Cod", ItemKind::Food, 64, 0, ToolKind::None,
                         ToolTier::None, 0, 0, 2, .4f});
    set(ItemId::RAW_SALMON, {"Raw Salmon", ItemKind::Food, 64, 0, ToolKind::None,
                            ToolTier::None, 0, 0, 2, .4f});
    set(ItemId::COOKED_COD, {"Cooked Cod", ItemKind::Food, 64, 0, ToolKind::None,
                            ToolTier::None, 0, 0, 5, 6.0f});
    set(ItemId::COOKED_SALMON, {"Cooked Salmon", ItemKind::Food, 64, 0, ToolKind::None,
                               ToolTier::None, 0, 0, 6, 9.6f});
    set(ItemId::BOW, {"Bow", ItemKind::Weapon, 1, 384, ToolKind::Bow});
    set(ItemId::SHIELD, {"Shield", ItemKind::Weapon, 1, 336, ToolKind::Shield});

    const ItemId armorStart[] = {
        ItemId::LEATHER_HELMET, ItemId::IRON_HELMET,
        ItemId::GOLDEN_HELMET, ItemId::DIAMOND_HELMET
    };
    const uint16_t armorDurability[][4] = {
        {55, 80, 75, 65}, {165, 240, 225, 195},
        {77, 112, 105, 91}, {363, 528, 495, 429}
    };
    const char* armorNames[][4] = {
        {"Leather Helmet", "Leather Chestplate", "Leather Leggings", "Leather Boots"},
        {"Iron Helmet", "Iron Chestplate", "Iron Leggings", "Iron Boots"},
        {"Golden Helmet", "Golden Chestplate", "Golden Leggings", "Golden Boots"},
        {"Diamond Helmet", "Diamond Chestplate", "Diamond Leggings", "Diamond Boots"}
    };
    for (size_t tier = 0; tier < 4; ++tier) {
        for (uint16_t slot = 0; slot < 4; ++slot) {
            set(static_cast<ItemId>(static_cast<uint16_t>(armorStart[tier]) + slot),
                {armorNames[tier][slot], ItemKind::Armor, 1, armorDurability[tier][slot]});
        }
    }
    set(ItemId::FLINT, {"Flint"});
    set(ItemId::OAK_SAPLING, {"Oak Sapling", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::OAK_SAPLING});
    set(ItemId::BIRCH_SAPLING, {"Birch Sapling", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::BIRCH_SAPLING});
    set(ItemId::SPRUCE_SAPLING, {"Spruce Sapling", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::SPRUCE_SAPLING});
    set(ItemId::JUNGLE_SAPLING, {"Jungle Sapling", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::JUNGLE_SAPLING});
    set(ItemId::ACACIA_SAPLING, {"Acacia Sapling", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::ACACIA_SAPLING});
    set(ItemId::GLASS, {"Glass", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::GLASS});
    set(ItemId::TNT, {"TNT", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::TNT});
    set(ItemId::OBSIDIAN, {"Obsidian", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::OBSIDIAN});
    set(ItemId::DANDELION, {"Dandelion", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::DANDELION});
    set(ItemId::BLUE_ORCHID, {"Blue Orchid", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::BLUE_ORCHID});
    set(ItemId::ALLIUM, {"Allium", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::ALLIUM});
    set(ItemId::OXEYE_DAISY, {"Oxeye Daisy", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::OXEYE_DAISY});
    set(ItemId::SUNFLOWER, {"Sunflower", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::SUNFLOWER_BOTTOM});
    set(ItemId::FLINT_AND_STEEL, {"Flint and Steel", ItemKind::Tool, 1, 64});
    set(ItemId::GUNPOWDER, {"Gunpowder"});

    const std::array<std::pair<ItemId, SpawnEggMob>, 10> spawnEggs{{
        {ItemId::COW_SPAWN_EGG, SpawnEggMob::Cow},
        {ItemId::PIG_SPAWN_EGG, SpawnEggMob::Pig},
        {ItemId::SHEEP_SPAWN_EGG, SpawnEggMob::Sheep},
        {ItemId::CHICKEN_SPAWN_EGG, SpawnEggMob::Chicken},
        {ItemId::ZOMBIE_SPAWN_EGG, SpawnEggMob::Zombie},
        {ItemId::SKELETON_SPAWN_EGG, SpawnEggMob::Skeleton},
        {ItemId::SPIDER_SPAWN_EGG, SpawnEggMob::Spider},
        {ItemId::BLASTLING_SPAWN_EGG, SpawnEggMob::Blastling},
        {ItemId::VILLAGER_SPAWN_EGG, SpawnEggMob::Villager},
        {ItemId::ZOMBIE_VILLAGER_SPAWN_EGG, SpawnEggMob::ZombieVillager}
    }};
    const std::array<const char*, 10> spawnEggNames{{
        "Cow Spawn Egg", "Pig Spawn Egg", "Sheep Spawn Egg",
        "Chicken Spawn Egg", "Zombie Spawn Egg", "Skeleton Spawn Egg",
        "Spider Spawn Egg", "Blastling Spawn Egg", "Villager Spawn Egg",
        "Zombie Villager Spawn Egg"
    }};
    for (size_t index = 0; index < spawnEggs.size(); ++index) {
        ItemProperties properties{
            spawnEggNames[index], ItemKind::SpawnEgg, 64};
        properties.spawnEggMob = spawnEggs[index].second;
        set(spawnEggs[index].first, std::move(properties));
    }

    const std::array<std::pair<ItemId, BlockId>, 8> naturalBlocks{{
        {ItemId::LIMESTONE, BlockId::LIMESTONE},
        {ItemId::BASALT, BlockId::BASALT}, {ItemId::TUFF, BlockId::TUFF},
        {ItemId::COARSE_DIRT, BlockId::COARSE_DIRT}, {ItemId::MUD, BlockId::MUD},
        {ItemId::PACKED_ICE, BlockId::PACKED_ICE},
        {ItemId::BLACK_SAND, BlockId::BLACK_SAND},
        {ItemId::GRANITE, BlockId::GRANITE}
    }};
    const std::array<const char*, 8> naturalNames{{
        "Limestone", "Basalt", "Tuff", "Coarse Dirt", "Mud",
        "Packed Ice", "Black Sand", "Granite"
    }};
    for (size_t i = 0; i < naturalBlocks.size(); ++i) {
        set(naturalBlocks[i].first,
            {naturalNames[i], ItemKind::Block, 64, 0, ToolKind::None,
             ToolTier::None, 0, 0, 0, 0, naturalBlocks[i].second});
    }

    const std::array<std::pair<ItemId, BlockId>, 10> heavenBlocks{{
        {ItemId::AETHER_GRASS, BlockId::AETHER_GRASS},
        {ItemId::AETHER_SOIL, BlockId::AETHER_SOIL},
        {ItemId::CLOUDSTONE, BlockId::CLOUDSTONE},
        {ItemId::SUNSTONE, BlockId::SUNSTONE},
        {ItemId::SKYROOT_LOG, BlockId::SKYROOT_WOOD},
        {ItemId::SKYROOT_LEAVES, BlockId::SKYROOT_LEAVES},
        {ItemId::STAR_CRYSTAL, BlockId::STAR_CRYSTAL},
        {ItemId::STARFLOWER, BlockId::STARFLOWER},
        {ItemId::CLOUD_BLOOM, BlockId::CLOUD_BLOOM},
        {ItemId::GLOWSHROOM, BlockId::GLOWSHROOM}
    }};
    const std::array<const char*, 10> heavenNames{{
        "Aether Grass", "Aether Soil", "Cloudstone", "Sunstone",
        "Skyroot Log", "Skyroot Leaves", "Star Crystal", "Starflower",
        "Cloud Bloom", "Glowshroom"
    }};
    for (size_t i = 0; i < heavenBlocks.size(); ++i) {
        set(heavenBlocks[i].first,
            {heavenNames[i], ItemKind::Block, 64, 0, ToolKind::None,
             ToolTier::None, 0, 0, 0, 0, heavenBlocks[i].second});
    }

    const std::array<std::pair<ItemId, BlockId>, 10> architecturalBlocks{{
        {ItemId::OAK_PLANKS_SLAB, BlockId::PLANKS_SLAB_BOTTOM},
        {ItemId::OAK_PLANKS_STAIRS, BlockId::PLANKS_STAIRS_BOTTOM_NORTH},
        {ItemId::COBBLESTONE_SLAB, BlockId::COBBLESTONE_SLAB_BOTTOM},
        {ItemId::COBBLESTONE_STAIRS, BlockId::COBBLESTONE_STAIRS_BOTTOM_NORTH},
        {ItemId::TERRACOTTA_SLAB, BlockId::TERRACOTTA_SLAB_BOTTOM},
        {ItemId::TERRACOTTA_STAIRS, BlockId::TERRACOTTA_STAIRS_BOTTOM_NORTH},
        {ItemId::SUNSTONE_SLAB, BlockId::SUNSTONE_SLAB_BOTTOM},
        {ItemId::SUNSTONE_STAIRS, BlockId::SUNSTONE_STAIRS_BOTTOM_NORTH},
        {ItemId::CLOUDSTONE_SLAB, BlockId::CLOUDSTONE_SLAB_BOTTOM},
        {ItemId::CLOUDSTONE_STAIRS, BlockId::CLOUDSTONE_STAIRS_BOTTOM_NORTH},
    }};
    const std::array<const char*, 10> architecturalNames{{
        "Oak Planks Slab", "Oak Planks Stairs", "Cobblestone Slab",
        "Cobblestone Stairs", "Terracotta Slab", "Terracotta Stairs",
        "Sunstone Slab", "Sunstone Stairs", "Cloudstone Slab",
        "Cloudstone Stairs"
    }};
    for (size_t i = 0; i < architecturalBlocks.size(); ++i) {
        set(architecturalBlocks[i].first,
            {architecturalNames[i], ItemKind::Block, 64, 0, ToolKind::None,
             ToolTier::None, 0, 0, 0, 0, architecturalBlocks[i].second});
    }

    set(ItemId::EMERALD, {"Emerald"});
    const std::array<std::pair<ItemId, BlockId>, 15> villagerBlocks{{
        {ItemId::EMERALD_ORE, BlockId::EMERALD_ORE},
        {ItemId::DEEPSLATE_EMERALD_ORE, BlockId::DEEPSLATE_EMERALD_ORE},
        {ItemId::COMPOSTER, BlockId::COMPOSTER},
        {ItemId::FLETCHING_TABLE, BlockId::FLETCHING_TABLE},
        {ItemId::LOOM, BlockId::LOOM},
        {ItemId::CAULDRON, BlockId::CAULDRON},
        {ItemId::BLAST_FURNACE, BlockId::BLAST_FURNACE},
        {ItemId::SMITHING_TABLE, BlockId::SMITHING_TABLE},
        {ItemId::GRINDSTONE, BlockId::GRINDSTONE},
        {ItemId::BARREL, BlockId::BARREL},
        {ItemId::LECTERN, BlockId::LECTERN},
        {ItemId::CARTOGRAPHY_TABLE, BlockId::CARTOGRAPHY_TABLE},
        {ItemId::BREWING_STAND, BlockId::BREWING_STAND},
        {ItemId::SMOKER, BlockId::SMOKER},
        {ItemId::STONECUTTER, BlockId::STONECUTTER},

    }};
    const std::array<const char*, 15> villagerBlockNames{{
        "Emerald Ore", "Deepslate Emerald Ore", "Composter",
        "Fletching Table", "Loom", "Cauldron", "Blast Furnace",
        "Smithing Table", "Grindstone", "Barrel", "Lectern", "Cartography Table", "Brewing Stand", "Smoker", "Stonecutter"
    }};
    for (size_t i = 0; i < villagerBlocks.size(); ++i) {
        set(villagerBlocks[i].first,
            {villagerBlockNames[i], ItemKind::Block, 64, 0, ToolKind::None,
             ToolTier::None, 0, 0, 0, 0, villagerBlocks[i].second});
    }

    const std::array<std::pair<ItemId, BlockId>, 7> caveBlocks{{
        {ItemId::DRIPSTONE_BLOCK, BlockId::DRIPSTONE_BLOCK},
        {ItemId::POINTED_DRIPSTONE, BlockId::POINTED_DRIPSTONE_UP},
        {ItemId::CALCITE, BlockId::CALCITE},
        {ItemId::HANGING_ROOTS, BlockId::HANGING_ROOTS},
        {ItemId::GLOW_FERN, BlockId::GLOW_FERN},
        {ItemId::RESONANT_CRYSTAL, BlockId::RESONANT_CRYSTAL},
        {ItemId::SULFUR_CRUST, BlockId::SULFUR_CRUST},
    }};
    const std::array<const char*, 7> caveBlockNames{{
        "Dripstone Block", "Pointed Dripstone", "Calcite", "Hanging Roots",
        "Glow Fern", "Resonant Crystal", "Sulfur Crust"
    }};
    for (size_t i = 0; i < caveBlocks.size(); ++i) {
        set(caveBlocks[i].first,
            {caveBlockNames[i], ItemKind::Block, 64, 0, ToolKind::None,
             ToolTier::None, 0, 0, 0, 0, caveBlocks[i].second});
    }

    set(ItemId::STARSTEP_SCEPTER,
        {"Starstep Scepter", ItemKind::Tool, 1, 192});

    set(ItemId::STONE_BRICKS, {"Stone Bricks", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::STONE_BRICKS});
    set(ItemId::MOSSY_STONE_BRICKS, {"Mossy Stone Bricks", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::MOSSY_STONE_BRICKS});
    set(ItemId::CRACKED_STONE_BRICKS, {"Cracked Stone Bricks", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::CRACKED_STONE_BRICKS});
    set(ItemId::CHISELED_STONE_BRICKS, {"Chiseled Stone Bricks", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::CHISELED_STONE_BRICKS});
    set(ItemId::BRICKS, {"Bricks", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::BRICKS});
    set(ItemId::POLISHED_GRANITE, {"Polished Granite", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::POLISHED_GRANITE});
    set(ItemId::POLISHED_BASALT, {"Polished Basalt", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::POLISHED_BASALT});
    set(ItemId::POLISHED_LIMESTONE, {"Polished Limestone", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::POLISHED_LIMESTONE});
    set(ItemId::POLISHED_TUFF, {"Polished Tuff", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::POLISHED_TUFF});
    set(ItemId::DEEPSLATE_BRICKS, {"Deepslate Bricks", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::DEEPSLATE_BRICKS});
    set(ItemId::SANDSTONE, {"Sandstone", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::SANDSTONE});
    set(ItemId::CUT_SANDSTONE, {"Cut Sandstone", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::CUT_SANDSTONE});
    set(ItemId::SMOOTH_SANDSTONE, {"Smooth Sandstone", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::SMOOTH_SANDSTONE});
    set(ItemId::RED_WOOL, {"Red Wool", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::RED_WOOL});
    set(ItemId::YELLOW_WOOL, {"Yellow Wool", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::YELLOW_WOOL});
    set(ItemId::BLUE_WOOL, {"Blue Wool", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::BLUE_WOOL});
    set(ItemId::GREEN_WOOL, {"Green Wool", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::GREEN_WOOL});
    set(ItemId::BLACK_WOOL, {"Black Wool", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::BLACK_WOOL});
    set(ItemId::CLAY_BALL, {"Clay Ball"});
    set(ItemId::BRICK, {"Brick"});
    set(ItemId::RED_DYE, {"Red Dye"});
    set(ItemId::YELLOW_DYE, {"Yellow Dye"});
    set(ItemId::BLUE_DYE, {"Blue Dye"});
    set(ItemId::GREEN_DYE, {"Green Dye"});
    set(ItemId::BLACK_DYE, {"Black Dye"});
    set(ItemId::BONE_MEAL, {"Bone Meal"});

    set(ItemId::ROOTED_DIRT, {"Rooted Dirt", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::ROOTED_DIRT});
    set(ItemId::LEAF_LITTER_SOIL, {"Leaf Litter Soil", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::LEAF_LITTER_SOIL});
    set(ItemId::PEAT, {"Peat", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::PEAT});
    set(ItemId::SILT, {"Silt", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::SILT});
    set(ItemId::DRY_GRASS_BLOCK, {"Dry Grass Block", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::DRY_GRASS_BLOCK});
    set(ItemId::PERMAFROST, {"Permafrost", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::PERMAFROST});
    set(ItemId::BLUE_ICE, {"Blue Ice", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::BLUE_ICE});
    set(ItemId::SHALE, {"Shale", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::SHALE});
    set(ItemId::RED_SANDSTONE, {"Red Sandstone", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::RED_SANDSTONE});
    set(ItemId::OCHRE_TERRACOTTA, {"Ochre Terracotta", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::OCHRE_TERRACOTTA});
    set(ItemId::WHITE_TERRACOTTA, {"White Terracotta", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::WHITE_TERRACOTTA});
    set(ItemId::VOLCANIC_ASH, {"Volcanic Ash", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::VOLCANIC_ASH});
    set(ItemId::CORAL_ROCK, {"Coral Rock", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::CORAL_ROCK});
    set(ItemId::FERN, {"Fern", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::FERN});
    set(ItemId::DEAD_BUSH, {"Dead Bush", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::DEAD_BUSH});
    set(ItemId::DRY_GRASS, {"Dry Grass", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::DRY_GRASS});
    set(ItemId::BROWN_MUSHROOM, {"Brown Mushroom", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::BROWN_MUSHROOM});
    set(ItemId::RED_MUSHROOM, {"Red Mushroom", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::RED_MUSHROOM});
    set(ItemId::LAVENDER, {"Lavender", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::LAVENDER});
    set(ItemId::BELLFLOWER, {"Bellflower", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::BELLFLOWER});
    set(ItemId::ALPINE_FLOWER, {"Alpine Flower", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::ALPINE_FLOWER});
    set(ItemId::TROPICAL_FLOWER, {"Tropical Flower", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::TROPICAL_FLOWER});
    set(ItemId::CATTAIL, {"Cattail", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::CATTAIL});
    set(ItemId::BEACH_GRASS, {"Beach Grass", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::BEACH_GRASS});
    set(ItemId::ANDESITE, {"Andesite", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::ANDESITE});
    set(ItemId::DIORITE, {"Diorite", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::DIORITE});
    set(ItemId::GNEISS, {"Gneiss", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::GNEISS});
    set(ItemId::MARBLE, {"Marble", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::MARBLE});
    set(ItemId::LATERITE, {"Laterite", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::LATERITE});
    set(ItemId::RED_CLAY, {"Red Clay", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::RED_CLAY});
    set(ItemId::CRACKED_MUD, {"Cracked Mud", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::CRACKED_MUD});
    set(ItemId::SALT_CRUST, {"Salt Crust", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::SALT_CRUST});
    set(ItemId::CLOVER, {"Clover", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::CLOVER});
    set(ItemId::HEATHER, {"Heather", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::HEATHER});
    set(ItemId::WILD_MINT, {"Wild Mint", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::WILD_MINT});
    set(ItemId::NETTLE, {"Nettle", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::NETTLE});
    set(ItemId::DESERT_FLOWER, {"Desert Flower", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::DESERT_FLOWER});
    set(ItemId::SMALL_CACTUS, {"Small Cactus", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::SMALL_CACTUS});
    set(ItemId::REED_FLOWER, {"Reed Flower", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::REED_FLOWER});
    set(ItemId::TUNDRA_MOSS, {"Tundra Moss", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::TUNDRA_MOSS});
    set(ItemId::FALLEN_TWIGS, {"Fallen Twigs", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::FALLEN_TWIGS});
    set(ItemId::JUNGLE_FERN, {"Jungle Fern", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::JUNGLE_FERN});
    set(ItemId::CAVE_MOSS, {"Cave Moss", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::CAVE_MOSS});
    set(ItemId::WET_LIMESTONE, {"Wet Limestone", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::WET_LIMESTONE});
    set(ItemId::GYPSUM, {"Gypsum", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::GYPSUM});
    set(ItemId::AMETHYST_BLOCK, {"Amethyst Block", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::AMETHYST_BLOCK});
    set(ItemId::QUARTZ_BLOCK, {"Quartz Block", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::QUARTZ_BLOCK});
    set(ItemId::IRON_STAINED_ROCK, {"Iron Stained Rock", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::IRON_STAINED_ROCK});
    set(ItemId::SULFUR_ROCK, {"Sulfur Rock", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::SULFUR_ROCK});
    set(ItemId::AMETHYST_CLUSTER, {"Amethyst Cluster", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::AMETHYST_CLUSTER});
    set(ItemId::QUARTZ_CLUSTER, {"Quartz Cluster", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::QUARTZ_CLUSTER});
    set(ItemId::CAVE_GLOWSHROOM, {"Cave Glowshroom", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::CAVE_GLOWSHROOM});


    set(ItemId::MOONSTONE, {"Moonstone", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::MOONSTONE});
    set(ItemId::SKYSTONE, {"Skystone", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::SKYSTONE});
    set(ItemId::AETHER_MOSS, {"Aether Moss", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::AETHER_MOSS});
    set(ItemId::GLIMMER_SILT, {"Glimmer Silt", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::GLIMMER_SILT});
    set(ItemId::STAR_CRYSTAL_ORE, {"Star Crystal Ore", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::STAR_CRYSTAL_ORE});
    set(ItemId::SKYROOT_PLANKS, {"Skyroot Planks", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::SKYROOT_PLANKS});
    set(ItemId::CLOUDSTONE_BRICKS, {"Cloudstone Bricks", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::CLOUDSTONE_BRICKS});
    set(ItemId::SUNSTONE_BRICKS, {"Sunstone Bricks", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::SUNSTONE_BRICKS});
    set(ItemId::MOONSTONE_BRICKS, {"Moonstone Bricks", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::MOONSTONE_BRICKS});
    set(ItemId::STAR_CRYSTAL_LAMP, {"Star Crystal Lamp", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::STAR_CRYSTAL_LAMP});
    set(ItemId::SKY_FERN, {"Sky Fern", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::SKY_FERN});
    set(ItemId::DAWN_BELL, {"Dawn Bell", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::DAWN_BELL});
    set(ItemId::MOONFLOWER, {"Moonflower", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::MOONFLOWER});
    set(ItemId::GLIMMER_REED, {"Glimmer Reed", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::GLIMMER_REED});
    set(ItemId::CLOUDBERRY_BUSH, {"Cloudberry Bush", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::CLOUDBERRY_BUSH});
    set(ItemId::HANGING_CLOUD_VINE, {"Hanging Cloud Vine", ItemKind::Block, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 0, 0, BlockId::HANGING_CLOUD_VINE});
    set(ItemId::STAR_CRYSTAL_SHARD, {"Star Crystal Shard"});
    set(ItemId::CLOUDBERRY, {"Cloudberry", ItemKind::Food, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 2, 0.4f});
    set(ItemId::ROASTED_CLOUDBERRY, {"Roasted Cloudberry", ItemKind::Food, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 4, 1.2f});
    set(ItemId::CLOUDBERRY_BREAD, {"Cloudberry Bread", ItemKind::Food, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 7, 6.0f});
    set(ItemId::ROASTED_GLOWSHROOM, {"Roasted Glowshroom", ItemKind::Food, 64, 0,
        ToolKind::None, ToolTier::None, 0, 0, 4, 2.0f});

    constexpr const char* names[] = {"Oak", "Birch", "Spruce", "Jungle", "Acacia", "Skyroot", "Iron"};
    for (uint8_t i=0;i<7;++i) {
        DoorState door; door.material=static_cast<DoorMaterial>(i);
        ButtonState button; button.material=door.material;
        set(static_cast<ItemId>(static_cast<uint16_t>(ItemId::OAK_DOOR)+i),
            {std::string(names[i])+" Door", ItemKind::Block, 64, 0,
             ToolKind::None, ToolTier::None, 0,0,0,0,doorBlock(door)});
        set(static_cast<ItemId>(static_cast<uint16_t>(ItemId::OAK_BUTTON)+i),
            {std::string(i==6?"Stone":names[i])+" Button", ItemKind::Block, 64, 0,
             ToolKind::None, ToolTier::None, 0,0,0,0,buttonBlock(button)});
        if(i>0 && i<5) set(plankItem(door.material),
            {std::string(names[i])+" Planks", ItemKind::Block,64,0,
             ToolKind::None,ToolTier::None,0,0,0,0,woodPlanks(door.material)});
    }

    items[static_cast<size_t>(ItemId::FLOWER)].name = "Poppy";

    return items;
}

const auto REGISTRY = buildRegistry();

// Explicit Minecraft-style tab assignment.  The lists are audited by the
// inventory tests: every serialized item appears in exactly one category and
// all category sizes are asserted there.
CreativeItemCategory categoryFor(ItemId id) {
    if (id >= ItemId::BIRCH_PLANKS && id <= ItemId::ACACIA_PLANKS)
        return CreativeItemCategory::BuildingBlocks;
    if (id >= ItemId::OAK_DOOR && id <= ItemId::STONE_BUTTON)
        return CreativeItemCategory::Functional;
    switch (id) {
        // ── Building Blocks ─────────────────────────────────────────────
        case ItemId::GRASS_BLOCK: case ItemId::DIRT: case ItemId::STONE:
        case ItemId::OAK_LOG: case ItemId::SAND: case ItemId::BEDROCK:
        case ItemId::SNOW: case ItemId::OAK_PLANKS: case ItemId::DEEPSLATE:
        case ItemId::COAL_ORE: case ItemId::IRON_ORE: case ItemId::GOLD_ORE:
        case ItemId::DIAMOND_ORE: case ItemId::ICE: case ItemId::GRAVEL:
        case ItemId::CLAY: case ItemId::RED_SAND: case ItemId::TERRACOTTA:
        case ItemId::PODZOL: case ItemId::BIRCH_LOG: case ItemId::SPRUCE_LOG:
        case ItemId::JUNGLE_LOG: case ItemId::ACACIA_LOG:
        case ItemId::COBBLESTONE: case ItemId::GLASS: case ItemId::OBSIDIAN:
        case ItemId::LIMESTONE: case ItemId::BASALT: case ItemId::TUFF:
        case ItemId::COARSE_DIRT: case ItemId::MUD: case ItemId::PACKED_ICE:
        case ItemId::BLACK_SAND: case ItemId::GRANITE:
        case ItemId::AETHER_GRASS: case ItemId::AETHER_SOIL:
        case ItemId::CLOUDSTONE: case ItemId::SUNSTONE:
        case ItemId::SKYROOT_LOG: case ItemId::STAR_CRYSTAL:
        case ItemId::OAK_PLANKS_SLAB: case ItemId::OAK_PLANKS_STAIRS:
        case ItemId::COBBLESTONE_SLAB: case ItemId::COBBLESTONE_STAIRS:
        case ItemId::TERRACOTTA_SLAB: case ItemId::TERRACOTTA_STAIRS:
        case ItemId::SUNSTONE_SLAB: case ItemId::SUNSTONE_STAIRS:
        case ItemId::CLOUDSTONE_SLAB: case ItemId::CLOUDSTONE_STAIRS:
        case ItemId::EMERALD_ORE: case ItemId::DEEPSLATE_EMERALD_ORE:
        case ItemId::DRIPSTONE_BLOCK: case ItemId::CALCITE:
        case ItemId::SULFUR_CRUST:
        case ItemId::STONE_BRICKS:
        case ItemId::MOSSY_STONE_BRICKS:
        case ItemId::CRACKED_STONE_BRICKS:
        case ItemId::CHISELED_STONE_BRICKS:
        case ItemId::BRICKS:
        case ItemId::POLISHED_GRANITE:
        case ItemId::POLISHED_BASALT:
        case ItemId::POLISHED_LIMESTONE:
        case ItemId::POLISHED_TUFF:
        case ItemId::DEEPSLATE_BRICKS:
        case ItemId::SANDSTONE:
        case ItemId::CUT_SANDSTONE:
        case ItemId::SMOOTH_SANDSTONE:
        case ItemId::RED_WOOL:
        case ItemId::YELLOW_WOOL:
        case ItemId::BLUE_WOOL:
        case ItemId::GREEN_WOOL:
        case ItemId::BLACK_WOOL:
        case ItemId::ROOTED_DIRT:
        case ItemId::LEAF_LITTER_SOIL:
        case ItemId::PEAT:
        case ItemId::SILT:
        case ItemId::DRY_GRASS_BLOCK:
        case ItemId::PERMAFROST:
        case ItemId::BLUE_ICE:
        case ItemId::SHALE:
        case ItemId::RED_SANDSTONE:
        case ItemId::OCHRE_TERRACOTTA:
        case ItemId::WHITE_TERRACOTTA:
        case ItemId::VOLCANIC_ASH:
        case ItemId::CORAL_ROCK:
        case ItemId::ANDESITE:
        case ItemId::DIORITE:
        case ItemId::GNEISS:
        case ItemId::MARBLE:
        case ItemId::LATERITE:
        case ItemId::RED_CLAY:
        case ItemId::CRACKED_MUD:
        case ItemId::SALT_CRUST:
        case ItemId::CAVE_MOSS:
        case ItemId::WET_LIMESTONE:
        case ItemId::GYPSUM:
        case ItemId::AMETHYST_BLOCK:
        case ItemId::QUARTZ_BLOCK:
        case ItemId::IRON_STAINED_ROCK:
        case ItemId::SULFUR_ROCK:
        case ItemId::MOONSTONE:
        case ItemId::SKYSTONE:
        case ItemId::AETHER_MOSS:
        case ItemId::GLIMMER_SILT:
        case ItemId::STAR_CRYSTAL_ORE:
        case ItemId::SKYROOT_PLANKS:
        case ItemId::CLOUDSTONE_BRICKS:
        case ItemId::SUNSTONE_BRICKS:
        case ItemId::MOONSTONE_BRICKS:
        case ItemId::STAR_CRYSTAL_LAMP:
            return CreativeItemCategory::BuildingBlocks;

        // ── Nature & Decoration ─────────────────────────────────────────
        case ItemId::OAK_LEAVES: case ItemId::WATER: case ItemId::CACTUS:
        case ItemId::LAVA: case ItemId::MOSS: case ItemId::TALL_GRASS:
        case ItemId::FLOWER: case ItemId::REEDS: case ItemId::BIRCH_LEAVES:
        case ItemId::SPRUCE_LEAVES: case ItemId::JUNGLE_LEAVES:
        case ItemId::ACACIA_LEAVES: case ItemId::WHITE_WOOL:
        case ItemId::OAK_SAPLING: case ItemId::BIRCH_SAPLING:
        case ItemId::SPRUCE_SAPLING: case ItemId::JUNGLE_SAPLING:
        case ItemId::ACACIA_SAPLING: case ItemId::DANDELION:
        case ItemId::BLUE_ORCHID: case ItemId::ALLIUM:
        case ItemId::OXEYE_DAISY: case ItemId::SUNFLOWER:
        case ItemId::SKYROOT_LEAVES: case ItemId::STARFLOWER:
        case ItemId::CLOUD_BLOOM: case ItemId::GLOWSHROOM:
        case ItemId::POINTED_DRIPSTONE: case ItemId::HANGING_ROOTS:
        case ItemId::GLOW_FERN: case ItemId::RESONANT_CRYSTAL:
        case ItemId::FERN:
        case ItemId::DEAD_BUSH:
        case ItemId::DRY_GRASS:
        case ItemId::BROWN_MUSHROOM:
        case ItemId::RED_MUSHROOM:
        case ItemId::LAVENDER:
        case ItemId::BELLFLOWER:
        case ItemId::ALPINE_FLOWER:
        case ItemId::TROPICAL_FLOWER:
        case ItemId::CATTAIL:
        case ItemId::BEACH_GRASS:
        case ItemId::CLOVER:
        case ItemId::HEATHER:
        case ItemId::WILD_MINT:
        case ItemId::NETTLE:
        case ItemId::DESERT_FLOWER:
        case ItemId::SMALL_CACTUS:
        case ItemId::REED_FLOWER:
        case ItemId::TUNDRA_MOSS:
        case ItemId::FALLEN_TWIGS:
        case ItemId::JUNGLE_FERN:
        case ItemId::AMETHYST_CLUSTER:
        case ItemId::QUARTZ_CLUSTER:
        case ItemId::CAVE_GLOWSHROOM:
        case ItemId::SKY_FERN:
        case ItemId::DAWN_BELL:
        case ItemId::MOONFLOWER:
        case ItemId::GLIMMER_REED:
        case ItemId::CLOUDBERRY_BUSH:
        case ItemId::HANGING_CLOUD_VINE:
            return CreativeItemCategory::Nature;

        // ── Functional Blocks ───────────────────────────────────────────
        case ItemId::CRAFTING_TABLE: case ItemId::FURNACE: case ItemId::CHEST:
        case ItemId::TORCH: case ItemId::WHITE_BED: case ItemId::FARMLAND:
        case ItemId::TNT:
        case ItemId::COMPOSTER: case ItemId::FLETCHING_TABLE:
        case ItemId::LOOM: case ItemId::CAULDRON:
        case ItemId::BLAST_FURNACE: case ItemId::SMITHING_TABLE:
        case ItemId::BARREL:
        case ItemId::LECTERN:
        case ItemId::CARTOGRAPHY_TABLE:
        case ItemId::BREWING_STAND:
        case ItemId::SMOKER:
        case ItemId::STONECUTTER:
        case ItemId::GRINDSTONE:
            return CreativeItemCategory::Functional;

        // ── Tools & Utilities ───────────────────────────────────────────
        case ItemId::WOODEN_PICKAXE: case ItemId::WOODEN_AXE:
        case ItemId::WOODEN_SHOVEL: case ItemId::WOODEN_HOE:
        case ItemId::STONE_PICKAXE: case ItemId::STONE_AXE:
        case ItemId::STONE_SHOVEL: case ItemId::STONE_HOE:
        case ItemId::IRON_PICKAXE: case ItemId::IRON_AXE:
        case ItemId::IRON_SHOVEL: case ItemId::IRON_HOE:
        case ItemId::GOLDEN_PICKAXE: case ItemId::GOLDEN_AXE:
        case ItemId::GOLDEN_SHOVEL: case ItemId::GOLDEN_HOE:
        case ItemId::DIAMOND_PICKAXE: case ItemId::DIAMOND_AXE:
        case ItemId::DIAMOND_SHOVEL: case ItemId::DIAMOND_HOE:
        case ItemId::FLINT_AND_STEEL:
        case ItemId::STARSTEP_SCEPTER:
        case ItemId::FISHING_ROD:
            return CreativeItemCategory::Tools;

        // ── Combat ──────────────────────────────────────────────────────
        case ItemId::ARROW:
        case ItemId::WOODEN_SWORD: case ItemId::STONE_SWORD:
        case ItemId::IRON_SWORD: case ItemId::GOLDEN_SWORD:
        case ItemId::DIAMOND_SWORD: case ItemId::BOW: case ItemId::SHIELD:
        case ItemId::LEATHER_HELMET: case ItemId::LEATHER_CHESTPLATE:
        case ItemId::LEATHER_LEGGINGS: case ItemId::LEATHER_BOOTS:
        case ItemId::IRON_HELMET: case ItemId::IRON_CHESTPLATE:
        case ItemId::IRON_LEGGINGS: case ItemId::IRON_BOOTS:
        case ItemId::GOLDEN_HELMET: case ItemId::GOLDEN_CHESTPLATE:
        case ItemId::GOLDEN_LEGGINGS: case ItemId::GOLDEN_BOOTS:
        case ItemId::DIAMOND_HELMET: case ItemId::DIAMOND_CHESTPLATE:
        case ItemId::DIAMOND_LEGGINGS: case ItemId::DIAMOND_BOOTS:
            return CreativeItemCategory::Combat;

        // ── Food ────────────────────────────────────────────────────────
        case ItemId::BREAD: case ItemId::RAW_BEEF: case ItemId::STEAK:
        case ItemId::RAW_PORKCHOP: case ItemId::COOKED_PORKCHOP:
        case ItemId::RAW_CHICKEN: case ItemId::COOKED_CHICKEN:
        case ItemId::MUTTON: case ItemId::COOKED_MUTTON:
        case ItemId::RAW_COD: case ItemId::RAW_SALMON:
        case ItemId::COOKED_COD: case ItemId::COOKED_SALMON:
        case ItemId::ROTTEN_FLESH:
        case ItemId::CLOUDBERRY:
        case ItemId::ROASTED_CLOUDBERRY:
        case ItemId::CLOUDBERRY_BREAD:
        case ItemId::ROASTED_GLOWSHROOM:
            return CreativeItemCategory::Food;

        // ── Materials ───────────────────────────────────────────────────
        case ItemId::CLAY_BALL:
        case ItemId::BRICK:
        case ItemId::RED_DYE:
        case ItemId::YELLOW_DYE:
        case ItemId::BLUE_DYE:
        case ItemId::GREEN_DYE:
        case ItemId::BLACK_DYE:
        case ItemId::BONE_MEAL:
        case ItemId::STICK: case ItemId::COAL: case ItemId::RAW_IRON:
        case ItemId::IRON_INGOT: case ItemId::RAW_GOLD:
        case ItemId::GOLD_INGOT: case ItemId::DIAMOND: case ItemId::STRING:
        case ItemId::FEATHER: case ItemId::LEATHER: case ItemId::BONE:
        case ItemId::WHEAT_SEEDS: case ItemId::WHEAT: case ItemId::FLINT:
        case ItemId::GUNPOWDER:
        case ItemId::EMERALD: case ItemId::STAR_CRYSTAL_SHARD:
            return CreativeItemCategory::Materials;

        // ── Spawn Eggs ──────────────────────────────────────────────────
        case ItemId::COW_SPAWN_EGG: case ItemId::PIG_SPAWN_EGG:
        case ItemId::SHEEP_SPAWN_EGG: case ItemId::CHICKEN_SPAWN_EGG:
        case ItemId::ZOMBIE_SPAWN_EGG: case ItemId::SKELETON_SPAWN_EGG:
        case ItemId::SPIDER_SPAWN_EGG: case ItemId::BLASTLING_SPAWN_EGG:
        case ItemId::VILLAGER_SPAWN_EGG:
        case ItemId::ZOMBIE_VILLAGER_SPAWN_EGG:
            return CreativeItemCategory::SpawnEggs;

        default:
            return CreativeItemCategory::Count;
    }
}

constexpr size_t categoryCount =
    static_cast<size_t>(CreativeItemCategory::Count);

const std::array<CreativeCategoryInfo, categoryCount> CATEGORY_INFO{{
    {CreativeItemCategory::BuildingBlocks, "inventory.tab.building",
     ItemId::GRASS_BLOCK},
    {CreativeItemCategory::Nature, "inventory.tab.nature", ItemId::FLOWER},
    {CreativeItemCategory::Functional, "inventory.tab.functional",
     ItemId::CRAFTING_TABLE},
    {CreativeItemCategory::Tools, "inventory.tab.tools", ItemId::IRON_PICKAXE},
    {CreativeItemCategory::Combat, "inventory.tab.combat", ItemId::IRON_SWORD},
    {CreativeItemCategory::Food, "inventory.tab.food", ItemId::BREAD},
    {CreativeItemCategory::Materials, "inventory.tab.materials",
     ItemId::DIAMOND},
    {CreativeItemCategory::SpawnEggs, "inventory.tab.spawn_eggs",
     ItemId::COW_SPAWN_EGG}
}};

std::array<std::vector<ItemId>, categoryCount> buildCategoryItems() {
    std::array<std::vector<ItemId>, categoryCount> buckets;
    for (size_t raw = 1; raw < itemCount; ++raw) {
        const ItemId id = static_cast<ItemId>(raw);
        const auto& props = getItemProps(id);
        if (props.name.empty() || props.maxStack == 0) continue;
        const auto category = categoryFor(id);
        if (category != CreativeItemCategory::Count)
            buckets[static_cast<size_t>(category)].push_back(id);
    }
    return buckets;
}

auto CATEGORY_ITEMS = buildCategoryItems();

} // namespace

CreativeItemCategory creativeInventoryCategory(ItemId id) {
    if (!isValidItemId(id)) return CreativeItemCategory::Count;
    if (const auto* item = Plugins::pluginItem(id)) return item->category;
    return categoryFor(id);
}

const std::vector<ItemId>& creativeInventoryItemsIn(
    CreativeItemCategory category) {
    static const std::vector<ItemId> empty;
    if (static_cast<size_t>(category) >= categoryCount) return empty;
    static uint64_t revision = UINT64_MAX;
    if (revision != Plugins::content().revision) {
        CATEGORY_ITEMS = buildCategoryItems();
        for (size_t i = 0; i < categoryCount; ++i) {
            const auto& extra = Plugins::content().categories[i];
            CATEGORY_ITEMS[i].insert(CATEGORY_ITEMS[i].end(), extra.begin(), extra.end());
        }
        revision = Plugins::content().revision;
    }
    return CATEGORY_ITEMS[static_cast<size_t>(category)];
}

const CreativeCategoryInfo& creativeCategoryInfo(CreativeItemCategory category) {
    static const CreativeCategoryInfo fallback{
        CreativeItemCategory::Count, "", ItemId::EMPTY};
    if (static_cast<size_t>(category) >= categoryCount) return fallback;
    return CATEGORY_INFO[static_cast<size_t>(category)];
}

bool isValidItemId(ItemId id) {
    return static_cast<size_t>(id) < itemCount || Plugins::pluginItem(id);
}

const ItemProperties& getItemProps(ItemId id) {
    if (!isValidItemId(id)) throw std::out_of_range("Invalid serialized item id");
    if (const auto* item = Plugins::pluginItem(id)) return item->properties;
    return REGISTRY[static_cast<size_t>(id)];
}

ItemId itemForBlock(BlockId id) {
    DoorState door; ButtonState button;
    if(decodeDoor(id,door)) return static_cast<ItemId>(static_cast<uint16_t>(ItemId::OAK_DOOR)+static_cast<uint8_t>(door.material));
    if(decodeButton(id,button)) return static_cast<ItemId>(static_cast<uint16_t>(ItemId::OAK_BUTTON)+static_cast<uint8_t>(button.material));
    if(id>=BlockId::BIRCH_PLANKS && id<=BlockId::ACACIA_PLANKS)
        return static_cast<ItemId>(static_cast<uint16_t>(ItemId::BIRCH_PLANKS)+static_cast<uint16_t>(id)-static_cast<uint16_t>(BlockId::BIRCH_PLANKS));
    if (const auto* block = Plugins::pluginBlock(id)) return block->drop;
    if (isBed(id)) return ItemId::WHITE_BED;
    ArchitecturalBlockState architectural;
    if (decodeArchitecturalBlock(id, architectural)) {
        const uint16_t base = static_cast<uint16_t>(ItemId::OAK_PLANKS_SLAB) +
            static_cast<uint16_t>(architectural.material) * 2;
        return static_cast<ItemId>(base +
            (architectural.shape == RenderShape::Stair ? 1 : 0));
    }
    const auto raw = static_cast<uint16_t>(id);
    if (raw > 0 && raw <= 35) return static_cast<ItemId>(raw);
    switch (raw) {
        case static_cast<uint16_t>(BlockId::ROOTED_DIRT): return ItemId::ROOTED_DIRT;
        case static_cast<uint16_t>(BlockId::LEAF_LITTER_SOIL): return ItemId::LEAF_LITTER_SOIL;
        case static_cast<uint16_t>(BlockId::PEAT): return ItemId::PEAT;
        case static_cast<uint16_t>(BlockId::SILT): return ItemId::SILT;
        case static_cast<uint16_t>(BlockId::DRY_GRASS_BLOCK): return ItemId::DRY_GRASS_BLOCK;
        case static_cast<uint16_t>(BlockId::PERMAFROST): return ItemId::PERMAFROST;
        case static_cast<uint16_t>(BlockId::BLUE_ICE): return ItemId::BLUE_ICE;
        case static_cast<uint16_t>(BlockId::SHALE): return ItemId::SHALE;
        case static_cast<uint16_t>(BlockId::RED_SANDSTONE): return ItemId::RED_SANDSTONE;
        case static_cast<uint16_t>(BlockId::OCHRE_TERRACOTTA): return ItemId::OCHRE_TERRACOTTA;
        case static_cast<uint16_t>(BlockId::WHITE_TERRACOTTA): return ItemId::WHITE_TERRACOTTA;
        case static_cast<uint16_t>(BlockId::VOLCANIC_ASH): return ItemId::VOLCANIC_ASH;
        case static_cast<uint16_t>(BlockId::CORAL_ROCK): return ItemId::CORAL_ROCK;
        case static_cast<uint16_t>(BlockId::FERN): return ItemId::FERN;
        case static_cast<uint16_t>(BlockId::DEAD_BUSH): return ItemId::DEAD_BUSH;
        case static_cast<uint16_t>(BlockId::DRY_GRASS): return ItemId::DRY_GRASS;
        case static_cast<uint16_t>(BlockId::BROWN_MUSHROOM): return ItemId::BROWN_MUSHROOM;
        case static_cast<uint16_t>(BlockId::RED_MUSHROOM): return ItemId::RED_MUSHROOM;
        case static_cast<uint16_t>(BlockId::LAVENDER): return ItemId::LAVENDER;
        case static_cast<uint16_t>(BlockId::BELLFLOWER): return ItemId::BELLFLOWER;
        case static_cast<uint16_t>(BlockId::ALPINE_FLOWER): return ItemId::ALPINE_FLOWER;
        case static_cast<uint16_t>(BlockId::TROPICAL_FLOWER): return ItemId::TROPICAL_FLOWER;
        case static_cast<uint16_t>(BlockId::CATTAIL): return ItemId::CATTAIL;
        case static_cast<uint16_t>(BlockId::BEACH_GRASS): return ItemId::BEACH_GRASS;
        case static_cast<uint16_t>(BlockId::ANDESITE): return ItemId::ANDESITE;
        case static_cast<uint16_t>(BlockId::DIORITE): return ItemId::DIORITE;
        case static_cast<uint16_t>(BlockId::GNEISS): return ItemId::GNEISS;
        case static_cast<uint16_t>(BlockId::MARBLE): return ItemId::MARBLE;
        case static_cast<uint16_t>(BlockId::LATERITE): return ItemId::LATERITE;
        case static_cast<uint16_t>(BlockId::RED_CLAY): return ItemId::RED_CLAY;
        case static_cast<uint16_t>(BlockId::CRACKED_MUD): return ItemId::CRACKED_MUD;
        case static_cast<uint16_t>(BlockId::SALT_CRUST): return ItemId::SALT_CRUST;
        case static_cast<uint16_t>(BlockId::CLOVER): return ItemId::CLOVER;
        case static_cast<uint16_t>(BlockId::HEATHER): return ItemId::HEATHER;
        case static_cast<uint16_t>(BlockId::WILD_MINT): return ItemId::WILD_MINT;
        case static_cast<uint16_t>(BlockId::NETTLE): return ItemId::NETTLE;
        case static_cast<uint16_t>(BlockId::DESERT_FLOWER): return ItemId::DESERT_FLOWER;
        case static_cast<uint16_t>(BlockId::SMALL_CACTUS): return ItemId::SMALL_CACTUS;
        case static_cast<uint16_t>(BlockId::REED_FLOWER): return ItemId::REED_FLOWER;
        case static_cast<uint16_t>(BlockId::TUNDRA_MOSS): return ItemId::TUNDRA_MOSS;
        case static_cast<uint16_t>(BlockId::FALLEN_TWIGS): return ItemId::FALLEN_TWIGS;
        case static_cast<uint16_t>(BlockId::JUNGLE_FERN): return ItemId::JUNGLE_FERN;
        case static_cast<uint16_t>(BlockId::CAVE_MOSS): return ItemId::CAVE_MOSS;
        case static_cast<uint16_t>(BlockId::WET_LIMESTONE): return ItemId::WET_LIMESTONE;
        case static_cast<uint16_t>(BlockId::GYPSUM): return ItemId::GYPSUM;
        case static_cast<uint16_t>(BlockId::AMETHYST_BLOCK): return ItemId::AMETHYST_BLOCK;
        case static_cast<uint16_t>(BlockId::QUARTZ_BLOCK): return ItemId::QUARTZ_BLOCK;
        case static_cast<uint16_t>(BlockId::IRON_STAINED_ROCK): return ItemId::IRON_STAINED_ROCK;
        case static_cast<uint16_t>(BlockId::SULFUR_ROCK): return ItemId::SULFUR_ROCK;
        case static_cast<uint16_t>(BlockId::AMETHYST_CLUSTER): return ItemId::AMETHYST_CLUSTER;
        case static_cast<uint16_t>(BlockId::QUARTZ_CLUSTER): return ItemId::QUARTZ_CLUSTER;
        case static_cast<uint16_t>(BlockId::CAVE_GLOWSHROOM): return ItemId::CAVE_GLOWSHROOM;


        case static_cast<uint16_t>(BlockId::STONE_BRICKS): return ItemId::STONE_BRICKS;
        case static_cast<uint16_t>(BlockId::MOSSY_STONE_BRICKS): return ItemId::MOSSY_STONE_BRICKS;
        case static_cast<uint16_t>(BlockId::CRACKED_STONE_BRICKS): return ItemId::CRACKED_STONE_BRICKS;
        case static_cast<uint16_t>(BlockId::CHISELED_STONE_BRICKS): return ItemId::CHISELED_STONE_BRICKS;
        case static_cast<uint16_t>(BlockId::BRICKS): return ItemId::BRICKS;
        case static_cast<uint16_t>(BlockId::POLISHED_GRANITE): return ItemId::POLISHED_GRANITE;
        case static_cast<uint16_t>(BlockId::POLISHED_BASALT): return ItemId::POLISHED_BASALT;
        case static_cast<uint16_t>(BlockId::POLISHED_LIMESTONE): return ItemId::POLISHED_LIMESTONE;
        case static_cast<uint16_t>(BlockId::POLISHED_TUFF): return ItemId::POLISHED_TUFF;
        case static_cast<uint16_t>(BlockId::DEEPSLATE_BRICKS): return ItemId::DEEPSLATE_BRICKS;
        case static_cast<uint16_t>(BlockId::SANDSTONE): return ItemId::SANDSTONE;
        case static_cast<uint16_t>(BlockId::CUT_SANDSTONE): return ItemId::CUT_SANDSTONE;
        case static_cast<uint16_t>(BlockId::SMOOTH_SANDSTONE): return ItemId::SMOOTH_SANDSTONE;
        case static_cast<uint16_t>(BlockId::RED_WOOL): return ItemId::RED_WOOL;
        case static_cast<uint16_t>(BlockId::YELLOW_WOOL): return ItemId::YELLOW_WOOL;
        case static_cast<uint16_t>(BlockId::BLUE_WOOL): return ItemId::BLUE_WOOL;
        case static_cast<uint16_t>(BlockId::GREEN_WOOL): return ItemId::GREEN_WOOL;
        case static_cast<uint16_t>(BlockId::BLACK_WOOL): return ItemId::BLACK_WOOL;

        case static_cast<uint16_t>(BlockId::COBBLESTONE): return ItemId::COBBLESTONE;
        case static_cast<uint16_t>(BlockId::CRAFTING_TABLE): return ItemId::CRAFTING_TABLE;
        case static_cast<uint16_t>(BlockId::FURNACE): return ItemId::FURNACE;
        case static_cast<uint16_t>(BlockId::CHEST): return ItemId::CHEST;
        case static_cast<uint16_t>(BlockId::TORCH): return ItemId::TORCH;
        case static_cast<uint16_t>(BlockId::WHITE_WOOL): return ItemId::WHITE_WOOL;
        case static_cast<uint16_t>(BlockId::FARMLAND): return ItemId::DIRT;
        case static_cast<uint16_t>(BlockId::FARMLAND_1):
        case static_cast<uint16_t>(BlockId::FARMLAND_2):
        case static_cast<uint16_t>(BlockId::FARMLAND_3):
        case static_cast<uint16_t>(BlockId::FARMLAND_4):
        case static_cast<uint16_t>(BlockId::FARMLAND_5):
        case static_cast<uint16_t>(BlockId::FARMLAND_6):
        case static_cast<uint16_t>(BlockId::FARMLAND_7): return ItemId::DIRT;
        case static_cast<uint16_t>(BlockId::OAK_SAPLING): return ItemId::OAK_SAPLING;
        case static_cast<uint16_t>(BlockId::BIRCH_SAPLING): return ItemId::BIRCH_SAPLING;
        case static_cast<uint16_t>(BlockId::SPRUCE_SAPLING): return ItemId::SPRUCE_SAPLING;
        case static_cast<uint16_t>(BlockId::JUNGLE_SAPLING): return ItemId::JUNGLE_SAPLING;
        case static_cast<uint16_t>(BlockId::ACACIA_SAPLING): return ItemId::ACACIA_SAPLING;
        case static_cast<uint16_t>(BlockId::GLASS): return ItemId::GLASS;
        case static_cast<uint16_t>(BlockId::TNT): return ItemId::TNT;
        case static_cast<uint16_t>(BlockId::OBSIDIAN): return ItemId::OBSIDIAN;
        case static_cast<uint16_t>(BlockId::DANDELION): return ItemId::DANDELION;
        case static_cast<uint16_t>(BlockId::BLUE_ORCHID): return ItemId::BLUE_ORCHID;
        case static_cast<uint16_t>(BlockId::ALLIUM): return ItemId::ALLIUM;
        case static_cast<uint16_t>(BlockId::OXEYE_DAISY): return ItemId::OXEYE_DAISY;
        case static_cast<uint16_t>(BlockId::SUNFLOWER_BOTTOM): return ItemId::SUNFLOWER;
        case static_cast<uint16_t>(BlockId::SUNFLOWER_TOP): return ItemId::EMPTY;
        case static_cast<uint16_t>(BlockId::EMERALD_ORE): return ItemId::EMERALD_ORE;
        case static_cast<uint16_t>(BlockId::DEEPSLATE_EMERALD_ORE):
            return ItemId::DEEPSLATE_EMERALD_ORE;
        case static_cast<uint16_t>(BlockId::COMPOSTER): return ItemId::COMPOSTER;
        case static_cast<uint16_t>(BlockId::FLETCHING_TABLE): return ItemId::FLETCHING_TABLE;
        case static_cast<uint16_t>(BlockId::LOOM): return ItemId::LOOM;
        case static_cast<uint16_t>(BlockId::CAULDRON): return ItemId::CAULDRON;
        case static_cast<uint16_t>(BlockId::BLAST_FURNACE): return ItemId::BLAST_FURNACE;
        case static_cast<uint16_t>(BlockId::SMITHING_TABLE): return ItemId::SMITHING_TABLE;
        case static_cast<uint16_t>(BlockId::BARREL): return ItemId::BARREL;
        case static_cast<uint16_t>(BlockId::LECTERN): return ItemId::LECTERN;
        case static_cast<uint16_t>(BlockId::CARTOGRAPHY_TABLE): return ItemId::CARTOGRAPHY_TABLE;
        case static_cast<uint16_t>(BlockId::BREWING_STAND): return ItemId::BREWING_STAND;
        case static_cast<uint16_t>(BlockId::SMOKER): return ItemId::SMOKER;
        case static_cast<uint16_t>(BlockId::STONECUTTER): return ItemId::STONECUTTER;
        case static_cast<uint16_t>(BlockId::GRINDSTONE): return ItemId::GRINDSTONE;
        case static_cast<uint16_t>(BlockId::FLOWING_WATER_1):
        case static_cast<uint16_t>(BlockId::FLOWING_WATER_2):
        case static_cast<uint16_t>(BlockId::FLOWING_WATER_3):
        case static_cast<uint16_t>(BlockId::FLOWING_WATER_4):
        case static_cast<uint16_t>(BlockId::FLOWING_WATER_5):
        case static_cast<uint16_t>(BlockId::FLOWING_WATER_6):
        case static_cast<uint16_t>(BlockId::FLOWING_WATER_7): return ItemId::WATER;
        case static_cast<uint16_t>(BlockId::FLOWING_LAVA_1):
        case static_cast<uint16_t>(BlockId::FLOWING_LAVA_2):
        case static_cast<uint16_t>(BlockId::FLOWING_LAVA_3):
        case static_cast<uint16_t>(BlockId::FLOWING_LAVA_4):
        case static_cast<uint16_t>(BlockId::FLOWING_LAVA_5):
        case static_cast<uint16_t>(BlockId::FLOWING_LAVA_6):
        case static_cast<uint16_t>(BlockId::FLOWING_LAVA_7):
        case static_cast<uint16_t>(BlockId::FALLING_LAVA): return ItemId::LAVA;
        case static_cast<uint16_t>(BlockId::FALLING_WATER): return ItemId::WATER;
        case static_cast<uint16_t>(BlockId::LIMESTONE): return ItemId::LIMESTONE;
        case static_cast<uint16_t>(BlockId::BASALT): return ItemId::BASALT;
        case static_cast<uint16_t>(BlockId::TUFF): return ItemId::TUFF;
        case static_cast<uint16_t>(BlockId::COARSE_DIRT): return ItemId::COARSE_DIRT;
        case static_cast<uint16_t>(BlockId::MUD): return ItemId::MUD;
        case static_cast<uint16_t>(BlockId::PACKED_ICE): return ItemId::PACKED_ICE;
        case static_cast<uint16_t>(BlockId::BLACK_SAND): return ItemId::BLACK_SAND;
        case static_cast<uint16_t>(BlockId::GRANITE): return ItemId::GRANITE;
        case static_cast<uint16_t>(BlockId::AETHER_GRASS): return ItemId::AETHER_GRASS;
        case static_cast<uint16_t>(BlockId::AETHER_SOIL): return ItemId::AETHER_SOIL;
        case static_cast<uint16_t>(BlockId::CLOUDSTONE): return ItemId::CLOUDSTONE;
        case static_cast<uint16_t>(BlockId::SUNSTONE): return ItemId::SUNSTONE;
        case static_cast<uint16_t>(BlockId::SKYROOT_WOOD): return ItemId::SKYROOT_LOG;
        case static_cast<uint16_t>(BlockId::SKYROOT_LEAVES): return ItemId::SKYROOT_LEAVES;
        case static_cast<uint16_t>(BlockId::STAR_CRYSTAL): return ItemId::STAR_CRYSTAL;
        case static_cast<uint16_t>(BlockId::STARFLOWER): return ItemId::STARFLOWER;
        case static_cast<uint16_t>(BlockId::CLOUD_BLOOM): return ItemId::CLOUD_BLOOM;
        case static_cast<uint16_t>(BlockId::GLOWSHROOM): return ItemId::GLOWSHROOM;
        case static_cast<uint16_t>(BlockId::DRIPSTONE_BLOCK): return ItemId::DRIPSTONE_BLOCK;
        case static_cast<uint16_t>(BlockId::POINTED_DRIPSTONE_UP):
        case static_cast<uint16_t>(BlockId::POINTED_DRIPSTONE_DOWN):
            return ItemId::POINTED_DRIPSTONE;
        case static_cast<uint16_t>(BlockId::CALCITE): return ItemId::CALCITE;
        case static_cast<uint16_t>(BlockId::HANGING_ROOTS): return ItemId::HANGING_ROOTS;
        case static_cast<uint16_t>(BlockId::GLOW_FERN): return ItemId::GLOW_FERN;
        case static_cast<uint16_t>(BlockId::RESONANT_CRYSTAL): return ItemId::RESONANT_CRYSTAL;
        case static_cast<uint16_t>(BlockId::SULFUR_CRUST): return ItemId::SULFUR_CRUST;
        case static_cast<uint16_t>(BlockId::MOONSTONE): return ItemId::MOONSTONE;
        case static_cast<uint16_t>(BlockId::SKYSTONE): return ItemId::SKYSTONE;
        case static_cast<uint16_t>(BlockId::AETHER_MOSS): return ItemId::AETHER_MOSS;
        case static_cast<uint16_t>(BlockId::GLIMMER_SILT): return ItemId::GLIMMER_SILT;
        case static_cast<uint16_t>(BlockId::STAR_CRYSTAL_ORE): return ItemId::STAR_CRYSTAL_ORE;
        case static_cast<uint16_t>(BlockId::SKYROOT_PLANKS): return ItemId::SKYROOT_PLANKS;
        case static_cast<uint16_t>(BlockId::CLOUDSTONE_BRICKS): return ItemId::CLOUDSTONE_BRICKS;
        case static_cast<uint16_t>(BlockId::SUNSTONE_BRICKS): return ItemId::SUNSTONE_BRICKS;
        case static_cast<uint16_t>(BlockId::MOONSTONE_BRICKS): return ItemId::MOONSTONE_BRICKS;
        case static_cast<uint16_t>(BlockId::STAR_CRYSTAL_LAMP): return ItemId::STAR_CRYSTAL_LAMP;
        case static_cast<uint16_t>(BlockId::SKY_FERN): return ItemId::SKY_FERN;
        case static_cast<uint16_t>(BlockId::DAWN_BELL): return ItemId::DAWN_BELL;
        case static_cast<uint16_t>(BlockId::MOONFLOWER): return ItemId::MOONFLOWER;
        case static_cast<uint16_t>(BlockId::GLIMMER_REED): return ItemId::GLIMMER_REED;
        case static_cast<uint16_t>(BlockId::CLOUDBERRY_BUSH): return ItemId::CLOUDBERRY_BUSH;
        case static_cast<uint16_t>(BlockId::HANGING_CLOUD_VINE): return ItemId::HANGING_CLOUD_VINE;
        default: return ItemId::EMPTY;
    }
}

std::vector<ItemId> creativeInventoryItems() {
    std::vector<ItemId> items;
    items.reserve(itemCount - 1);
    for (size_t raw = 1; raw < itemCount; ++raw) {
        const ItemId id = static_cast<ItemId>(raw);
        const auto& props = getItemProps(id);
        if (!props.name.empty() && props.maxStack > 0) items.push_back(id);
    }
    for (const auto& entry : Plugins::content().items) items.push_back(static_cast<ItemId>(entry.first));
    return items;
}

const std::string& itemCommandName(ItemId id) {
    if (const auto* item = Plugins::pluginItem(id)) return item->key;
    static const auto names = [] {
        std::array<std::string, itemCount> result{};
        for (size_t i = 1; i < result.size(); ++i) {
            for (unsigned char c : getItemProps(static_cast<ItemId>(i)).name)
                result[i] += c == ' ' ? '_' : static_cast<char>(std::tolower(c));
        }
        return result;
    }();
    return names[isValidItemId(id) ? static_cast<size_t>(id) : 0];
}

std::optional<ItemId> itemFromCommandName(std::string_view name) {
    for (const auto& entry : Plugins::content().items)
        if (entry.second.key == name) return static_cast<ItemId>(entry.first);
    for (size_t i = 1; i < itemCount; ++i) {
        const auto id = static_cast<ItemId>(i);
        if (itemCommandName(id) == name) return id;
    }
    return std::nullopt;
}

ItemId plankItem(DoorMaterial material) {
    constexpr ItemId items[] = {ItemId::OAK_PLANKS, ItemId::BIRCH_PLANKS,
        ItemId::SPRUCE_PLANKS,ItemId::JUNGLE_PLANKS,ItemId::ACACIA_PLANKS,
        ItemId::SKYROOT_PLANKS,ItemId::STONE};
    return items[static_cast<uint8_t>(material)];
}
bool isWoodPlankItem(ItemId id) {
    return id==ItemId::OAK_PLANKS || id==ItemId::SKYROOT_PLANKS ||
        (id>=ItemId::BIRCH_PLANKS && id<=ItemId::ACACIA_PLANKS);
}
