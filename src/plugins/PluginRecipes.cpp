#include "plugins/PluginManager.h"
#include "plugins/ContentMutation.h"
#include "plugins/ContentQueries.h"

#include <algorithm>
#include <cmath>

namespace Plugins {
namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

uint32_t number(const Json& json, const char* field, uint32_t fallback) {
    const double value = json.numeric(field, fallback);
    require(value >= 0 && value <= UINT16_MAX && std::floor(value) == value,
            json.source + ": Expected bounded integer for " + field);
    return static_cast<uint32_t>(value);
}

ItemId recipeItem(const std::string& key, const std::string& field) {
    try {
        const auto item = resolveItem(key);
        if (item == ItemId::EMPTY || !availableItem(item))
            throw std::runtime_error("Expected an available nonempty item");
        return item;
    } catch (const std::exception& error) {
        throw std::runtime_error(field + " (" + key + "): " + error.what());
    }
}
}

void PluginManager::itemGroup(MC_PluginContext& ctx, const MC_ItemGroupV1& definition) {
    require(definition.size >= sizeof(definition) && definition.key && definition.items &&
            definition.count > 0 && definition.count <= MC_MAX_ITEM_GROUP_MEMBERS,
            "Invalid item group descriptor");
    const std::string key = definition.key;
    owned(ctx, key);
    std::vector<std::string> items;
    items.reserve(definition.count);
    for (uint32_t i = 0; i < definition.count; ++i) {
        require(definition.items[i], "Missing item group member");
        reference(ctx, definition.items[i]);
        items.emplace_back(definition.items[i]);
    }
    require(ctx.pendingItemGroups.emplace(key, std::move(items)).second, "Duplicate item group: " + key);
    m_definitionSources.try_emplace({"item_groups", key}, m_info[ctx.index].manifest.id + " / C SDK");
}

void PluginManager::recipeV1(MC_PluginContext& ctx, const MC_RecipeDefinitionV1& definition) {
    require(definition.size >= sizeof(definition) && definition.key && definition.output &&
            definition.kind <= MC_RECIPE_FURNACE && definition.count >= 1 && definition.count <= 64 &&
            definition.ingredient_count >= 1 && definition.ingredient_count <= MC_MAX_RECIPE_INGREDIENTS,
            "Invalid recipe descriptor");
    const std::string key = definition.key;
    owned(ctx, key);
    reference(ctx, definition.output);
    const bool shaped = definition.kind == MC_RECIPE_SHAPED;
    const bool furnace = definition.kind == MC_RECIPE_FURNACE;
    if (shaped) {
        require(definition.width >= 1 && definition.width <= 3 &&
                definition.height >= 1 && definition.height <= 3 &&
                definition.ingredient_count == definition.width * definition.height && definition.mirror <= 1,
                "Invalid shaped recipe dimensions or mirror flag");
    } else {
        require(!definition.width && !definition.height && !definition.mirror,
                "Only shaped recipes accept dimensions or mirroring");
    }
    if (furnace) {
        require(definition.ingredient_count == 1 && definition.cook_ticks > 0 &&
                definition.cook_ticks <= UINT16_MAX && definition.experience_milli <= UINT16_MAX,
                "Invalid furnace input count, cook duration or experience");
    } else {
        require(!definition.cook_ticks && !definition.experience_milli,
                "Only furnace recipes accept cook duration or experience");
    }

    MC_PluginContext::PendingRecipe recipe{{}, definition.output, definition.kind, definition.count,
        definition.width, definition.height, definition.mirror, definition.cook_ticks, definition.experience_milli};
    bool hasIngredient = false;
    for (size_t i = 0; i < recipe.ingredients.size(); ++i) {
        const auto* ingredient = definition.ingredients[i];
        if (i >= definition.ingredient_count) {
            require(!ingredient, "Ingredient past ingredient_count");
            continue;
        }
        if (!ingredient) {
            require(shaped, "Only shaped recipes accept empty cells");
            continue;
        }
        const bool group = ingredient[0] == '#';
        require(!group || !furnace, "Furnace input must be one concrete item");
        reference(ctx, group ? ingredient + 1 : ingredient);
        recipe.ingredients[i] = ingredient;
        hasIngredient = true;
    }
    require(hasIngredient, "A recipe needs at least one ingredient");
    const auto& state = content();
    require(!state.crafting.count(key) && !state.smelting.count(key) &&
            ctx.pendingRecipes.emplace(key, std::move(recipe)).second, "Duplicate recipe: " + key);
    m_definitionSources.try_emplace({"recipes", key}, m_info[ctx.index].manifest.id + " / C SDK");
}

void PluginManager::loadRecipeData(MC_PluginContext& ctx, const Json& data) {
    if (data.has("item_groups")) {
        const auto& groups = data.at("item_groups");
        require(groups.type == Json::Type::Array, "Item groups must be an array");
        for (const auto& group : groups.array) {
            const auto key = group.at("key").text();
            const auto& values = group.at("items");
            require(values.type == Json::Type::Array && values.array.size() <= MC_MAX_ITEM_GROUP_MEMBERS,
                    values.source + ": Expected an item group member array");
            std::vector<const char*> items;
            items.reserve(values.array.size());
            for (const auto& item : values.array) {
                require(item.type == Json::Type::String, item.source + ": Expected item key");
                items.push_back(item.string.c_str());
            }
            itemGroup(ctx, {sizeof(MC_ItemGroupV1), key.c_str(), items.data(), static_cast<uint32_t>(items.size())});
        }
    }
    if (!data.has("recipes")) return;
    const auto& recipes = data.at("recipes");
    require(recipes.type == Json::Type::Array, "Recipes must be an array");
    for (const auto& json : recipes.array) {
        if (!json.has("type")) continue;
        for (const auto* field : {"operation", "target", "smelting", "output", "count"})
            require(!json.has(field), std::string("Typed recipe cannot use legacy field ") + field);

        MC_RecipeDefinitionV1 definition{};
        definition.size = sizeof(definition);
        const auto type = json.at("type").text();
        const auto key = json.at("key").text();
        definition.key = key.c_str();
        const auto& result = json.at("result");
        const auto output = result.at("item").text();
        definition.output = output.c_str();
        definition.count = number(result, "count", 1);

        if (type == "shaped") definition.kind = MC_RECIPE_SHAPED;
        else if (type == "shapeless") definition.kind = MC_RECIPE_SHAPELESS;
        else if (type == "furnace") definition.kind = MC_RECIPE_FURNACE;
        else throw std::runtime_error("Unknown recipe type: " + type);

        if (definition.kind == MC_RECIPE_SHAPED) {
            definition.width = number(json, "width", 0);
            definition.height = number(json, "height", 0);
            definition.mirror = json.flag("mirror", true);
        } else {
            require(!json.has("width") && !json.has("height") && !json.has("mirror"),
                    "Only shaped recipes accept width, height or mirroring");
        }
        if (definition.kind == MC_RECIPE_FURNACE) {
            definition.cook_ticks = number(json, "cook_ticks", 200);
            definition.experience_milli = number(json, "experience_milli", 0);
        } else {
            require(!json.has("cook_ticks") && !json.has("experience_milli"),
                    "Only furnace recipes accept cook duration or experience");
        }

        const auto& ingredients = json.at("ingredients");
        require(ingredients.type == Json::Type::Array && ingredients.array.size() <= MC_MAX_RECIPE_INGREDIENTS,
                ingredients.source + ": Expected a recipe ingredient array");
        definition.ingredient_count = static_cast<uint32_t>(ingredients.array.size());
        for (size_t i = 0; i < ingredients.array.size(); ++i) {
            const auto& entry = ingredients.array[i];
            if (entry.type == Json::Type::Null) continue;
            require(entry.type == Json::Type::String, entry.source + ": Expected item or group key");
            definition.ingredients[i] = entry.string.c_str();
        }
        recipeV1(ctx, definition);
    }
}

void PluginManager::resolveRecipes(MC_PluginContext& ctx) {
    auto& state = mutableContentState();
    for (const auto& [key, members] : ctx.pendingItemGroups) {
        validateDefinition("item_groups", key, [&] {
            std::vector<ItemId> ids;
            ids.reserve(members.size());
            for (size_t i = 0; i < members.size(); ++i)
                ids.push_back(recipeItem(members[i], "items/" + std::to_string(i)));
            std::sort(ids.begin(), ids.end());
            require(std::adjacent_find(ids.begin(), ids.end()) == ids.end(), "Duplicate item group member");
            std::vector<std::string> stableKeys;
            for (auto id : ids) stableKeys.push_back(itemKey(id));
            std::sort(stableKeys.begin(), stableKeys.end());
            state.networkDescription["item-group/" + key] = nlohmann::json(stableKeys).dump();
            state.itemGroups.emplace(key, std::move(ids));
        });
    }
    for (const auto& [key, recipe] : ctx.pendingRecipes) {
        validateDefinition("recipes", key, [&] {
            const ItemStack output{recipeItem(recipe.output, "result/item"), static_cast<uint8_t>(recipe.count)};
            require(output.count <= getItemProps(output.id).maxStack, "result/count exceeds stack limit");
            const auto result = nlohmann::json::array({itemKey(output.id), output.count});
            if (recipe.kind == MC_RECIPE_FURNACE) {
                const auto input = recipeItem(recipe.ingredients[0], "ingredients/0");
                state.smeltingV1.emplace(key, SmeltingRecipe{input, output, static_cast<uint16_t>(recipe.cookTicks),
                    static_cast<uint16_t>(recipe.experienceMilli)});
                state.networkDescription["smelting-v1/" + key] =
                    nlohmann::json::array({itemKey(input), result, recipe.cookTicks, recipe.experienceMilli}).dump();
                return;
            }

            auto choices = std::make_shared<CraftingIngredients>();
            auto ingredients = nlohmann::json::array();
            for (size_t i = 0; i < recipe.ingredients.size(); ++i) {
                const auto& entry = recipe.ingredients[i];
                if (entry.empty()) {
                    ingredients.push_back(nullptr);
                } else if (entry.front() == '#') {
                    const auto group = state.itemGroups.find(entry.substr(1));
                    require(group != state.itemGroups.end(), "ingredients/" + std::to_string(i) + ": Unknown item group " + entry);
                    (*choices)[i] = group->second;
                    ingredients.push_back(entry);
                } else {
                    const auto item = recipeItem(entry, "ingredients/" + std::to_string(i));
                    (*choices)[i].push_back(item);
                    ingredients.push_back(itemKey(item));
                }
            }
            CraftingRecipe crafting;
            crafting.width = static_cast<uint8_t>(recipe.width);
            crafting.height = static_cast<uint8_t>(recipe.height);
            crafting.allowMirror = recipe.mirror != 0;
            crafting.allowPlankVariants = false;
            crafting.shapeless = recipe.kind == MC_RECIPE_SHAPELESS;
            crafting.output = output;
            crafting.ingredientChoices = std::move(choices);
            state.craftingV1.emplace(key, std::move(crafting));
            state.networkDescription["crafting-v1/" + key] =
                nlohmann::json::array({recipe.kind, recipe.width, recipe.height, recipe.mirror, ingredients, result}).dump();
        });
    }
    ctx.pendingItemGroups.clear();
    ctx.pendingRecipes.clear();
}
} // namespace Plugins
