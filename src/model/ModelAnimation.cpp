#include "model/ModelAnimation.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <functional>

namespace model {
namespace {

glm::quat quaternionFromValue(const glm::vec4& value) {
    const glm::quat rotation(value.w, value.x, value.y, value.z);
    const float length = glm::length(rotation);
    return length > 0.0f ? rotation / length : glm::quat(1, 0, 0, 0);
}

glm::vec4 valueFromQuaternion(const glm::quat& rotation) {
    return {rotation.x, rotation.y, rotation.z, rotation.w};
}

glm::mat4 localTransform(const ModelAsset& asset, const Pose& pose,
                         std::size_t nodeIndex) {
    const Node& node = asset.nodes[nodeIndex];
    if (node.usesMatrix) return node.matrix;

    const glm::vec3 translation = nodeIndex < pose.translation.size()
        ? pose.translation[nodeIndex] : node.translation;
    const glm::vec3 scale = nodeIndex < pose.scale.size()
        ? pose.scale[nodeIndex] : node.scale;
    const glm::quat rotation = nodeIndex < pose.rotation.size()
        ? pose.rotation[nodeIndex] : node.rotation;
    return glm::translate(glm::mat4(1.0f), translation) *
           glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
}

glm::vec4 channelValue(const Channel& channel, std::size_t index) {
    if (channel.interpolation == Interpolation::CubicSpline) {
        const std::size_t valueIndex = index * 3 + 1;
        return valueIndex < channel.values.size() ? channel.values[valueIndex]
                                                  : glm::vec4(0.0f);
    }
    return index < channel.values.size() ? channel.values[index] : glm::vec4(0.0f);
}

glm::vec4 normalizedRotationValue(const glm::vec4& value) {
    return valueFromQuaternion(quaternionFromValue(value));
}

} // namespace

Pose bindPose(const ModelAsset& asset) {
    Pose pose;
    const std::size_t nodeCount = asset.nodes.size();
    pose.translation.reserve(nodeCount);
    pose.scale.reserve(nodeCount);
    pose.rotation.reserve(nodeCount);
    pose.global.assign(nodeCount, glm::mat4(1.0f));
    for (const Node& node : asset.nodes) {
        pose.translation.push_back(node.translation);
        pose.scale.push_back(node.scale);
        pose.rotation.push_back(node.rotation);
    }
    return pose;
}

glm::vec4 sampleChannel(const Channel& channel, float time, bool loop) {
    if (channel.times.empty() || channel.values.empty()) return glm::vec4(0.0f);

    const float firstTime = channel.times.front();
    const float lastTime = channel.times.back();
    const float duration = lastTime - firstTime;
    if (loop && duration > 0.0f) {
        time = std::fmod(time - firstTime, duration);
        if (time < 0.0f) time += duration;
        time += firstTime;
    } else {
        time = std::clamp(time, firstTime, lastTime);
    }

    const auto upper = std::upper_bound(channel.times.begin(), channel.times.end(), time);
    const std::size_t right = static_cast<std::size_t>(
        std::distance(channel.times.begin(), upper));
    const std::size_t left = right == 0 ? 0 : right - 1;
    if (left >= channel.times.size() - 1 || channel.interpolation == Interpolation::Step) {
        const glm::vec4 value = channelValue(channel, left);
        return channel.path == ChannelPath::Rotation ? normalizedRotationValue(value) : value;
    }

    const float keyDuration = channel.times[right] - channel.times[left];
    const float amount = keyDuration > 0.0f
        ? (time - channel.times[left]) / keyDuration : 0.0f;
    const glm::vec4 first = channelValue(channel, left);
    const glm::vec4 second = channelValue(channel, right);
    if (channel.interpolation == Interpolation::Linear) {
        if (channel.path == ChannelPath::Rotation) {
            glm::quat to = quaternionFromValue(second);
            const glm::quat from = quaternionFromValue(first);
            if (glm::dot(from, to) < 0.0f) to = -to;
            return valueFromQuaternion(glm::slerp(from, to, amount));
        }
        return glm::mix(first, second, amount);
    }

    const std::size_t outTangent = left * 3 + 2;
    const std::size_t inTangent = right * 3;
    if (outTangent >= channel.values.size() || inTangent >= channel.values.size()) {
        return channel.path == ChannelPath::Rotation
            ? normalizedRotationValue(glm::mix(first, second, amount))
            : glm::mix(first, second, amount);
    }
    const float amount2 = amount * amount;
    const float amount3 = amount2 * amount;
    const float h00 = 2.0f * amount3 - 3.0f * amount2 + 1.0f;
    const float h10 = amount3 - 2.0f * amount2 + amount;
    const float h01 = -2.0f * amount3 + 3.0f * amount2;
    const float h11 = amount3 - amount2;
    const glm::vec4 value = h00 * first + h10 * keyDuration * channel.values[outTangent] +
                            h01 * second + h11 * keyDuration * channel.values[inTangent];
    return channel.path == ChannelPath::Rotation ? normalizedRotationValue(value) : value;
}

void evaluatePose(const ModelAsset& asset, std::string_view clipName, float time,
                  bool loop, Pose& pose) {
    pose = bindPose(asset);
    const AnimationClip* clip = asset.findClip(clipName);
    if (clip) {
        for (const Channel& channel : clip->channels) {
            if (channel.node < 0 ||
                static_cast<std::size_t>(channel.node) >= asset.nodes.size()) continue;
            const glm::vec4 value = sampleChannel(channel, time, loop);
            const std::size_t node = static_cast<std::size_t>(channel.node);
            switch (channel.path) {
            case ChannelPath::Translation:
                pose.translation[node] = glm::vec3(value);
                break;
            case ChannelPath::Rotation:
                pose.rotation[node] = quaternionFromValue(value);
                break;
            case ChannelPath::Scale:
                pose.scale[node] = glm::vec3(value);
                break;
            }
        }
    }
    composeGlobals(asset, pose);
}

void blendPoses(const Pose& from, const Pose& to, float amount, Pose& out) {
    const std::size_t count = std::min({from.translation.size(), to.translation.size(),
                                        from.scale.size(), to.scale.size(),
                                        from.rotation.size(), to.rotation.size()});
    amount = std::clamp(amount, 0.0f, 1.0f);
    out.translation.resize(count);
    out.scale.resize(count);
    out.rotation.resize(count);
    out.global.assign(count, glm::mat4(1.0f));
    for (std::size_t index = 0; index < count; ++index) {
        out.translation[index] = glm::mix(from.translation[index], to.translation[index], amount);
        out.scale[index] = glm::mix(from.scale[index], to.scale[index], amount);
        glm::quat target = to.rotation[index];
        if (glm::dot(from.rotation[index], target) < 0.0f) target = -target;
        out.rotation[index] = glm::normalize(glm::slerp(from.rotation[index], target, amount));
    }
}

void composeGlobals(const ModelAsset& asset, Pose& pose) {
    const std::size_t nodeCount = asset.nodes.size();
    pose.global.assign(nodeCount, glm::mat4(1.0f));
    std::vector<uint8_t> state(nodeCount, 0);
    std::function<void(std::size_t)> composeNode = [&](std::size_t nodeIndex) {
        if (state[nodeIndex] == 2) return;
        const glm::mat4 local = localTransform(asset, pose, nodeIndex);
        if (state[nodeIndex] == 1) {
            pose.global[nodeIndex] = local;
            state[nodeIndex] = 2;
            return;
        }
        state[nodeIndex] = 1;
        const int parent = asset.nodes[nodeIndex].parent;
        if (parent >= 0 && static_cast<std::size_t>(parent) < nodeCount) {
            composeNode(static_cast<std::size_t>(parent));
            pose.global[nodeIndex] = pose.global[static_cast<std::size_t>(parent)] * local;
        } else {
            pose.global[nodeIndex] = local;
        }
        state[nodeIndex] = 2;
    };
    for (std::size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex)
        composeNode(nodeIndex);
}

std::vector<glm::mat4> jointMatrices(const ModelAsset& asset, const Pose& pose,
                                     std::size_t skinIndex) {
    if (skinIndex >= asset.skins.size()) return {};
    const Skin& skin = asset.skins[skinIndex];
    const std::size_t jointCount = std::min(skin.joints.size(), MAX_JOINTS);
    std::vector<glm::mat4> matrices;
    matrices.reserve(jointCount);
    for (std::size_t index = 0; index < jointCount; ++index) {
        const int joint = skin.joints[index];
        const glm::mat4 inverseBind = index < skin.inverseBindMatrices.size()
            ? skin.inverseBindMatrices[index] : glm::mat4(1.0f);
        if (joint >= 0 && static_cast<std::size_t>(joint) < pose.global.size()) {
            matrices.push_back(pose.global[static_cast<std::size_t>(joint)] * inverseBind);
        } else {
            matrices.push_back(glm::mat4(1.0f));
        }
    }
    return matrices;
}

} // namespace model
