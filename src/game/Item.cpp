#include "game/Item.h"

#include <array>
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
            props.attackDamage = offset == 4 ? 4.0f + static_cast<float>(tierIndex) : 1.0f;
            props.name = std::string(tierNames[tierIndex]) + names[offset];
            set(static_cast<ItemId>(base + offset), props);
        }
    }

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

    const std::array<std::pair<ItemId, SpawnEggMob>, 8> spawnEggs{{
        {ItemId::COW_SPAWN_EGG, SpawnEggMob::Cow},
        {ItemId::PIG_SPAWN_EGG, SpawnEggMob::Pig},
        {ItemId::SHEEP_SPAWN_EGG, SpawnEggMob::Sheep},
        {ItemId::CHICKEN_SPAWN_EGG, SpawnEggMob::Chicken},
        {ItemId::ZOMBIE_SPAWN_EGG, SpawnEggMob::Zombie},
        {ItemId::SKELETON_SPAWN_EGG, SpawnEggMob::Skeleton},
        {ItemId::SPIDER_SPAWN_EGG, SpawnEggMob::Spider},
        {ItemId::BLASTLING_SPAWN_EGG, SpawnEggMob::Blastling}
    }};
    const std::array<const char*, 8> spawnEggNames{{
        "Cow Spawn Egg", "Pig Spawn Egg", "Sheep Spawn Egg",
        "Chicken Spawn Egg", "Zombie Spawn Egg", "Skeleton Spawn Egg",
        "Spider Spawn Egg", "Blastling Spawn Egg"
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

    items[static_cast<size_t>(ItemId::FLOWER)].name = "Poppy";

    return items;
}

const auto REGISTRY = buildRegistry();

} // namespace

bool isValidItemId(ItemId id) {
    return static_cast<size_t>(id) < itemCount;
}

const ItemProperties& getItemProps(ItemId id) {
    if (!isValidItemId(id)) throw std::out_of_range("Invalid serialized item id");
    return REGISTRY[static_cast<size_t>(id)];
}

ItemId itemForBlock(BlockId id) {
    if (isBed(id)) return ItemId::WHITE_BED;
    const auto raw = static_cast<uint16_t>(id);
    if (raw > 0 && raw <= 35) return static_cast<ItemId>(raw);
    switch (raw) {
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
    return items;
}
