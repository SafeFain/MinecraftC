#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <map>

namespace model {

constexpr std::size_t MAX_JOINTS = 256;

struct Vertex {
    glm::vec3 position{};
    glm::vec3 normal{};
    glm::vec2 uv{};
    glm::uvec4 joints{0};
    glm::vec4 weights{1, 0, 0, 0};
};

inline void normalizeWeights(Vertex& vertex) {
    const float total = vertex.weights.x + vertex.weights.y +
                        vertex.weights.z + vertex.weights.w;
    if (total <= 0.0f) {
        vertex.joints = glm::uvec4(0);
        vertex.weights = glm::vec4(1, 0, 0, 0);
        return;
    }
    vertex.weights /= total;
}

enum class AlphaMode { Opaque, Mask, Blend };

struct Material {
    glm::vec4 baseColor{1};
    int image = -1;
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = .5f;
    bool doubleSided = false;
    int texture = -1;
};

struct Primitive {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    int material = -1;
    glm::vec3 boundsMin{};
    glm::vec3 boundsMax{};
};

struct Node {
    std::string name;
    int parent = -1;
    std::vector<int> children;
    glm::vec3 translation{0};
    glm::vec3 scale{1};
    glm::quat rotation{1, 0, 0, 0};
    glm::mat4 matrix{1};
    bool usesMatrix = false;
    std::vector<int> primitives;
    int skin = -1;
};

struct Skin {
    std::vector<int> joints;
    std::vector<glm::mat4> inverseBindMatrices;
};

enum class ChannelPath { Translation, Rotation, Scale };
enum class Interpolation { Step, Linear, CubicSpline };

struct Channel {
    int node = -1;
    ChannelPath path{};
    Interpolation interpolation{};
    std::vector<float> times;
    std::vector<glm::vec4> values;
};

struct AnimationClip {
    std::string name;
    float duration = 0;
    std::vector<Channel> channels;
};

struct ImageData {
    int width = 0;
    int height = 0;
    int channels = 4;
    std::vector<uint8_t> pixels;
};

struct Sampler {
    int minFilter = 9728;
    int magFilter = 9728;
    int wrapS = 10497;
    int wrapT = 10497;
};

struct Texture {
    int image = -1;
    int sampler = -1;
};

struct DrawBinding {
    int primitive = -1;
    int node = -1;
    int skin = -1;
};

struct ModelAsset {
    std::vector<Primitive> primitives;
    std::vector<Material> materials;
    std::vector<Node> nodes;
    std::vector<int> sceneRoots;
    std::vector<Skin> skins;
    std::vector<AnimationClip> animations;
    std::vector<ImageData> images;
    std::vector<Sampler> samplers;
    std::vector<Texture> textures;
    std::vector<DrawBinding> bindings;
    std::vector<int> topology;
    std::vector<glm::mat4> bindGlobals;
    std::map<std::string, std::size_t, std::less<>> clipIndices;
    glm::vec3 boundsMin{};
    glm::vec3 boundsMax{};

    const AnimationClip* findClip(std::string_view name) const {
        const auto found = clipIndices.find(name);
        if (found != clipIndices.end()) return &animations[found->second];
        for (const AnimationClip& clip : animations) {
            if (clip.name == name) return &clip;
        }
        return nullptr;
    }
};

bool prepareAsset(ModelAsset& asset, std::string& error);

} // namespace model
