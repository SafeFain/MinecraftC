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

void resetPose(const ModelAsset& asset, Pose& pose) {
    const auto count = asset.nodes.size();
    pose.translation.resize(count); pose.scale.resize(count); pose.rotation.resize(count);
    pose.global.resize(count,glm::mat4(1.0f));
    for (std::size_t i=0;i<count;++i) {
        pose.translation[i]=asset.nodes[i].translation;
        pose.scale[i]=asset.nodes[i].scale;
        pose.rotation[i]=asset.nodes[i].rotation;
    }
}

Pose bindPose(const ModelAsset& asset) {
    Pose pose; resetPose(asset, pose); return pose;
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

void sampleLocalPose(const ModelAsset& asset, const AnimationClip* clip, float time,
                  bool loop, Pose& pose) {
    resetPose(asset, pose);
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
}

void evaluatePose(const ModelAsset& asset, std::string_view clipName, float time,
                  bool loop, Pose& pose) {
    sampleLocalPose(asset, asset.findClip(clipName), time, loop, pose);
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
    pose.global.resize(nodeCount);
    if (asset.topology.size() == nodeCount) {
        for (int i : asset.topology) {
            const auto node = static_cast<std::size_t>(i);
            const auto parent = asset.nodes[node].parent;
            const auto local = localTransform(asset,pose,node);
            pose.global[node] = parent < 0 ? local : pose.global[static_cast<std::size_t>(parent)] * local;
        }
        return;
    }
    // Hand-authored test/procedural assets may not have been prepared yet.
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
    std::vector<glm::mat4> matrices;
    jointMatrices(asset, pose, skinIndex, matrices);
    return matrices;
}

void jointMatrices(const ModelAsset& asset, const Pose& pose, std::size_t skinIndex, std::vector<glm::mat4>& matrices) {
    if (skinIndex >= asset.skins.size()) { matrices.clear(); return; }
    const Skin& skin = asset.skins[skinIndex];
    const std::size_t jointCount = std::min(skin.joints.size(), MAX_JOINTS);
    matrices.resize(jointCount);
    for (std::size_t index = 0; index < jointCount; ++index) {
        const int joint = skin.joints[index];
        const glm::mat4 inverseBind = index < skin.inverseBindMatrices.size()
            ? skin.inverseBindMatrices[index] : glm::mat4(1.0f);
        if (joint >= 0 && static_cast<std::size_t>(joint) < pose.global.size()) {
            matrices[index] = pose.global[static_cast<std::size_t>(joint)] * inverseBind;
        } else {
            matrices[index] = glm::mat4(1.0f);
        }
    }

}

void updatePalettes(const ModelAsset& asset, ModelInstance& instance) {
    instance.jointPalettes.resize(asset.skins.size());
    for (std::size_t skin=0;skin<asset.skins.size();++skin)
        jointMatrices(asset, instance.pose, skin, instance.jointPalettes[skin]);
    ++instance.revision;
}

bool prepareAsset(ModelAsset& asset, std::string& error) {
    const auto count = asset.nodes.size();
    asset.topology.clear(); asset.bindings.clear(); asset.clipIndices.clear();
    for(const auto& skin:asset.skins) {
        if(skin.joints.empty() || skin.joints.size()>MAX_JOINTS) {error="skin exceeds joint limit or is empty";return false;}
        for(int joint:skin.joints) if(joint<0 || static_cast<std::size_t>(joint)>=count) {error="skin references invalid node";return false;}
    }
    for(const auto& primitive:asset.primitives) {
        if(primitive.material < -1 || (primitive.material>=0 && static_cast<std::size_t>(primitive.material)>=asset.materials.size())) {error="invalid material reference";return false;}
        if(primitive.indices.size()>UINT32_MAX) {error="primitive index count overflow";return false;}
        for(uint32_t index:primitive.indices) if(index>=primitive.vertices.size()) {error="primitive index out of range";return false;}
    }
    for(const auto& material:asset.materials) {
        for(int c=0;c<4;++c) if(!std::isfinite(material.baseColor[c])) {error="nonfinite material factor";return false;}
        if(!std::isfinite(material.alphaCutoff)) {error="nonfinite alpha cutoff";return false;}
    }
    for (std::size_t i=0;i<count;++i) {
        const auto& node = asset.nodes[i];
        if(node.skin < -1 || (node.skin>=0 && static_cast<std::size_t>(node.skin)>=asset.skins.size())) {error="invalid skin binding";return false;}
        const auto validFloat = [](float v){return std::isfinite(v);};
        for (int c=0;c<3;++c) if (!validFloat(node.translation[c]) || !validFloat(node.scale[c])) { error="non-finite node TRS"; return false; }
        for (int c=0;c<4;++c) if (!validFloat(node.rotation[c])) { error="non-finite node rotation"; return false; }
        for (int c=0;c<4;++c) for(int r=0;r<4;++r) if(!validFloat(node.matrix[c][r])) {error="non-finite node matrix";return false;}
        if (node.parent < 0) asset.topology.push_back(static_cast<int>(i));
        else if (static_cast<std::size_t>(node.parent)>=count) {error="invalid parent node";return false;}
    }
    std::vector<bool> visited(count,false);
    for (std::size_t cursor=0;cursor<asset.topology.size();++cursor) {
        const auto node=static_cast<std::size_t>(asset.topology[cursor]);
        if (visited[node]) {error="node hierarchy cycle or repeated child";return false;}
        visited[node]=true;
        for (int child:asset.nodes[node].children) {
            if (child<0 || static_cast<std::size_t>(child)>=count || asset.nodes[child].parent!=static_cast<int>(node)) {error="invalid child node";return false;}
            asset.topology.push_back(child);
        }
    }
    if (asset.topology.size()!=count) {error="node hierarchy cycle or disconnected parent";return false;}
    Pose bind=bindPose(asset); composeGlobals(asset,bind); asset.bindGlobals=bind.global;
    std::vector<int> active=asset.sceneRoots;
    std::fill(visited.begin(),visited.end(),false);
    for (std::size_t i=0;i<active.size();++i) {
        const auto node=active[i];
        if(node<0 || static_cast<std::size_t>(node)>=count || visited[node]) {error="invalid or repeated scene root";return false;}
        visited[node]=true;
        for(int primitive:asset.nodes[node].primitives) {
            if(primitive<0 || static_cast<std::size_t>(primitive)>=asset.primitives.size()) {error="invalid primitive binding";return false;}
            asset.bindings.push_back({primitive,node,asset.nodes[node].skin});
        }
        active.insert(active.end(),asset.nodes[node].children.begin(),asset.nodes[node].children.end());
    }
    bool hasBounds=false;
    asset.boundsMin=asset.boundsMax=glm::vec3(0);
    for(const auto& binding:asset.bindings) {
        std::vector<glm::mat4> palette=jointMatrices(asset,bind,static_cast<std::size_t>(binding.skin));
        for(const auto& vertex:asset.primitives[binding.primitive].vertices) {
            glm::mat4 transform=bind.global[binding.node];
            if(binding.skin>=0) {
                transform=glm::mat4(0);
                for(int j=0;j<4;++j) if(vertex.weights[j]>0) {
                    if(vertex.joints[j]>=palette.size()) {error="joint out of skin range";return false;}
                    transform+=vertex.weights[j]*palette[vertex.joints[j]];
                }
            }
            const auto position=glm::vec3(transform*glm::vec4(vertex.position,1));
            if(!hasBounds) {asset.boundsMin=asset.boundsMax=position;hasBounds=true;}
            else {asset.boundsMin=glm::min(asset.boundsMin,position);asset.boundsMax=glm::max(asset.boundsMax,position);}
        }
    }
    for(std::size_t i=0;i<asset.animations.size();++i) asset.clipIndices.emplace(asset.animations[i].name,i);
    return true;
}

} // namespace model
