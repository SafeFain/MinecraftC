#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "world/Block.h"

// Serialized item identifiers. Existing values must never be reordered.
enum class ItemId : uint16_t {
    EMPTY = 0,

    GRASS_BLOCK = 1,
    DIRT = 2,
    STONE = 3,
    OAK_LOG = 4,
    OAK_LEAVES = 5,
    SAND = 6,
    BEDROCK = 7,
    WATER = 8,
    SNOW = 9,
    OAK_PLANKS = 10,
    DEEPSLATE = 11,
    CACTUS = 12,
    COAL_ORE = 13,
    IRON_ORE = 14,
    GOLD_ORE = 15,
    DIAMOND_ORE = 16,
    LAVA = 17,
    ICE = 18,
    GRAVEL = 19,
    CLAY = 20,
    RED_SAND = 21,
    TERRACOTTA = 22,
    PODZOL = 23,
    MOSS = 24,
    TALL_GRASS = 25,
    FLOWER = 26,
    REEDS = 27,
    BIRCH_LOG = 28,
    BIRCH_LEAVES = 29,
    SPRUCE_LOG = 30,
    SPRUCE_LEAVES = 31,
    JUNGLE_LOG = 32,
    JUNGLE_LEAVES = 33,
    ACACIA_LOG = 34,
    ACACIA_LEAVES = 35,

    COBBLESTONE = 36,
    STICK,
    COAL,
    RAW_IRON,
    IRON_INGOT,
    RAW_GOLD,
    GOLD_INGOT,
    DIAMOND,
    STRING,
    FEATHER,
    LEATHER,
    BONE,
    ARROW,
    WHEAT_SEEDS,
    WHEAT,
    BREAD,
    RAW_BEEF,
    STEAK,
    RAW_PORKCHOP,
    COOKED_PORKCHOP,
    RAW_CHICKEN,
    COOKED_CHICKEN,
    MUTTON,
    COOKED_MUTTON,
    ROTTEN_FLESH,
    WHITE_WOOL,

    CRAFTING_TABLE,
    FURNACE,
    CHEST,
    TORCH,
    WHITE_BED,
    FARMLAND,

    WOODEN_PICKAXE,
    WOODEN_AXE,
    WOODEN_SHOVEL,
    WOODEN_HOE,
    WOODEN_SWORD,
    STONE_PICKAXE,
    STONE_AXE,
    STONE_SHOVEL,
    STONE_HOE,
    STONE_SWORD,
    IRON_PICKAXE,
    IRON_AXE,
    IRON_SHOVEL,
    IRON_HOE,
    IRON_SWORD,
    GOLDEN_PICKAXE,
    GOLDEN_AXE,
    GOLDEN_SHOVEL,
    GOLDEN_HOE,
    GOLDEN_SWORD,
    DIAMOND_PICKAXE,
    DIAMOND_AXE,
    DIAMOND_SHOVEL,
    DIAMOND_HOE,
    DIAMOND_SWORD,
    BOW,
    SHIELD,

    LEATHER_HELMET,
    LEATHER_CHESTPLATE,
    LEATHER_LEGGINGS,
    LEATHER_BOOTS,
    IRON_HELMET,
    IRON_CHESTPLATE,
    IRON_LEGGINGS,
    IRON_BOOTS,
    GOLDEN_HELMET,
    GOLDEN_CHESTPLATE,
    GOLDEN_LEGGINGS,
    GOLDEN_BOOTS,
    DIAMOND_HELMET,
    DIAMOND_CHESTPLATE,
    DIAMOND_LEGGINGS,
    DIAMOND_BOOTS,

    FLINT,

    OAK_SAPLING,
    BIRCH_SAPLING,
    SPRUCE_SAPLING,
    JUNGLE_SAPLING,
    ACACIA_SAPLING,

    GLASS,
    TNT,
    OBSIDIAN,
    DANDELION,
    BLUE_ORCHID,
    ALLIUM,
    OXEYE_DAISY,
    SUNFLOWER,
    FLINT_AND_STEEL,
    GUNPOWDER,

    COW_SPAWN_EGG,
    PIG_SPAWN_EGG,
    SHEEP_SPAWN_EGG,
    CHICKEN_SPAWN_EGG,
    ZOMBIE_SPAWN_EGG,
    SKELETON_SPAWN_EGG,
    SPIDER_SPAWN_EGG,
    BLASTLING_SPAWN_EGG,

    LIMESTONE,
    BASALT,
    TUFF,
    COARSE_DIRT,
    MUD,
    PACKED_ICE,
    BLACK_SAND,
    GRANITE,

    AETHER_GRASS,
    AETHER_SOIL,
    CLOUDSTONE,
    SUNSTONE,
    SKYROOT_LOG,
    SKYROOT_LEAVES,
    STAR_CRYSTAL,
    STARFLOWER,
    CLOUD_BLOOM,
    GLOWSHROOM,

    OAK_PLANKS_SLAB,
    OAK_PLANKS_STAIRS,
    COBBLESTONE_SLAB,
    COBBLESTONE_STAIRS,
    TERRACOTTA_SLAB,
    TERRACOTTA_STAIRS,
    SUNSTONE_SLAB,
    SUNSTONE_STAIRS,
    CLOUDSTONE_SLAB,
    CLOUDSTONE_STAIRS,

    EMERALD,
    EMERALD_ORE,
    DEEPSLATE_EMERALD_ORE,
    COMPOSTER,
    FLETCHING_TABLE,
    LOOM,
    CAULDRON,
    BLAST_FURNACE,
    SMITHING_TABLE,
    GRINDSTONE,
    VILLAGER_SPAWN_EGG,
    ZOMBIE_VILLAGER_SPAWN_EGG,

    COUNT,
    POPPY = FLOWER
};

enum class ItemKind : uint8_t {
    Material,
    Block,
    Tool,
    Weapon,
    Armor,
    Food,
    SpawnEgg
};

// Minecraft-style creative inventory tabs.  Every registered item belongs to
// exactly one category; the enum order is the tab order in the catalog UI.
enum class CreativeItemCategory : uint8_t {
    BuildingBlocks,
    Nature,
    Functional,
    Tools,
    Combat,
    Food,
    Materials,
    SpawnEggs,
    Count
};

enum class SpawnEggMob : uint8_t {
    Cow,
    Pig,
    Sheep,
    Chicken,
    Zombie,
    Skeleton,
    Spider,
    Blastling,
    Villager,
    ZombieVillager
};

enum class ToolKind : uint8_t {
    None,
    Pickaxe,
    Axe,
    Shovel,
    Hoe,
    Sword,
    Bow,
    Shield
};

enum class ToolTier : uint8_t {
    None,
    Wood,
    Stone,
    Iron,
    Gold,
    Diamond
};

struct ItemProperties {
    std::string name;
    ItemKind kind = ItemKind::Material;
    uint8_t maxStack = 64;
    uint16_t maxDurability = 0;
    ToolKind tool = ToolKind::None;
    ToolTier tier = ToolTier::None;
    float attackDamage = 0.0f;
    float attackSpeed = 0.0f;
    uint8_t food = 0;
    float saturation = 0.0f;
    std::optional<BlockId> placedBlock;
    std::optional<SpawnEggMob> spawnEggMob;

    ItemProperties() = default;
    ItemProperties(std::string itemName, ItemKind itemKind = ItemKind::Material,
                   uint8_t stack = 64, uint16_t durability = 0,
                   ToolKind toolKind = ToolKind::None,
                   ToolTier toolTier = ToolTier::None,
                   float damage = 0.0f, float speed = 0.0f,
                   uint8_t foodValue = 0, float saturationValue = 0.0f,
                   std::optional<BlockId> block = std::nullopt,
                   std::optional<SpawnEggMob> eggMob = std::nullopt)
        : name(std::move(itemName)), kind(itemKind), maxStack(stack),
          maxDurability(durability), tool(toolKind), tier(toolTier),
          attackDamage(damage), attackSpeed(speed), food(foodValue),
          saturation(saturationValue), placedBlock(block), spawnEggMob(eggMob) {}
};

struct CreativeCategoryInfo {
    CreativeItemCategory category;
    const char* localizationKey;
    ItemId icon;
};

struct ItemStack {
    ItemId id = ItemId::EMPTY;
    uint8_t count = 0;
    uint16_t damage = 0;

    bool empty() const { return id == ItemId::EMPTY || count == 0; }
    void clear() {
        id = ItemId::EMPTY;
        count = 0;
        damage = 0;
    }
};

const ItemProperties& getItemProps(ItemId id);
bool isValidItemId(ItemId id);
ItemId itemForBlock(BlockId id);
std::vector<ItemId> creativeInventoryItems();
CreativeItemCategory creativeInventoryCategory(ItemId id);
const std::vector<ItemId>& creativeInventoryItemsIn(CreativeItemCategory category);
const CreativeCategoryInfo& creativeCategoryInfo(CreativeItemCategory category);
