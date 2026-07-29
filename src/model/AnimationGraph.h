#pragma once

#include "model/ModelAsset.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace model {

enum class LayerBlendMode { Override, Additive };

struct AnimationEventDefinition {
    std::string name;
    float time = 0.0f;
};

struct AnimationLayerDefinition {
    std::string name;
    int order = 0;
    LayerBlendMode blend = LayerBlendMode::Override;
    std::vector<float> nodeMask;
};

struct AnimationActionDefinition {
    std::string name;
    std::string clip;
    std::string layer;
    bool loop = false;
    float duration = 0.0f;
    float speed = 1.0f;
    float fadeIn = 0.15f;
    float fadeOut = 0.15f;
    int priority = 0;
    std::vector<AnimationEventDefinition> events;
};

struct AnimationGraphAsset {
    int version = 1;
    std::vector<AnimationLayerDefinition> layers;
    std::map<std::string, AnimationActionDefinition> actions;
    std::map<std::string, std::string> bindings;

    const AnimationActionDefinition* findAction(const std::string& name) const;
    const AnimationLayerDefinition* findLayer(const std::string& name) const;
    std::string actionFor(const std::string& semantic) const;
};

struct AnimationGraphLoadResult {
    std::shared_ptr<AnimationGraphAsset> graph;
    std::string error;
    explicit operator bool() const { return graph != nullptr; }
};

AnimationGraphLoadResult loadAnimationGraph(const std::filesystem::path& path,
                                             const ModelAsset& model);

} // namespace model
