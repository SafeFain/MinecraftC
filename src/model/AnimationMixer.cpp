#include "model/AnimationMixer.h"

#include <algorithm>
#include <cmath>

namespace model {
namespace {
glm::quat safeNormalize(const glm::quat& value) {
    const float length = glm::length(value);
    return length > 0.000001f ? value / length : glm::quat(1, 0, 0, 0);
}

void applyOverride(Pose& base, const Pose& layer, const std::vector<float>& mask,
                   float weight) {
    const std::size_t count = std::min({base.translation.size(), layer.translation.size(),
                                        mask.size()});
    for (std::size_t node = 0; node < count; ++node) {
        const float amount = std::clamp(mask[node] * weight, 0.0f, 1.0f);
        base.translation[node] = glm::mix(base.translation[node], layer.translation[node], amount);
        base.scale[node] = glm::mix(base.scale[node], layer.scale[node], amount);
        glm::quat target = layer.rotation[node];
        if (glm::dot(base.rotation[node], target) < 0.0f) target = -target;
        base.rotation[node] = safeNormalize(glm::slerp(base.rotation[node], target, amount));
    }
}

void applyAdditive(Pose& base, const Pose& layer, const Pose& bind,
                   const std::vector<float>& mask, float weight) {
    const std::size_t count = std::min({base.translation.size(), layer.translation.size(),
                                        bind.translation.size(), mask.size()});
    for (std::size_t node = 0; node < count; ++node) {
        const float amount = std::clamp(mask[node] * weight, 0.0f, 1.0f);
        base.translation[node] += (layer.translation[node] - bind.translation[node]) * amount;
        const glm::vec3 ratio(
            bind.scale[node].x != 0.0f ? layer.scale[node].x / bind.scale[node].x : 1.0f,
            bind.scale[node].y != 0.0f ? layer.scale[node].y / bind.scale[node].y : 1.0f,
            bind.scale[node].z != 0.0f ? layer.scale[node].z / bind.scale[node].z : 1.0f);
        base.scale[node] *= glm::mix(glm::vec3(1.0f), ratio, amount);
        const glm::quat delta = safeNormalize(layer.rotation[node] *
                                              glm::inverse(bind.rotation[node]));
        base.rotation[node] = safeNormalize(glm::slerp(glm::quat(1,0,0,0), delta,
                                                       amount) * base.rotation[node]);
    }
}
}

AnimationMixer::AnimationMixer(const ModelAsset* asset,
                               const AnimationGraphAsset* graph) {
    reset(asset, graph);
}

void AnimationMixer::reset(const ModelAsset* asset, const AnimationGraphAsset* graph) {
    m_asset = asset;
    m_graph = graph;
    m_layers.clear();
    if (graph) for (const auto& layer : graph->layers) m_layers.emplace(layer.name, LayerState{});
}

void AnimationMixer::start(LayerState& layer, Request request) {
    layer.previous = layer.current;
    layer.previousTime = layer.time;
    layer.current = request;
    layer.time = 0.0f;
    layer.transition = request.action && request.action->fadeIn > 0.0f ? 0.0f : 1.0f;
}

bool AnimationMixer::play(const std::string& actionName, PlayPolicy policy,
                          float speed) {
    if (!m_graph || !std::isfinite(speed) || speed <= 0.0f) return false;
    const AnimationActionDefinition* action = m_graph->findAction(actionName);
    if (!action) return false;
    auto found = m_layers.find(action->layer);
    if (found == m_layers.end()) return false;
    LayerState& layer = found->second;
    Request request{action, speed};
    if (policy == PlayPolicy::Queue && layer.current.action) {
        layer.queue.push_back(request);
        return true;
    }
    if (layer.current.action && !layer.current.action->loop &&
        action->priority < layer.current.action->priority) return false;
    if (layer.current.action == action && action->loop) {
        layer.current.speed = speed;
        return true;
    }
    start(layer, request);
    return true;
}

void AnimationMixer::stop(const std::string& layerName) {
    auto found = m_layers.find(layerName);
    if (found == m_layers.end()) return;
    found->second = LayerState{};
}

void AnimationMixer::setLayerWeight(const std::string& layerName, float weight) {
    auto found = m_layers.find(layerName);
    if (found != m_layers.end()) found->second.weight = std::clamp(weight, 0.0f, 1.0f);
}

std::vector<FiredAnimationEvent> AnimationMixer::advance(float dt) {
    std::vector<FiredAnimationEvent> fired;
    if (!m_graph || dt <= 0.0f || !std::isfinite(dt)) return fired;
    for (auto& pair : m_layers) {
        LayerState& layer = pair.second;
        const AnimationActionDefinition* action = layer.current.action;
        if (!action) continue;
        const float oldTime = layer.time;
        const float amount = dt * action->speed * layer.current.speed;
        layer.time += amount;
        layer.previousTime += dt * (layer.previous.action
            ? layer.previous.action->speed * layer.previous.speed : 1.0f);
        if (action->fadeIn > 0.0f)
            layer.transition = std::min(1.0f, layer.transition + dt / action->fadeIn);

        auto fireInterval = [&](float begin, float end) {
            for (const auto& event : action->events)
                if (event.time > begin && event.time <= end)
                    fired.push_back({action->name, event.name});
        };
        if (action->loop && action->duration > 0.0f) {
            const int firstLoop = static_cast<int>(std::floor(oldTime / action->duration));
            const int lastLoop = static_cast<int>(std::floor(layer.time / action->duration));
            for (int loop = firstLoop; loop <= lastLoop; ++loop) {
                const float begin = loop == firstLoop ? oldTime - loop * action->duration : -0.000001f;
                const float end = loop == lastLoop ? layer.time - loop * action->duration
                                                   : action->duration;
                fireInterval(begin, end);
            }
        } else {
            fireInterval(oldTime, std::min(layer.time, action->duration));
            if (layer.time >= action->duration) {
                if (!layer.queue.empty()) {
                    Request next = layer.queue.front();
                    layer.queue.pop_front();
                    start(layer, next);
                } else if (action->fadeOut <= 0.0f ||
                           layer.time >= action->duration + action->fadeOut) {
                    layer.current = {};
                    layer.previous = {};
                    layer.transition = 1.0f;
                }
            }
        }
    }
    return fired;
}

void AnimationMixer::evaluate(Pose& output) const {
    if (!m_asset) { output = {}; return; }
    const Pose bind = bindPose(*m_asset);
    output = bind;
    if (!m_graph) { composeGlobals(*m_asset, output); return; }
    for (const auto& definition : m_graph->layers) {
        auto found = m_layers.find(definition.name);
        if (found == m_layers.end()) continue;
        const LayerState& state = found->second;
        Pose sampled;
        if (state.current.action) {
            evaluatePose(*m_asset, state.current.action->clip, state.time,
                         state.current.action->loop, sampled);
            if (state.previous.action && state.transition < 1.0f) {
                Pose previous;
                evaluatePose(*m_asset, state.previous.action->clip, state.previousTime,
                             state.previous.action->loop, previous);
                Pose transitioned;
                blendPoses(previous, sampled, state.transition, transitioned);
                sampled = std::move(transitioned);
            }
        } else if (state.previous.action && state.previous.action->fadeOut > 0.0f) {
            evaluatePose(*m_asset, state.previous.action->clip,
                         state.previousTime, false, sampled);
        } else continue;
        float weight = state.weight;
        if (state.current.action && !state.current.action->loop &&
            state.time > state.current.action->duration &&
            state.current.action->fadeOut > 0.0f) {
            weight *= std::clamp(1.0f -
                (state.time - state.current.action->duration) /
                    state.current.action->fadeOut, 0.0f, 1.0f);
        }
        if (definition.blend == LayerBlendMode::Override)
            applyOverride(output, sampled, definition.nodeMask, weight);
        else applyAdditive(output, sampled, bind, definition.nodeMask, weight);
    }
    composeGlobals(*m_asset, output);
}

bool AnimationMixer::playing(const std::string& action) const {
    for (const auto& pair : m_layers)
        if (pair.second.current.action && pair.second.current.action->name == action)
            return true;
    return false;
}

} // namespace model
