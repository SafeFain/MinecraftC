#pragma once

#include "model/AnimationGraph.h"
#include "model/ModelAnimation.h"

#include <deque>
#include <map>
#include <string>
#include <vector>

namespace model {

enum class PlayPolicy { Replace, Queue };

struct FiredAnimationEvent {
    std::string action;
    std::string name;
};

class AnimationMixer {
public:
    AnimationMixer() = default;
    AnimationMixer(const ModelAsset* asset, const AnimationGraphAsset* graph);

    void reset(const ModelAsset* asset, const AnimationGraphAsset* graph);
    bool play(const std::string& action, PlayPolicy policy = PlayPolicy::Replace,
              float speed = 1.0f);
    void stop(const std::string& layer);
    void setLayerWeight(const std::string& layer, float weight);
    std::vector<FiredAnimationEvent> advance(float dt);
    void evaluate(Pose& output) const;
    bool playing(const std::string& action) const;

private:
    struct Request { const AnimationActionDefinition* action = nullptr; float speed = 1.0f; };
    struct LayerState {
        Request current;
        Request previous;
        float time = 0.0f;
        float previousTime = 0.0f;
        float transition = 1.0f;
        float weight = 1.0f;
        std::deque<Request> queue;
    };
    const ModelAsset* m_asset = nullptr;
    const AnimationGraphAsset* m_graph = nullptr;
    std::map<std::string, LayerState> m_layers;

    void start(LayerState& layer, Request request);
};

} // namespace model
