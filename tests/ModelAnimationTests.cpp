#include "model/ModelAnimation.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
bool near(float actual, float expected) {
    return std::abs(actual - expected) < 0.0001f;
}

bool nearVec(const glm::vec3& actual, const glm::vec3& expected) {
    return near(actual.x, expected.x) && near(actual.y, expected.y) &&
           near(actual.z, expected.z);
}

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    model::Channel linear;
    linear.path = model::ChannelPath::Translation;
    linear.interpolation = model::Interpolation::Linear;
    linear.times = {0.0f, 2.0f};
    linear.values = {{0.0f, 0.0f, 0.0f, 0.0f}, {4.0f, 2.0f, 0.0f, 0.0f}};
    require(nearVec(glm::vec3(model::sampleChannel(linear, 1.0f, false)),
                    {2.0f, 1.0f, 0.0f}),
            "linear channel did not interpolate between keyframes");
    require(nearVec(glm::vec3(model::sampleChannel(linear, 3.0f, true)),
                    {2.0f, 1.0f, 0.0f}),
            "looping channel did not wrap by its duration");

    model::Channel stepped = linear;
    stepped.interpolation = model::Interpolation::Step;
    require(nearVec(glm::vec3(model::sampleChannel(stepped, 1.9f, false)),
                    {0.0f, 0.0f, 0.0f}),
            "step channel advanced before its next keyframe");

    model::Channel cubic;
    cubic.path = model::ChannelPath::Translation;
    cubic.interpolation = model::Interpolation::CubicSpline;
    cubic.times = {0.0f, 2.0f};
    cubic.values = {
        {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f},
        {4.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f},
    };
    require(nearVec(glm::vec3(model::sampleChannel(cubic, 0.5f, false)),
                    {0.8125f, 0.0f, 0.0f}),
            "cubic spline did not use delta-scaled Hermite tangents");

    model::Channel rotation;
    rotation.path = model::ChannelPath::Rotation;
    rotation.interpolation = model::Interpolation::Linear;
    rotation.times = {0.0f, 1.0f};
    rotation.values = {{0.0f, 0.0f, 0.0f, 2.0f}, {0.0f, 1.0f, 0.0f, 0.0f}};
    const glm::vec4 sampledRotation = model::sampleChannel(rotation, 0.5f, false);
    require(near(glm::length(glm::quat(sampledRotation.w, sampledRotation.x,
                                       sampledRotation.y, sampledRotation.z)),
                 1.0f),
            "rotation interpolation did not normalize its quaternion");

    model::ModelAsset asset;
    model::Node root;
    root.translation = {1.0f, 0.0f, 0.0f};
    root.children = {1};
    model::Node child;
    child.parent = 0;
    child.translation = {0.0f, 2.0f, 0.0f};
    asset.nodes = {root, child};
    model::AnimationClip clip;
    clip.name = "move";
    clip.duration = 2.0f;
    linear.node = 1;
    clip.channels = {linear};
    asset.animations = {clip};

    model::Pose pose;
    model::evaluatePose(asset, "move", 1.0f, false, pose);
    require(nearVec(pose.translation[1], {2.0f, 1.0f, 0.0f}),
            "pose evaluation did not apply translation channel");
    require(nearVec(glm::vec3(pose.global[1][3]), {3.0f, 1.0f, 0.0f}),
            "pose evaluation did not compose parent transform");

    model::Pose target = model::bindPose(asset);
    target.translation[1] = {8.0f, 0.0f, 0.0f};
    model::Pose blended;
    model::blendPoses(pose, target, 0.25f, blended);
    require(nearVec(blended.translation[1], {3.5f, 0.75f, 0.0f}),
            "pose blend did not interpolate local translation");

    asset.skins = {{{1}, {glm::mat4(1.0f)}}};
    const auto palette = model::jointMatrices(asset, pose, 0);
    require(palette.size() == 1 && nearVec(glm::vec3(palette[0][3]), {3.0f, 1.0f, 0.0f}),
            "joint palette did not use the animated joint global transform");

    std::cout << "model animation tests passed\n";
}
