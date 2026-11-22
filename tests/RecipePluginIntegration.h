#pragma once

#include "game/InventoryTransaction.h"
#include "world/BlockEntityLogic.h"

extern "C" const MC_Plugin* mc_content_query_recipe_fixture(uint32_t);

namespace RecipePluginIntegration {
inline void install(const std::filesystem::path& source, const std::filesystem::path& profile) {
    write(profile / "plugins.json", R"({"official_heaven":false})");
    std::filesystem::create_directories(profile / "mods");
    std::filesystem::copy(source, profile / "mods/recipe_demo", std::filesystem::copy_options::recursive);
}

inline auto description() {
    const auto& network = Plugins::content().networkDescription;
    std::array<std::string, 3> result{
        network.at("item-group/recipe_demo:stone"),
        network.at("crafting-v1/recipe_demo:blank"),
        network.at("smelting-v1/recipe_demo:tile")};
    return result;
}

inline void crafting() {
    const auto blank = Plugins::resolveItem("recipe_demo:blank");
    const auto& recipes = craftingRecipes();
    const auto& typed = Plugins::content().craftingV1;
    for (const auto stone : {ItemId::GRANITE, ItemId::DIORITE}) {
        std::array<ItemId, 9> grid{};
        grid[0] = ItemId::FLINT;
        grid[3] = stone;
        const auto* result = findCraftingRecipe(grid, 2, 2);
        require(result && result->output.id == blank, "Shapeless 2x2 did not match alternative");
    }

    std::array<ItemId, 9> mirrored{};
    mirrored[0] = ItemId::DIAMOND;
    mirrored[2] = ItemId::DIORITE;
    mirrored[4] = ItemId::FLINT;
    const auto* shaped = findCraftingRecipe(mirrored, 3, 3);
    require(shaped && shaped->output.id == ItemId::GOLD_INGOT, "Mirrored 3x3 did not match");
    require(!findCraftingRecipe(mirrored, 2, 2), "Three-wide recipe matched 2x2");
    mirrored[1] = ItemId::STONE;
    require(!findCraftingRecipe(mirrored, 3, 3), "Shaped recipe accepted an occupied empty cell");

    const auto& flex = typed.at("recipe_extra:flex");
    InventoryModel inventory;
    inventory.slot(0) = {ItemId::GRANITE, 2};
    inventory.slot(1) = {ItemId::DIORITE, 1};
    require(inventory.slot(1).components.setCustomName("Keep this name"), "Component fixture setup failed");
    const auto found = std::ranges::find_if(recipes, [&](const CraftingRecipe& recipe) {
        return recipe.ingredientChoices == flex.ingredientChoices;
    });
    require(found != recipes.end(), "Typed recipe missing from recipe book");
    InventoryTransaction window;
    InventoryAction action;
    action.sequence = 1;
    action.revision = window.revision();
    action.operation = InventoryOperation::FillRecipe;
    action.argument = static_cast<uint16_t>(found - recipes.begin());
    require(window.apply(inventory, action, {}).accepted, "Authoritative overlapping recipe fill failed");
    require(window.crafting()[0].components.customName() == "Keep this name" &&
            window.crafting()[1].id == ItemId::GRANITE && window.crafting()[2].id == ItemId::GRANITE,
            "Overlapping ingredients were assigned incorrectly or lost components");
    action.sequence++;
    action.revision = window.revision();
    action.operation = InventoryOperation::QuickMove;
    action.slot = {InventoryArea::Output, 0};
    require(window.apply(inventory, action, {}).accepted && inventory.count(ItemId::EMERALD) == 1,
            "Quick craft did not consume overlapping ingredients");

    std::array<ItemStack, 9> blocked{};
    blocked[0] = {ItemId::STONE, 1};
    for (size_t i = 0; i < InventoryModel::STORAGE_SIZE; ++i) inventory.slot(i) = {ItemId::COAL, 64};
    inventory.slot(0) = {ItemId::GRANITE, 64};
    inventory.slot(1) = {ItemId::FLINT, 64};
    const auto before = inventory.storage();
    const auto gridBefore = blocked;
    require(!fillCraftingRecipe(typed.at("recipe_demo:blank"), inventory, blocked, 2, 2) &&
            inventory.storage() == before && blocked == gridBefore, "Full recipe-book fill mutated storage");
}

inline void smelting(const std::filesystem::path& worldRoot) {
    const auto blank = Plugins::resolveItem("recipe_demo:blank");
    const auto tile = Plugins::resolveItem("recipe_demo:tile");
    BlockEntity furnace;
    furnace.type = BlockEntityType::Furnace;
    require(containerAccepts(furnace, 0, blank), "Furnace input rejected typed recipe");
    furnace.input = {blank, 1};
    furnace.fuel = {ItemId::COAL, 1};
    for (int i = 0; i < 99; ++i) tickFurnace(furnace);
    require(furnace.output.empty() && furnace.furnaceExperience.empty(), "Incomplete smelt awarded output or XP");
    tickFurnace(furnace);
    require(furnace.input.empty() && furnace.output.id == tile &&
            furnace.furnaceExperience.at("recipe_demo:tile") == 1000, "Furnace did not bank recipe XP");

    SaveStore store(worldRoot);
    WorldMetadata metadata;
    metadata.displayName = "Recipe experience";
    metadata.generationVersion = WorldGenContext::GENERATION_VERSION;
    store.saveMetadata(metadata);
    store.saveBlockEntities(0, 0, {{7, furnace}});
    furnace = SaveStore(worldRoot).loadBlockEntities(0, 0).at(0).value;
    const auto pending = furnace.furnaceExperience;
    InventoryTransaction window;
    InventoryModel inventory;
    InventoryContext context;
    context.container = &furnace;
    InventoryAction action;
    action.sequence = 1;
    action.revision = window.revision();
    action.operation = InventoryOperation::QuickMove;
    action.slot = {InventoryArea::Container, 2};
    const auto result = window.apply(inventory, action, context);
    require(result.accepted && pending.size() == 1 && result.furnaceExperience == pending &&
            furnace.furnaceExperience.empty() && inventory.count(tile) == 1, "Persisted experience was not claimed");
    require(window.apply(inventory, action, context).furnaceExperience.empty(), "Replayed smelting claim paid again");

    for (size_t i = 0; i < BlockEntity::MAX_FURNACE_EXPERIENCE_RECIPES; ++i)
        furnace.furnaceExperience.emplace("recipe_test:" + std::to_string(i), 1);
    furnace.input = {blank, 1};
    furnace.burnRemaining = 2;
    furnace.cookProgress = 99;
    tickFurnace(furnace);
    require(furnace.furnaceExperience.size() == BlockEntity::MAX_FURNACE_EXPERIENCE_RECIPES &&
            furnace.furnaceExperience.at("recipe_test:0") == 1001, "Full XP ledger lost overflow reward");
    store.saveBlockEntities(0, 0, {{7, furnace}});
}

inline int run(const std::filesystem::path& data, const std::filesystem::path& module,
               const std::filesystem::path& staged) {
    const auto root = data.parent_path().parent_path() / "runtime";
    const std::filesystem::path assets = MINECRAFTC_SOURCE_DIR "/assets";
    const auto external = root / "data";
    install(data, external);
    const auto extra = external / "mods/recipe_extra";
    write(extra / "mod.json", R"({"id":"recipe_extra","name":"Extra","version":"1.0.0","api":1,"package_format":1,"dependencies":[{"id":"recipe_demo"}]})");
    write(extra / "item_groups/granite.json", R"({"key":"recipe_extra:granite","items":["minecraftc:granite"]})");
    write(extra / "recipes/flex.json", R"({"key":"recipe_extra:flex","type":"shapeless","ingredients":["#recipe_demo:stone","#recipe_extra:granite","minecraftc:granite"],"result":{"item":"minecraftc:emerald"}})");
    write(extra / "recipes/mirror.json", R"({"key":"recipe_extra:mirror","type":"shaped","width":3,"height":2,"ingredients":["#recipe_demo:stone",null,"minecraftc:diamond",null,"minecraftc:flint",null],"result":{"item":"minecraftc:gold_ingot"}})");

    const auto bad = external / "mods/recipe_bad";
    write(bad / "mod.json", R"({"id":"recipe_bad","name":"Bad","version":"1.0.0","api":1,"package_format":1})");
    write(bad / "item_groups/ok.json", R"({"key":"recipe_bad:ok","items":["minecraftc:flint"]})");
    write(bad / "recipes/invalid.json", R"({"key":"recipe_bad:missing","type":"shapeless","ingredients":["#recipe_bad:missing"],"result":{"item":"minecraftc:diamond"}})");
    const auto dependent = external / "mods/recipe_blocked";
    write(dependent / "mod.json", R"({"id":"recipe_blocked","name":"Blocked","version":"1.0.0","api":1,"package_format":1,"dependencies":[{"id":"recipe_bad"}]})");

    Plugins::PluginManager dataManager({assets, external});
    dataManager.initialize();
    const auto expected = description();
    auto status = [&](const std::string& id) -> const Plugins::PluginInfo& {
        const auto entry = std::ranges::find_if(dataManager.plugins(), [&](const auto& plugin) {
            return plugin.manifest.id == id;
        });
        require(entry != dataManager.plugins().end(), "Missing recipe plugin status");
        return *entry;
    };
    require(!status("recipe_bad").active && status("recipe_bad").status.find("recipes/invalid.json") != std::string::npos &&
            status("recipe_bad").status.find("ingredients/0") != std::string::npos &&
            !Plugins::content().itemGroups.contains("recipe_bad:ok") &&
            !Plugins::content().networkDescription.contains("item-group/recipe_bad:ok") &&
            !status("recipe_blocked").active, "Failed recipe package left registrations or blocked diagnosis");
    crafting();
    smelting(root / "world");
    dataManager.shutdown();

    const auto desktop = root / "desktop";
    install(module, desktop);
    Plugins::PluginManager nativeManager({assets, desktop});
    nativeManager.initialize();
    require(description() == expected, "Native and data recipe descriptions differ");
    nativeManager.shutdown();

    auto builtins = Plugins::builtinPlugins();
    const auto stage = staged.generic_string(), assetPath = assets.generic_string();
    builtins.push_back({"recipe_demo", "Kiln Recipe Demo", "content-package-v1",
        "plugins/recipe_demo", false, true, mc_content_query_recipe_fixture, true,
        stage.c_str(), assetPath.c_str()});
    const auto compiled = root / "compiled";
    write(compiled / "plugins.json", R"({"official_heaven":false})");
    Plugins::PluginManager builtinManager({assets, compiled}, {}, builtins);
    builtinManager.initialize();
    require(description() == expected, "Compiled recipe description differs");
    builtinManager.shutdown();
    std::cout << "Recipe package delivery, crafting and furnace persistence passed\n";
    return 0;
}
}
