#include "game/SurvivalRules.h"

#include <algorithm>
#include <array>

#include "game/InventoryModel.h"

namespace {

int tierLevel(ToolTier tier) {
    switch (tier) {
        case ToolTier::None: return 0;
        case ToolTier::Wood:
        case ToolTier::Gold: return 1;
        case ToolTier::Stone: return 2;
        case ToolTier::Iron: return 3;
        case ToolTier::Diamond: return 4;
    }
    return 0;
}

bool canHarvest(const BlockSurvivalProperties& block, const ItemProperties& tool) {
    if (block.minimumHarvestTier == ToolTier::None) return true;
    return tool.tool == block.preferredTool &&
           tierLevel(tool.tier) >= tierLevel(block.minimumHarvestTier);
}

std::array<BlockSurvivalProperties, static_cast<size_t>(BlockId::COUNT)> buildBlocks() {
    std::array<BlockSurvivalProperties, static_cast<size_t>(BlockId::COUNT)> values{};
    auto set = [&](BlockId id, float hardness, ToolKind tool = ToolKind::None,
                   ToolTier tier = ToolTier::None, bool unbreakable = false) {
        values[static_cast<size_t>(id)] = {hardness, tool, tier, unbreakable};
    };
    set(BlockId::AIR, 0.0f);
    set(BlockId::GRASS, 0.6f, ToolKind::Shovel);
    set(BlockId::DIRT, 0.5f, ToolKind::Shovel);
    set(BlockId::STONE, 1.5f, ToolKind::Pickaxe, ToolTier::Wood);
    set(BlockId::WOOD, 2.0f, ToolKind::Axe);
    set(BlockId::LEAVES, 0.2f);
    set(BlockId::SAND, 0.5f, ToolKind::Shovel);
    set(BlockId::BEDROCK, -1.0f, ToolKind::Pickaxe, ToolTier::Diamond, true);
    set(BlockId::WATER, -1.0f, ToolKind::None, ToolTier::None, true);
    set(BlockId::SNOW, 0.2f, ToolKind::Shovel);
    set(BlockId::PLANKS, 2.0f, ToolKind::Axe);
    set(BlockId::DEEPSLATE, 3.0f, ToolKind::Pickaxe, ToolTier::Wood);
    set(BlockId::CACTUS_BLOCK, 0.4f);
    set(BlockId::COAL_ORE, 3.0f, ToolKind::Pickaxe, ToolTier::Wood);
    set(BlockId::IRON_ORE, 3.0f, ToolKind::Pickaxe, ToolTier::Stone);
    set(BlockId::GOLD_ORE, 3.0f, ToolKind::Pickaxe, ToolTier::Iron);
    set(BlockId::DIAMOND_ORE, 3.0f, ToolKind::Pickaxe, ToolTier::Iron);
    set(BlockId::LAVA, -1.0f, ToolKind::None, ToolTier::None, true);
    set(BlockId::ICE, 0.5f, ToolKind::Pickaxe);
    set(BlockId::GRAVEL, 0.6f, ToolKind::Shovel);
    for (BlockId id : {BlockId::CLAY, BlockId::RED_SAND, BlockId::FARMLAND,
                       BlockId::FARMLAND_1, BlockId::FARMLAND_2,
                       BlockId::FARMLAND_3, BlockId::FARMLAND_4,
                       BlockId::FARMLAND_5, BlockId::FARMLAND_6,
                       BlockId::FARMLAND_7})
        set(id, 0.6f, ToolKind::Shovel);
    for (BlockId id : {BlockId::TERRACOTTA, BlockId::COBBLESTONE,
                       BlockId::FURNACE})
        set(id, 2.0f, ToolKind::Pickaxe, ToolTier::Wood);
    for (BlockId id : {BlockId::PODZOL, BlockId::MOSS}) set(id, 0.6f, ToolKind::Shovel);
    for (BlockId id : {BlockId::TALL_GRASS, BlockId::FLOWER, BlockId::REEDS,
                       BlockId::TORCH, BlockId::WHEAT_0, BlockId::WHEAT_1,
                       BlockId::WHEAT_2, BlockId::WHEAT_3, BlockId::WHEAT_4,
                       BlockId::WHEAT_5, BlockId::WHEAT_6, BlockId::WHEAT_7})
        set(id, 0.0f);
    for (BlockId id : {BlockId::OAK_SAPLING, BlockId::BIRCH_SAPLING,
                       BlockId::SPRUCE_SAPLING, BlockId::JUNGLE_SAPLING,
                       BlockId::ACACIA_SAPLING})
        set(id, 0.0f);
    for (BlockId id : {BlockId::BIRCH_WOOD, BlockId::SPRUCE_WOOD,
                       BlockId::JUNGLE_WOOD, BlockId::ACACIA_WOOD,
                       BlockId::CRAFTING_TABLE, BlockId::CHEST})
        set(id, 2.0f, ToolKind::Axe);
    for (BlockId id : {BlockId::BIRCH_LEAVES, BlockId::SPRUCE_LEAVES,
                       BlockId::JUNGLE_LEAVES, BlockId::ACACIA_LEAVES})
        set(id, 0.2f);
    set(BlockId::WHITE_WOOL, 0.8f);
    set(BlockId::WHITE_BED, 0.2f);
    set(BlockId::GLASS, 0.3f);
    set(BlockId::TNT, 0.0f);
    set(BlockId::OBSIDIAN, 50.0f, ToolKind::Pickaxe, ToolTier::Diamond);
    for (BlockId id : {BlockId::DANDELION, BlockId::BLUE_ORCHID, BlockId::ALLIUM,
                       BlockId::OXEYE_DAISY, BlockId::SUNFLOWER_BOTTOM,
                       BlockId::SUNFLOWER_TOP}) set(id, 0.0f);
    for (uint8_t raw = static_cast<uint8_t>(BlockId::FLOWING_WATER_1);
         raw <= static_cast<uint8_t>(BlockId::FLOWING_LAVA_7); ++raw)
        set(static_cast<BlockId>(raw), -1.0f, ToolKind::None, ToolTier::None, true);
    for (BlockId id : {BlockId::LIMESTONE, BlockId::BASALT, BlockId::TUFF,
                       BlockId::PACKED_ICE, BlockId::GRANITE})
        set(id, 1.5f, ToolKind::Pickaxe, ToolTier::Wood);
    for (BlockId id : {BlockId::COARSE_DIRT, BlockId::MUD, BlockId::BLACK_SAND})
        set(id, 0.6f, ToolKind::Shovel);
    return values;
}

CraftingRecipe shaped(uint8_t width, uint8_t height,
                      std::initializer_list<ItemId> ingredients,
                      ItemStack output, bool mirror = true) {
    CraftingRecipe recipe;
    recipe.width = width;
    recipe.height = height;
    recipe.output = output;
    recipe.allowMirror = mirror;
    std::copy(ingredients.begin(), ingredients.end(), recipe.ingredients.begin());
    return recipe;
}

void addToolSet(std::vector<CraftingRecipe>& recipes, ItemId material,
                ItemId firstTool) {
    const uint16_t base = static_cast<uint16_t>(firstTool);
    const ItemId E = ItemId::EMPTY;
    const ItemId S = ItemId::STICK;
    recipes.push_back(shaped(3, 3, {material, material, material, E, S, E, E, S, E},
                             {static_cast<ItemId>(base), 1, 0}));
    recipes.push_back(shaped(2, 3, {material, material, material, S, E, S},
                             {static_cast<ItemId>(base + 1), 1, 0}));
    recipes.push_back(shaped(1, 3, {material, S, S},
                             {static_cast<ItemId>(base + 2), 1, 0}));
    recipes.push_back(shaped(2, 3, {material, material, E, S, E, S},
                             {static_cast<ItemId>(base + 3), 1, 0}));
    recipes.push_back(shaped(1, 3, {material, material, S},
                             {static_cast<ItemId>(base + 4), 1, 0}));
}

void addArmorSet(std::vector<CraftingRecipe>& recipes, ItemId material,
                 ItemId firstArmor) {
    const ItemId E = ItemId::EMPTY;
    const uint16_t base = static_cast<uint16_t>(firstArmor);
    recipes.push_back(shaped(3, 2, {material, material, material,
                                    material, E, material},
                             {static_cast<ItemId>(base), 1, 0}, false));
    recipes.push_back(shaped(3, 3, {material, E, material,
                                    material, material, material,
                                    material, material, material},
                             {static_cast<ItemId>(base + 1), 1, 0}, false));
    recipes.push_back(shaped(3, 3, {material, material, material,
                                    material, E, material,
                                    material, E, material},
                             {static_cast<ItemId>(base + 2), 1, 0}, false));
    recipes.push_back(shaped(3, 2, {material, E, material,
                                    material, E, material},
                             {static_cast<ItemId>(base + 3), 1, 0}, false));
}

std::vector<CraftingRecipe> buildRecipes() {
    const ItemId E = ItemId::EMPTY;
    std::vector<CraftingRecipe> recipes;
    for (ItemId log : {ItemId::OAK_LOG, ItemId::BIRCH_LOG, ItemId::SPRUCE_LOG,
                       ItemId::JUNGLE_LOG, ItemId::ACACIA_LOG})
        recipes.push_back(shaped(1, 1, {log}, {ItemId::OAK_PLANKS, 4, 0}, false));
    recipes.push_back(shaped(1, 2, {ItemId::OAK_PLANKS, ItemId::OAK_PLANKS},
                             {ItemId::STICK, 4, 0}, false));
    recipes.push_back(shaped(2, 2, {ItemId::OAK_PLANKS, ItemId::OAK_PLANKS,
                                    ItemId::OAK_PLANKS, ItemId::OAK_PLANKS},
                             {ItemId::CRAFTING_TABLE, 1, 0}, false));
    recipes.push_back(shaped(3, 3, {ItemId::COBBLESTONE, ItemId::COBBLESTONE,
                                    ItemId::COBBLESTONE, ItemId::COBBLESTONE, E,
                                    ItemId::COBBLESTONE, ItemId::COBBLESTONE,
                                    ItemId::COBBLESTONE, ItemId::COBBLESTONE},
                             {ItemId::FURNACE, 1, 0}, false));
    recipes.push_back(shaped(3, 3, {ItemId::OAK_PLANKS, ItemId::OAK_PLANKS,
                                    ItemId::OAK_PLANKS, ItemId::OAK_PLANKS, E,
                                    ItemId::OAK_PLANKS, ItemId::OAK_PLANKS,
                                    ItemId::OAK_PLANKS, ItemId::OAK_PLANKS},
                             {ItemId::CHEST, 1, 0}, false));
    recipes.push_back(shaped(1, 2, {ItemId::COAL, ItemId::STICK},
                             {ItemId::TORCH, 4, 0}, false));
    recipes.push_back(shaped(3, 2, {ItemId::WHITE_WOOL, ItemId::WHITE_WOOL,
                                    ItemId::WHITE_WOOL, ItemId::OAK_PLANKS,
                                    ItemId::OAK_PLANKS, ItemId::OAK_PLANKS},
                             {ItemId::WHITE_BED, 1, 0}, false));
    recipes.push_back(shaped(3, 1, {ItemId::WHEAT, ItemId::WHEAT, ItemId::WHEAT},
                             {ItemId::BREAD, 1, 0}, false));
    recipes.push_back(shaped(3, 3, {ItemId::STRING, ItemId::STICK, E,
                                    ItemId::STRING, E, ItemId::STICK,
                                    ItemId::STRING, ItemId::STICK, E},
                             {ItemId::BOW, 1, 0}));
    recipes.push_back(shaped(3, 1, {ItemId::FLINT, ItemId::STICK, ItemId::FEATHER},
                             {ItemId::ARROW, 4, 0}, false));
    recipes.push_back(shaped(3, 3, {ItemId::OAK_PLANKS, ItemId::IRON_INGOT,
                                    ItemId::OAK_PLANKS, ItemId::OAK_PLANKS,
                                    ItemId::OAK_PLANKS, ItemId::OAK_PLANKS,
                                    E, ItemId::OAK_PLANKS, E},
                             {ItemId::SHIELD, 1, 0}, false));
    recipes.push_back(shaped(2, 1, {ItemId::FLINT, ItemId::IRON_INGOT},
                             {ItemId::FLINT_AND_STEEL, 1, 0}));
    recipes.push_back(shaped(3, 3, {
        ItemId::GUNPOWDER, ItemId::SAND, ItemId::GUNPOWDER,
        ItemId::SAND, ItemId::GUNPOWDER, ItemId::SAND,
        ItemId::GUNPOWDER, ItemId::SAND, ItemId::GUNPOWDER},
        {ItemId::TNT, 1, 0}, false));
    addToolSet(recipes, ItemId::OAK_PLANKS, ItemId::WOODEN_PICKAXE);
    addToolSet(recipes, ItemId::COBBLESTONE, ItemId::STONE_PICKAXE);
    addToolSet(recipes, ItemId::IRON_INGOT, ItemId::IRON_PICKAXE);
    addToolSet(recipes, ItemId::GOLD_INGOT, ItemId::GOLDEN_PICKAXE);
    addToolSet(recipes, ItemId::DIAMOND, ItemId::DIAMOND_PICKAXE);
    addArmorSet(recipes, ItemId::LEATHER, ItemId::LEATHER_HELMET);
    addArmorSet(recipes, ItemId::IRON_INGOT, ItemId::IRON_HELMET);
    addArmorSet(recipes, ItemId::GOLD_INGOT, ItemId::GOLDEN_HELMET);
    addArmorSet(recipes, ItemId::DIAMOND, ItemId::DIAMOND_HELMET);
    return recipes;
}

bool recipeMatches(const CraftingRecipe& recipe, const std::array<ItemId, 9>& grid,
                   uint8_t gridWidth, uint8_t gridHeight, uint8_t offsetX,
                   uint8_t offsetY, bool mirrored) {
    for (uint8_t y = 0; y < gridHeight; ++y) {
        for (uint8_t x = 0; x < gridWidth; ++x) {
            ItemId expected = ItemId::EMPTY;
            if (x >= offsetX && x < offsetX + recipe.width &&
                y >= offsetY && y < offsetY + recipe.height) {
                const uint8_t rx = mirrored
                    ? static_cast<uint8_t>(recipe.width - 1 - (x - offsetX))
                    : static_cast<uint8_t>(x - offsetX);
                expected = recipe.ingredients[(y - offsetY) * recipe.width + rx];
            }
            if (grid[y * gridWidth + x] != expected) return false;
        }
    }
    return true;
}

const auto BLOCKS = buildBlocks();
const auto RECIPES = buildRecipes();
const std::array<SmeltingRecipe, 10> SMELTING = {{
    {ItemId::RAW_IRON, {ItemId::IRON_INGOT, 1, 0}, 200},
    {ItemId::IRON_ORE, {ItemId::IRON_INGOT, 1, 0}, 200},
    {ItemId::RAW_GOLD, {ItemId::GOLD_INGOT, 1, 0}, 200},
    {ItemId::GOLD_ORE, {ItemId::GOLD_INGOT, 1, 0}, 200},
    {ItemId::RAW_BEEF, {ItemId::STEAK, 1, 0}, 200},
    {ItemId::RAW_CHICKEN, {ItemId::COOKED_CHICKEN, 1, 0}, 200},
    {ItemId::RAW_PORKCHOP, {ItemId::COOKED_PORKCHOP, 1, 0}, 200},
    {ItemId::MUTTON, {ItemId::COOKED_MUTTON, 1, 0}, 200},
    {ItemId::SAND, {ItemId::GLASS, 1, 0}, 200},
    {ItemId::RED_SAND, {ItemId::GLASS, 1, 0}, 200}
}};

} // namespace

const BlockSurvivalProperties& getBlockSurvivalProps(BlockId block) {
    return BLOCKS[static_cast<size_t>(block)];
}

std::vector<ItemStack> getBlockDrops(
    BlockId block, const ItemStack& toolStack, uint32_t randomValue) {
    const auto& blockProps = getBlockSurvivalProps(block);
    const auto& tool = toolStack.empty() ? getItemProps(ItemId::EMPTY)
                                         : getItemProps(toolStack.id);
    if (blockProps.unbreakable || !canHarvest(blockProps, tool)) return {};
    switch (block) {
        case BlockId::STONE: return {{ItemId::COBBLESTONE, 1, 0}};
        case BlockId::COAL_ORE: return {{ItemId::COAL, 1, 0}};
        case BlockId::IRON_ORE: return {{ItemId::RAW_IRON, 1, 0}};
        case BlockId::GOLD_ORE: return {{ItemId::RAW_GOLD, 1, 0}};
        case BlockId::DIAMOND_ORE: return {{ItemId::DIAMOND, 1, 0}};
        case BlockId::GLASS:
        case BlockId::SUNFLOWER_TOP: return {};
        case BlockId::TALL_GRASS:
            return randomValue % 8 == 0
                ? std::vector<ItemStack>{{ItemId::WHEAT_SEEDS, 1, 0}}
                : std::vector<ItemStack>{};
        case BlockId::LEAVES:
        case BlockId::BIRCH_LEAVES:
        case BlockId::SPRUCE_LEAVES:
        case BlockId::JUNGLE_LEAVES:
        case BlockId::ACACIA_LEAVES: {
            if (randomValue % 20 != 0) return {};
            const uint16_t offset = block == BlockId::LEAVES ? 0 :
                block == BlockId::BIRCH_LEAVES ? 1 :
                block == BlockId::SPRUCE_LEAVES ? 2 :
                block == BlockId::JUNGLE_LEAVES ? 3 : 4;
            return {{static_cast<ItemId>(static_cast<uint16_t>(ItemId::OAK_SAPLING) + offset), 1, 0}};
        }
        case BlockId::GRAVEL:
            return randomValue % 10 == 0
                ? std::vector<ItemStack>{{ItemId::FLINT, 1, 0}}
                : std::vector<ItemStack>{{ItemId::GRAVEL, 1, 0}};
        case BlockId::WHEAT_7:
            return {{ItemId::WHEAT, 1, 0},
                    {ItemId::WHEAT_SEEDS, static_cast<uint8_t>(1 + randomValue % 4), 0}};
        case BlockId::WHEAT_0: case BlockId::WHEAT_1: case BlockId::WHEAT_2:
        case BlockId::WHEAT_3: case BlockId::WHEAT_4: case BlockId::WHEAT_5:
        case BlockId::WHEAT_6:
            return {{ItemId::WHEAT_SEEDS, 1, 0}};
        default: {
            const ItemId item = itemForBlock(block);
            return item == ItemId::EMPTY ? std::vector<ItemStack>{}
                                         : std::vector<ItemStack>{{item, 1, 0}};
        }
    }
}

float miningSeconds(BlockId block, const ItemStack& toolStack,
                    bool underwater, bool airborne) {
    const auto& blockProps = getBlockSurvivalProps(block);
    if (blockProps.unbreakable) return -1.0f;
    const auto& tool = toolStack.empty() ? getItemProps(ItemId::EMPTY)
                                         : getItemProps(toolStack.id);
    float speed = 1.0f;
    if (tool.tool == blockProps.preferredTool && tool.tool != ToolKind::None) {
        switch (tool.tier) {
            case ToolTier::Wood: speed = 2.0f; break;
            case ToolTier::Stone: speed = 4.0f; break;
            case ToolTier::Iron: speed = 6.0f; break;
            case ToolTier::Gold: speed = 12.0f; break;
            case ToolTier::Diamond: speed = 8.0f; break;
            case ToolTier::None: break;
        }
    }
    const bool harvest = canHarvest(blockProps, tool);
    float seconds = blockProps.hardness * (harvest ? 1.5f : 5.0f) / speed;
    if (underwater) seconds *= 5.0f;
    if (airborne) seconds *= 5.0f;
    return seconds;
}

const std::vector<CraftingRecipe>& craftingRecipes() { return RECIPES; }

const CraftingRecipe* findCraftingRecipe(
    const std::array<ItemId, 9>& grid, uint8_t gridWidth, uint8_t gridHeight) {
    if (gridWidth == 0 || gridWidth > 3 || gridHeight == 0 || gridHeight > 3)
        return nullptr;
    for (const auto& recipe : RECIPES) {
        if (recipe.width > gridWidth || recipe.height > gridHeight) continue;
        for (uint8_t oy = 0; oy <= gridHeight - recipe.height; ++oy) {
            for (uint8_t ox = 0; ox <= gridWidth - recipe.width; ++ox) {
                if (recipeMatches(recipe, grid, gridWidth, gridHeight, ox, oy, false))
                    return &recipe;
                if (recipe.allowMirror &&
                    recipeMatches(recipe, grid, gridWidth, gridHeight, ox, oy, true))
                    return &recipe;
            }
        }
    }
    return nullptr;
}

const SmeltingRecipe* findSmeltingRecipe(ItemId input) {
    const auto it = std::find_if(SMELTING.begin(), SMELTING.end(),
        [input](const SmeltingRecipe& recipe) { return recipe.input == input; });
    return it == SMELTING.end() ? nullptr : &*it;
}

uint16_t fuelTicks(ItemId fuel) {
    if (fuel == ItemId::COAL) return 1600;
    if (fuel == ItemId::STICK) return 100;
    if (fuel == ItemId::OAK_PLANKS) return 300;
    const auto& props = getItemProps(fuel);
    if (props.tool != ToolKind::None && props.tier == ToolTier::Wood) return 200;
    return 0;
}

int armorPointsForItem(ItemId item) {
    static constexpr int armorPoints[4][4] = {
        {1, 3, 2, 1}, {2, 6, 5, 2}, {2, 5, 3, 1}, {3, 8, 6, 3}
    };
    const uint16_t relative = static_cast<uint16_t>(item) -
                              static_cast<uint16_t>(ItemId::LEATHER_HELMET);
    if (relative >= 16) return 0;
    return armorPoints[relative / 4][relative % 4];
}

int totalArmorPoints(const InventoryModel& inventory) {
    int total = 0;
    for (const auto& stack : inventory.armor())
        if (!stack.empty()) total += armorPointsForItem(stack.id);
    return std::min(total, 20);
}

bool canTillBlock(ItemId tool,BlockId target,int faceNormalY){
    return getItemProps(tool).tool==ToolKind::Hoe&&faceNormalY>0&&
        (target==BlockId::DIRT||target==BlockId::GRASS);
}

float durabilityRemaining(const ItemStack& stack) {
    if (stack.empty()) return 0.0f;
    const uint16_t maximum = getItemProps(stack.id).maxDurability;
    if (maximum == 0) return 0.0f;
    return std::clamp(1.0f - static_cast<float>(stack.damage) / maximum, 0.0f, 1.0f);
}
