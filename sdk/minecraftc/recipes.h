#ifndef MINECRAFTC_RECIPES_H
#define MINECRAFTC_RECIPES_H

#include "plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MC_RECIPES_EXTENSION "minecraftc.recipes"
#define MC_MAX_ITEM_GROUP_MEMBERS 128u
#define MC_MAX_RECIPE_INGREDIENTS 9u

typedef enum MC_RecipeKindV1 {
    MC_RECIPE_SHAPED = 0, MC_RECIPE_SHAPELESS = 1, MC_RECIPE_FURNACE = 2
} MC_RecipeKindV1;

typedef struct MC_ItemGroupV1 {
    uint32_t size;
    const char* key;
    const char* const* items;
    uint32_t count;
} MC_ItemGroupV1;

typedef struct MC_RecipeDefinitionV1 {
    uint32_t size, kind;
    const char* key;
    /* Item keys or "#namespace:group". Null is an empty shaped cell.
       Entries beyond ingredient_count must be null. */
    const char* ingredients[MC_MAX_RECIPE_INGREDIENTS];
    uint32_t ingredient_count;
    const char* output;
    uint32_t count;
    uint32_t width, height, mirror; /* shaped only */
    uint32_t cook_ticks, experience_milli; /* furnace only, one concrete input */
} MC_RecipeDefinitionV1;

typedef struct MC_RecipesV1 {
    uint32_t size, version;
    /* REGISTER only. Descriptors are copied. References resolve at the end
       of package registration, after both data and code have registered. */
    int32_t (*register_item_group)(MC_PluginContext*, const MC_ItemGroupV1*);
    int32_t (*register_recipe)(MC_PluginContext*, const MC_RecipeDefinitionV1*);
} MC_RecipesV1;

#ifdef __cplusplus
}
#endif
#endif
