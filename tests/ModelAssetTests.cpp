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
    model::ModelAsset asset;
    model::Node root;
    root.name = "root";
    root.translation = {1.0f, 0.0f, 0.0f};
    root.children = {1};
    model::Node child;
    child.name = "child";
    child.parent = 0;
    child.translation = {0.0f, 2.0f, 0.0f};
    asset.nodes = {root, child};

    auto pose = model::bindPose(asset);
    model::composeGlobals(asset, pose);
    require(nearVec(glm::vec3(pose.global[1][3]), {1.0f, 2.0f, 0.0f}),
            "child global transform lost its parent");

    model::Vertex vertex{};
    vertex.weights = glm::vec4(0.0f);
    model::normalizeWeights(vertex);
    require(vertex.joints.x == 0u && near(vertex.weights.x, 1.0f),
            "zero weights did not select joint zero");

    vertex.weights = {2.0f, 1.0f, 1.0f, 0.0f};
    model::normalizeWeights(vertex);
    require(near(vertex.weights.x, 0.5f) && near(vertex.weights.y, 0.25f) &&
                near(vertex.weights.z, 0.25f) && near(vertex.weights.w, 0.0f),
            "nonzero skin weights were not normalized");

    model::AnimationClip walk;
    walk.name = "walk";
    model::AnimationClip idle;
    idle.name = "idle";
    asset.animations = {walk, idle};
    require(asset.findClip("idle") == &asset.animations[1],
            "clip lookup did not find the requested animation");
    require(asset.findClip("missing") == nullptr,
            "clip lookup returned a nonmatching animation");

    std::cout << "model asset tests passed\n";
}
