#pragma once

#include "model/ModelAsset.h"

#include <string_view>

namespace model {

struct Pose {
    std::vector<glm::vec3> translation;
    std::vector<glm::vec3> scale;
    std::vector<glm::quat> rotation;
    std::vector<glm::mat4> global;
};

struct ModelInstance {
    Pose pose;
    std::vector<std::vector<glm::mat4>> jointPalettes;
};

Pose bindPose(const ModelAsset& asset);
glm::vec4 sampleChannel(const Channel& channel, float time, bool loop);
void evaluatePose(const ModelAsset& asset, std::string_view clipName, float time,
                  bool loop, Pose& pose);
void blendPoses(const Pose& from, const Pose& to, float amount, Pose& out);
void composeGlobals(const ModelAsset& asset, Pose& pose);
std::vector<glm::mat4> jointMatrices(const ModelAsset& asset, const Pose& pose,
                                     std::size_t skin);

} // namespace model
