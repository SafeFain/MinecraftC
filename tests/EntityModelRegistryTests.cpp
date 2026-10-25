#include "entity/EntityModelRegistry.h"
#include "plugins/ContentMutation.h"
#include "plugins/ContentQueries.h"

#include <cstdlib>
#include <iostream>
#include <map>

namespace {
void require(bool value, const char* message) {
    if (!value) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
}

int main() {
    std::map<std::string, int> loads;
    EntityModelRegistry registry([&](const std::filesystem::path& path) {
        ++loads[path.filename().string()];
        model::LoadResult result;
        if (path.filename() == "pig.glb") {
            result.error = "fixture missing";
            return result;
        }
        auto asset = std::make_shared<model::ModelAsset>();
        asset->animations.push_back({"idle", 1.0f, {}});
        result.asset = std::move(asset);
        return result;
    });
    registry.loadAll("fixtures");
    require(loads["cow.glb"] == 1, "cow model was not loaded exactly once");
    require(registry.definition(EntityType::Cow).graph != nullptr &&
            registry.definition(EntityType::Cow).graph->findAction("idle") != nullptr,
            "missing action graph did not create a safe idle fallback");
    require(registry.definition(EntityType::Pig).usesPlaceholder,
            "missing model did not use the placeholder");
    require(registry.definition(EntityType::Pig).asset->materials[0].baseColor ==
                glm::vec4(1.0f, 0.0f, 1.0f, 1.0f),
            "placeholder was not visibly magenta");
    require(registry.playAction(EntityType::Zombie, 42, "attack"),
            "fallback graph did not preserve hostile attack timing");
    const auto fallbackEvents = registry.advance(EntityType::Zombie, 42, 0.31f);
    require(fallbackEvents.size() == 1 && fallbackEvents[0].name == "melee",
            "fallback hostile attack did not emit its gameplay event");
    registry.beginFrame();registry.endFrame();
    require(registry.playing(EntityType::Zombie,42,"attack"),"invisible entity lost its animation state");
    const auto graph=registry.definition(EntityType::Zombie).graph;
    registry.loadAll("fixtures");
    require(loads["cow.glb"]==1 && registry.definition(EntityType::Zombie).graph==graph && registry.playing(EntityType::Zombie,42,"attack"),
            "resource reinitialization reloaded immutable graphs or reset animation state");
    registry.retainInstances({});
    require(!registry.playing(EntityType::Zombie,42,"attack"),"retired entity kept its animation state");
    auto supplied=std::make_shared<model::ModelAsset>();
    supplied->animations.push_back({"idle",1.0f,{}});
    Plugins::EntityContent plugin;
    plugin.key="registry_test:mob";plugin.id=4096;plugin.base=static_cast<uint16_t>(EntityType::Cow);
    plugin.model="unavailable/model.gltf";plugin.asset=supplied;
    Plugins::mutableContentState().extensions.entities.emplace(plugin.id,plugin);
    auto base=plugin;base.id=static_cast<uint16_t>(EntityType::Cow);base.key="registry_test:cow";
    Plugins::mutableContentState().extensions.entities.emplace(base.id,base);
    auto inherited=plugin;inherited.id=4097;inherited.key="registry_test:inherited";inherited.model.clear();inherited.asset.reset();
    Plugins::mutableContentState().extensions.entities.emplace(inherited.id,inherited);
    EntityModelRegistry isolated([](const auto&) {model::LoadResult failure;failure.error="independent cache has no alias";return failure;});
    isolated.loadAll("fixtures");
    require(isolated.definition(static_cast<EntityType>(plugin.id)).asset==supplied &&
        !isolated.definition(static_cast<EntityType>(plugin.id)).usesPlaceholder,"registered asset relied on a cache alias");
    require(isolated.definition(static_cast<EntityType>(inherited.id)).asset==supplied,
        "template model inheritance relied on a cache alias");
    Plugins::mutableContentState().extensions.entities.erase(plugin.id);
    Plugins::mutableContentState().extensions.entities.erase(base.id);
    Plugins::mutableContentState().extensions.entities.erase(inherited.id);
    std::cout << "entity model registry tests passed\n";
}
