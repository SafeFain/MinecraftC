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
    AnimationMixer(const AnimationMixer&)=delete;
    AnimationMixer& operator=(const AnimationMixer&)=delete;
    AnimationMixer(AnimationMixer&&)=default;
    AnimationMixer& operator=(AnimationMixer&&)=default;
    AnimationMixer(const ModelAsset* asset, const AnimationGraphAsset* graph);

    void reset(const ModelAsset* asset, const AnimationGraphAsset* graph);
    bool play(const std::string& action, PlayPolicy policy = PlayPolicy::Replace,
              float speed = 1.0f);
    void stop(const std::string& layer);
    void setLayerWeight(const std::string& layer, float weight);
    std::vector<FiredAnimationEvent> advance(float dt);
    void evaluate(Pose& output) const;
    void evaluateLocal(Pose& output) const;
    bool evaluate(ModelInstance& output) const;
    bool playing(const std::string& action) const;

private:
    struct Request { const AnimationActionDefinition* action = nullptr; float speed = 1.0f; const AnimationClip* clip = nullptr; };
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

    struct EvaluationLayer { const AnimationLayerDefinition* definition; LayerState* state; };
    std::vector<EvaluationLayer> m_evaluationLayers;
    uint64_t m_revision = 1;
    mutable uint64_t m_evaluatedRevision = 0;
    mutable Pose m_bind, m_sampled, m_previous, m_transitioned, m_output;

    void start(LayerState& layer, Request request);
};

} // namespace model
