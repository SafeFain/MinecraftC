#include "model/AnimationGraph.h"
#include "core/AssetStore.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace model {
namespace {
using Json = nlohmann::json;

std::string context(const std::filesystem::path& path, const std::string& error) {
    return path.u8string() + ": " + error;
}

bool finiteNonNegative(float value) {
    return std::isfinite(value) && value >= 0.0f;
}

void includeDescendants(const ModelAsset& model, std::size_t node,
                        float weight, std::vector<float>& mask) {
    if (node >= model.nodes.size()) return;
    mask[node] = std::max(mask[node], weight);
    for (int child : model.nodes[node].children)
        if (child >= 0) includeDescendants(model, static_cast<std::size_t>(child),
                                           weight, mask);
}
}

const AnimationActionDefinition* AnimationGraphAsset::findAction(
    const std::string& name) const {
    const auto found = actions.find(name);
    return found == actions.end() ? nullptr : &found->second;
}

const AnimationLayerDefinition* AnimationGraphAsset::findLayer(
    const std::string& name) const {
    for (const auto& layer : layers) if (layer.name == name) return &layer;
    return nullptr;
}

std::string AnimationGraphAsset::actionFor(const std::string& semantic) const {
    const auto found = bindings.find(semantic);
    return found == bindings.end() ? semantic : found->second;
}

AnimationGraphLoadResult loadAnimationGraph(const std::filesystem::path& path,
                                             const ModelAsset& model) {
    try {
        std::istringstream input(AssetStore::readTextPath(path));
        Json root;
        input >> root;
        if (!root.is_object() || root.value("version", 0) != 1)
            return {{}, context(path, "unsupported or missing version")};

        auto graph = std::make_shared<AnimationGraphAsset>();
        graph->version = 1;
        if (!root.contains("layers") || !root["layers"].is_array() ||
            root["layers"].empty())
            return {{}, context(path, "layers must be a non-empty array")};

        std::set<std::string> layerNames;
        for (const Json& source : root["layers"]) {
            AnimationLayerDefinition layer;
            layer.name = source.at("name").get<std::string>();
            layer.order = source.value("order", 0);
            const std::string blend = source.value("blend", "override");
            if (blend == "override") layer.blend = LayerBlendMode::Override;
            else if (blend == "additive") layer.blend = LayerBlendMode::Additive;
            else return {{}, context(path, "unknown blend mode " + blend)};
            if (layer.name.empty() || !layerNames.insert(layer.name).second)
                return {{}, context(path, "duplicate or empty layer name")};
            layer.nodeMask.assign(model.nodes.size(), 0.0f);
            if (!source.contains("mask")) {
                std::fill(layer.nodeMask.begin(), layer.nodeMask.end(), 1.0f);
            } else {
                const Json& mask = source["mask"];
                const bool descendants = mask.value("include_descendants", true);
                const Json& nodes = mask.at("nodes");
                if (!nodes.is_object())
                    return {{}, context(path, "mask nodes must be an object")};
                for (auto item = nodes.begin(); item != nodes.end(); ++item) {
                    const float weight = item.value().get<float>();
                    if (!finiteNonNegative(weight) || weight > 1.0f)
                        return {{}, context(path, "mask weight is outside 0..1")};
                    auto node = std::find_if(model.nodes.begin(), model.nodes.end(),
                        [&](const Node& candidate) { return candidate.name == item.key(); });
                    if (node == model.nodes.end())
                        return {{}, context(path, "unknown mask node " + item.key())};
                    const std::size_t index = static_cast<std::size_t>(
                        std::distance(model.nodes.begin(), node));
                    if (descendants) includeDescendants(model, index, weight,
                                                        layer.nodeMask);
                    else layer.nodeMask[index] = weight;
                }
            }
            graph->layers.push_back(std::move(layer));
        }
        std::stable_sort(graph->layers.begin(), graph->layers.end(),
            [](const auto& a, const auto& b) { return a.order < b.order; });

        if (!root.contains("actions") || !root["actions"].is_object())
            return {{}, context(path, "actions must be an object")};
        for (auto item = root["actions"].begin(); item != root["actions"].end(); ++item) {
            const Json& source = item.value();
            AnimationActionDefinition action;
            action.name = item.key();
            action.clip = source.at("clip").get<std::string>();
            action.layer = source.at("layer").get<std::string>();
            action.loop = source.value("loop", false);
            action.speed = source.value("speed", 1.0f);
            action.fadeIn = source.value("fade_in", 0.15f);
            action.fadeOut = source.value("fade_out", 0.15f);
            action.priority = source.value("priority", 0);
            if (!graph->findLayer(action.layer))
                return {{}, context(path, "action uses unknown layer " + action.layer)};
            const AnimationClip* clip = model.findClip(action.clip);
            action.duration = source.value("duration", clip ? clip->duration : 0.0f);
            if (!clip || !finiteNonNegative(action.duration) || action.duration <= 0.0f ||
                !std::isfinite(action.speed) || action.speed <= 0.0f ||
                !finiteNonNegative(action.fadeIn) || !finiteNonNegative(action.fadeOut))
                return {{}, context(path, "invalid clip or timing for action " + action.name)};
            if (source.contains("events")) {
                for (const Json& eventSource : source["events"]) {
                    AnimationEventDefinition event;
                    event.name = eventSource.at("name").get<std::string>();
                    event.time = eventSource.at("time").get<float>();
                    if (event.name.empty() || !finiteNonNegative(event.time) ||
                        event.time > action.duration)
                        return {{}, context(path, "invalid event in action " + action.name)};
                    action.events.push_back(std::move(event));
                }
                std::stable_sort(action.events.begin(), action.events.end(),
                    [](const auto& a, const auto& b) { return a.time < b.time; });
            }
            graph->actions.emplace(action.name, std::move(action));
        }
        if (root.contains("bindings")) {
            if (!root["bindings"].is_object())
                return {{}, context(path, "bindings must be an object")};
            for (auto item = root["bindings"].begin(); item != root["bindings"].end(); ++item) {
                const std::string action = item.value().get<std::string>();
                if (!graph->findAction(action))
                    return {{}, context(path, "binding uses unknown action " + action)};
                graph->bindings.emplace(item.key(), action);
            }
        }
        return {std::move(graph), {}};
    } catch (const std::exception& error) {
        return {{}, context(path, error.what())};
    }
}

} // namespace model
