#include "entity/EntityModelRegistry.h"

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
    std::cout << "entity model registry tests passed\n";
}
