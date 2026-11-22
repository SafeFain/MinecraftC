#include <minecraftc/recipes.h>
#include <stddef.h>

static int32_t load(const MC_Host* host, MC_PluginContext* context) {
    if (host->size < offsetof(MC_Host, query_extension) + sizeof(host->query_extension))
        return -1;
    const MC_RecipesV1* recipes = host->query_extension(context, MC_RECIPES_EXTENSION, 1);
    if (!recipes || recipes->size < sizeof(*recipes) || recipes->version != 1)
        return -1;

    const MC_RecipeDefinitionV1 blank = {
        .size = sizeof(blank),
        .kind = MC_RECIPE_SHAPELESS,
        .key = "recipe_demo:blank",
        .ingredients = {"#recipe_demo:stone", "minecraftc:flint"},
        .ingredient_count = 2,
        .output = "recipe_demo:blank",
        .count = 1
    };
    const char* members[] = {"minecraftc:diorite", "minecraftc:granite"};
    const MC_ItemGroupV1 stones = {sizeof(stones), "recipe_demo:stone", members, 2};
    const MC_RecipeDefinitionV1 tile = {
        .size = sizeof(tile),
        .kind = MC_RECIPE_FURNACE,
        .key = "recipe_demo:tile",
        .ingredients = {"recipe_demo:blank"},
        .ingredient_count = 1,
        .output = "recipe_demo:tile",
        .count = 1,
        .cook_ticks = 100,
        .experience_milli = 1000
    };
    if (recipes->register_recipe(context, &blank) != 0 ||
        recipes->register_item_group(context, &stones) != 0)
        return -1;
    return recipes->register_recipe(context, &tile);
}

static const MC_Plugin plugin = {sizeof(plugin), MC_PLUGIN_ABI, "recipe_demo", "1.0.0", load, NULL};

MC_PLUGIN_EXPORT const MC_Plugin* MCPlugin_Query(uint32_t abi) {
    return abi == MC_PLUGIN_ABI ? &plugin : NULL;
}
