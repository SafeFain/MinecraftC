#include "model/AnimationGraph.h"
#include "model/AnimationMixer.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}

model::Channel translation(int node, glm::vec3 first, glm::vec3 second) {
    model::Channel channel;
    channel.node = node;
    channel.path = model::ChannelPath::Translation;
    channel.interpolation = model::Interpolation::Linear;
    channel.times = {0.0f, 1.0f};
    channel.values = {glm::vec4(first, 0), glm::vec4(second, 0)};
    return channel;
}
}

int main() {
    model::ModelAsset asset;
    model::Node root; root.name = "root"; root.children = {1};
    model::Node arm; arm.name = "arm"; arm.parent = 0;
    asset.nodes = {root, arm};
    asset.sceneRoots = {0};
    asset.animations = {
        {"idle", 1.0f, {translation(0, {0,0,0}, {0,0,0})}},
        {"walk", 1.0f, {translation(0, {0,0,0}, {2,0,0})}},
        {"wave", 1.0f, {translation(1, {0,0,0}, {0,2,0})}},
        {"hurt", 1.0f, {translation(0, {0,0,0}, {0,1,0})}}
    };

    model::AnimationGraphAsset graph;
    graph.layers = {
        {"base", 0, model::LayerBlendMode::Override, {1,1}},
        {"upper", 10, model::LayerBlendMode::Override, {0,1}},
        {"reaction", 20, model::LayerBlendMode::Additive, {1,1}}
    };
    graph.actions.emplace("walk", model::AnimationActionDefinition{
        "walk","walk","base",true,1,1,0,0,0,{}});
    graph.actions.emplace("wave", model::AnimationActionDefinition{
        "wave","wave","upper",false,1,1,0,0,10,{{"impact",.5f}}});
    graph.actions.emplace("hurt", model::AnimationActionDefinition{
        "hurt","hurt","reaction",false,1,1,0,0,20,{}});

    model::AnimationMixer mixer(&asset, &graph);
    require(mixer.play("walk") && mixer.play("wave") && mixer.play("hurt"),
            "mixer rejected valid actions");
    auto events = mixer.advance(0.5f);
    require(events.size() == 1 && events[0].name == "impact",
            "event was not fired at the crossing time");
    model::Pose pose;
    mixer.evaluate(pose);
    require(std::abs(pose.translation[0].x - 1.0f) < 0.001f &&
            std::abs(pose.translation[0].y - 0.5f) < 0.001f &&
            std::abs(pose.translation[1].y - 1.0f) < 0.001f,
            "override mask or additive layer produced the wrong pose");
    require(mixer.advance(0.1f).empty(), "event fired more than once");
    require(mixer.play("wave", model::PlayPolicy::Queue), "queue request failed");
    mixer.advance(0.5f);
    require(mixer.playing("wave"), "queued action did not start");

    const std::filesystem::path valid =
        std::filesystem::temp_directory_path() / "minecraftc-animation-graph-valid.json";
    const std::filesystem::path invalid =
        std::filesystem::temp_directory_path() / "minecraftc-animation-graph-invalid.json";
    {
        std::ofstream out(valid);
        out << R"({"version":1,"layers":[{"name":"base","order":0,"blend":"override"}],
               "actions":{"idle":{"clip":"idle","layer":"base","loop":true}},
               "bindings":{"idle":"idle"}})";
    }
    {
        std::ofstream out(invalid);
        out << R"({"version":1,"layers":[{"name":"bad","mask":{"nodes":{"missing":1}}}],
               "actions":{}})";
    }
    const auto loaded = model::loadAnimationGraph(valid, asset);
    require(loaded && loaded.graph->actionFor("idle") == "idle",
            "valid action graph was not loaded");
    require(!model::loadAnimationGraph(invalid, asset),
            "unknown mask node was accepted");
    std::filesystem::remove(valid);
    std::filesystem::remove(invalid);

    std::cout << "animation graph tests passed\n";
}
