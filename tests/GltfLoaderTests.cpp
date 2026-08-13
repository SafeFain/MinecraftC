#include "model/GltfLoader.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr unsigned char PNG_1X1_RGBA[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
    0x0d, 0x49, 0x44, 0x41, 0x54, 0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
    0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x89, 0x99, 0x3d, 0x1d, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

bool contains(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

void replaceOnce(std::string& text, const std::string& from, const std::string& to) {
    const std::size_t position = text.find(from);
    require(position != std::string::npos, "fixture replacement source was not found");
    text.replace(position, from.size(), to);
}

void appendU32(std::vector<unsigned char>& data, uint32_t value) {
    for (int byte = 0; byte < 4; ++byte)
        data.push_back(static_cast<unsigned char>((value >> (byte * 8)) & 0xff));
}

void appendFloat(std::vector<unsigned char>& data, float value) {
    static_assert(sizeof(float) == sizeof(uint32_t));
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    appendU32(data, bits);
}

void appendU16(std::vector<unsigned char>& data, uint16_t value) {
    data.push_back(static_cast<unsigned char>(value & 0xff));
    data.push_back(static_cast<unsigned char>(value >> 8));
}

void align4(std::vector<unsigned char>& data) {
    while (data.size() % 4 != 0) data.push_back(0);
}

std::vector<unsigned char> modelBuffer(uint16_t jointValue) {
    std::vector<unsigned char> data;
    const float positions[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const float normals[] = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float uvs[] = {0, 0, 1, 0, 0, 1};
    for (float value : positions) appendFloat(data, value);
    for (float value : normals) appendFloat(data, value);
    for (float value : uvs) appendFloat(data, value);
    for (int vertex = 0; vertex < 3; ++vertex) {
        appendU16(data, jointValue); appendU16(data, 0); appendU16(data, 0); appendU16(data, 0);
    }
    for (int vertex = 0; vertex < 3; ++vertex) {
        appendFloat(data, 1); appendFloat(data, 0); appendFloat(data, 0); appendFloat(data, 0);
    }
    appendU16(data, 0); appendU16(data, 1); appendU16(data, 2);
    align4(data);
    for (int matrix = 0; matrix < 2; ++matrix)
        for (int value = 0; value < 16; ++value) appendFloat(data, value % 5 == 0 ? 1.0f : 0.0f);
    data.insert(data.end(), std::begin(PNG_1X1_RGBA), std::end(PNG_1X1_RGBA));
    return data;
}

std::vector<unsigned char> animatedBuffer() {
    std::vector<unsigned char> data = modelBuffer(0);
    align4(data);
    for (float value : {0.0f, 1.0f}) appendFloat(data, value);
    for (float value : {0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f}) appendFloat(data, value);
    for (float value : {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f}) appendFloat(data, value);
    for (float value : {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                        0.0f, 0.0f, 0.0f, 2.0f, 2.0f, 2.0f,
                        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}) appendFloat(data, value);
    return data;
}

std::string modelJson(std::size_t byteLength, bool external, bool tooManyJoints,
                      bool badAccessor, bool requiredExtension, bool animation = false,
                      bool nonIndexed = false, bool indices32 = false) {
    const std::size_t positionSize = badAccessor ? 12 : 36;
    const std::string bufferUri = external ? "\"uri\":\"external.bin\"," : "";
    const std::string joints = tooManyJoints
        ? "[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64]"
        : "[0,1]";
    std::string nodes = "[{\"name\":\"root\",\"translation\":[2,0,0],\"children\":[1]},"
                        "{\"name\":\"mesh\",\"mesh\":0,\"skin\":0,\"scale\":[2,2,2]},"
                        "{\"name\":\"matrix\",\"matrix\":[1,0,0,0,0,1,0,0,0,0,1,0,4,0,0,1]}";
    if (tooManyJoints) {
        for (int index = 3; index < 65; ++index) nodes += ",{}";
    }
    nodes += "]";
    return std::string("{\"asset\":{\"version\":\"2.0\"},") +
        (requiredExtension ? "\"extensionsRequired\":[\"EXT_rejected\"]," : "") +
        "\"buffers\":[{" + bufferUri + "\"byteLength\":" + std::to_string(byteLength) + "}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":" + std::to_string(positionSize) + "},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":72,\"byteLength\":24},"
        "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":24},{\"buffer\":0,\"byteOffset\":120,\"byteLength\":48},"
        "{\"buffer\":0,\"byteOffset\":168,\"byteLength\":" + std::string(indices32 ? "12" : "6") + "},{\"buffer\":0,\"byteOffset\":176,\"byteLength\":128},"
        "{\"buffer\":0,\"byteOffset\":304,\"byteLength\":70}" +
        (animation ? ",{\"buffer\":0,\"byteOffset\":376,\"byteLength\":8},{\"buffer\":0,\"byteOffset\":384,\"byteLength\":24},{\"buffer\":0,\"byteOffset\":408,\"byteLength\":32},{\"buffer\":0,\"byteOffset\":440,\"byteLength\":72}" : "") + "],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"},"
        "{\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"VEC4\"},"
        "{\"bufferView\":4,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"},"
        "{\"bufferView\":5,\"componentType\":" + std::string(indices32 ? "5125" : "5123") + ",\"count\":3,\"type\":\"SCALAR\"},"
        "{\"bufferView\":6,\"componentType\":5126,\"count\":2,\"type\":\"MAT4\"}" +
        (animation ? ",{\"bufferView\":8,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"},{\"bufferView\":9,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"},{\"bufferView\":10,\"componentType\":5126,\"count\":2,\"type\":\"VEC4\"},{\"bufferView\":11,\"componentType\":5126,\"count\":6,\"type\":\"VEC3\"}" : "") + "],"
        "\"images\":[{\"bufferView\":7,\"mimeType\":\"image/png\"}],\"textures\":[{\"source\":0}],"
        "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2,\"JOINTS_0\":3,\"WEIGHTS_0\":4}," +
        (nonIndexed ? "" : "\"indices\":5,") + "\"material\":0}]}],"
        "\"nodes\":" + nodes + ","
        "\"skins\":[{\"joints\":" + joints + ",\"inverseBindMatrices\":6}]," +
        (animation ? "\"animations\":[{\"name\":\"three_modes\",\"samplers\":[{\"input\":7,\"output\":8,\"interpolation\":\"LINEAR\"},{\"input\":7,\"output\":9,\"interpolation\":\"STEP\"},{\"input\":7,\"output\":10,\"interpolation\":\"CUBICSPLINE\"}],\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}},{\"sampler\":1,\"target\":{\"node\":1,\"path\":\"rotation\"}},{\"sampler\":2,\"target\":{\"node\":1,\"path\":\"scale\"}}]}]," : "") +
        "\"scenes\":[{\"nodes\":[0,2]}],\"scene\":0}";
}

void writeFile(const std::filesystem::path& path, const std::vector<unsigned char>& data) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
}

void writeGlb(const std::filesystem::path& path, const std::string& json,
              const std::vector<unsigned char>& binary) {
    std::vector<unsigned char> jsonData(json.begin(), json.end());
    while (jsonData.size() % 4 != 0) jsonData.push_back(' ');
    std::vector<unsigned char> binaryData = binary;
    align4(binaryData);
    std::vector<unsigned char> glb;
    appendU32(glb, 0x46546c67); appendU32(glb, 2);
    appendU32(glb, static_cast<uint32_t>(12 + 8 + jsonData.size() + 8 + binaryData.size()));
    appendU32(glb, static_cast<uint32_t>(jsonData.size())); appendU32(glb, 0x4e4f534a);
    glb.insert(glb.end(), jsonData.begin(), jsonData.end());
    appendU32(glb, static_cast<uint32_t>(binaryData.size())); appendU32(glb, 0x004e4942);
    glb.insert(glb.end(), binaryData.begin(), binaryData.end());
    writeFile(path, glb);
}

std::filesystem::path fixture(const char* name) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "minecraftc-gltf-loader-fixtures";
    std::filesystem::create_directories(root);
    const std::vector<unsigned char> binary = modelBuffer(0);
    writeGlb(root / "skinned.glb", modelJson(binary.size(), false, false, false, false), binary);
    writeGlb(root / "skin_65.glb", modelJson(binary.size(), false, true, false, false), binary);
    writeGlb(root / "accessor_oob.glb", modelJson(binary.size(), false, false, true, false), binary);
    writeGlb(root / "joint_oob.glb", modelJson(binary.size(), false, false, false, false), modelBuffer(2));
    writeGlb(root / "required_extension.glb", modelJson(binary.size(), false, false, false, true), binary);
    writeGlb(root / "non_indexed.glb", modelJson(binary.size(), false, false, false, false, false, true), binary);
    std::vector<unsigned char> indices32 = binary;
    indices32[168] = 0; indices32[169] = 0; indices32[170] = 0; indices32[171] = 0;
    indices32[172] = 1; indices32[173] = 0; indices32[174] = 0; indices32[175] = 0;
    indices32[176] = 2; indices32[177] = 0; indices32[178] = 0; indices32[179] = 0;
    writeGlb(root / "indices_32.glb", modelJson(indices32.size(), false, false, false, false, false, false, true), indices32);
    const std::vector<unsigned char> animated = animatedBuffer();
    writeGlb(root / "animated.glb", modelJson(animated.size(), false, false, false, false, true), animated);
    std::string unsafeIndex = modelJson(binary.size(), false, false, false, false);
    replaceOnce(unsafeIndex, "\"bufferView\":5,\"componentType\":5123,\"count\":3",
                "\"bufferView\":5,\"componentType\":5123,\"count\":9223372036854775809");
    writeGlb(root / "unsafe_index_count.glb", unsafeIndex, binary);
    std::string badWeights = modelJson(binary.size(), false, false, false, false);
    replaceOnce(badWeights, "\"bufferView\":4,\"componentType\":5126",
                "\"bufferView\":4,\"componentType\":5123");
    writeGlb(root / "unnormalized_weights.glb", badWeights, binary);
    std::string matrixAnimated = modelJson(animated.size(), false, false, false, false, true);
    replaceOnce(matrixAnimated, "\"mesh\",\"mesh\":0,\"skin\":0,\"scale\":[2,2,2]",
                "\"mesh\",\"mesh\":0,\"skin\":0,\"matrix\":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]");
    writeGlb(root / "matrix_animated.glb", matrixAnimated, animated);
    std::string sharedMesh = modelJson(binary.size(), false, false, false, false);
    replaceOnce(sharedMesh,
                "\"nodes\":[{\"name\":\"root\",\"translation\":[2,0,0],\"children\":[1]},{\"name\":\"mesh\",\"mesh\":0,\"skin\":0,\"scale\":[2,2,2]},{\"name\":\"matrix\",\"matrix\":[1,0,0,0,0,1,0,0,0,0,1,0,4,0,0,1]}",
                "\"nodes\":[{\"name\":\"root\",\"translation\":[2,0,0],\"children\":[1,2]},{\"name\":\"mesh_a\",\"mesh\":0,\"skin\":0},{\"name\":\"mesh_b\",\"mesh\":0,\"skin\":1},{\"name\":\"matrix\",\"matrix\":[1,0,0,0,0,1,0,0,0,0,1,0,4,0,0,1]}");
    replaceOnce(sharedMesh, "\"skins\":[{\"joints\":[0,1],\"inverseBindMatrices\":6}],",
                "\"skins\":[{\"joints\":[0,1],\"inverseBindMatrices\":6},{\"joints\":[0,1],\"inverseBindMatrices\":6}],");
    replaceOnce(sharedMesh, "\"scenes\":[{\"nodes\":[0,2]}]", "\"scenes\":[{\"nodes\":[0,3]}]");
    writeGlb(root / "shared_mesh_skins.glb", sharedMesh, binary);
    writeFile(root / "external.bin", binary);
    writeText(root / "external.gltf", modelJson(binary.size(), true, false, false, false));
    writeText(root / "buffer_overflow.gltf", modelJson(binary.size() + 1, true, false, false, false));
    return root / name;
}

} // namespace

int main() {
    const auto valid = model::loadGltf(fixture("skinned.glb"));
    require(valid && valid.asset->skins[0].joints.size() == 2, "valid skin failed");
    require(valid.asset->primitives.size() == 1 && valid.asset->primitives[0].indices.size() == 3,
            "indexed triangle was not converted");
    require(valid.asset->primitives[0].skin == 0,
            "node skin was not handed off to its unique mesh primitive");
    require(valid.asset->images.size() == 1 && valid.asset->images[0].width == 1,
            "embedded PNG was not decoded");
    require(valid.asset->nodes[0].translation.x == 2.0f && valid.asset->nodes[2].usesMatrix,
            "TRS or matrix node transform was not preserved");

    const auto external = model::loadGltf(fixture("external.gltf"));
    require(external && external.asset->primitives[0].vertices.size() == 3,
            "external-buffer glTF failed");

    const auto nonIndexed = model::loadGltf(fixture("non_indexed.glb"));
    require(nonIndexed && nonIndexed.asset->primitives[0].indices.size() == 3,
            "non-indexed triangle was not generated");
    const auto indices32 = model::loadGltf(fixture("indices_32.glb"));
    require(indices32 && indices32.asset->primitives[0].indices[2] == 2,
            "32-bit indices were not converted");
    const auto animated = model::loadGltf(fixture("animated.glb"));
    require(animated && animated.asset->animations[0].channels.size() == 3 &&
                animated.asset->animations[0].channels[1].interpolation == model::Interpolation::Step &&
                animated.asset->animations[0].channels[2].interpolation == model::Interpolation::CubicSpline,
            "animation interpolation modes were not converted");

    const auto over = model::loadGltf(fixture("skin_65.glb"));
    require(!over && contains(over.error, "65") && contains(over.error, "64"),
            "joint limit diagnostic lost counts");

    const auto broken = model::loadGltf(fixture("accessor_oob.glb"));
    require(!broken && contains(broken.error, "accessor"), "bad accessor was accepted");

    const auto joint = model::loadGltf(fixture("joint_oob.glb"));
    require(!joint && contains(joint.error, "joint"), "out-of-range vertex joint was accepted");

    const auto extension = model::loadGltf(fixture("required_extension.glb"));
    require(!extension && contains(extension.error, "extension"), "required extension was accepted");

    const auto overflow = model::loadGltf(fixture("buffer_overflow.gltf"));
    require(!overflow && contains(overflow.error, "buffer"), "short external buffer was accepted");

    const auto unsafeIndex = model::loadGltf(fixture("unsafe_index_count.glb"));
    require(!unsafeIndex && contains(unsafeIndex.error, "accessor range"),
            "overflowing index accessor was not rejected by prevalidation");

    const auto badWeights = model::loadGltf(fixture("unnormalized_weights.glb"));
    require(!badWeights && contains(badWeights.error, "WEIGHTS_0"),
            "unnormalized integer weights were accepted");

    const auto matrixAnimated = model::loadGltf(fixture("matrix_animated.glb"));
    require(!matrixAnimated && contains(matrixAnimated.error, "matrix"),
            "animation targeting a matrix-authored node was accepted");

    const auto sharedMesh = model::loadGltf(fixture("shared_mesh_skins.glb"));
    require(sharedMesh && sharedMesh.asset->nodes[1].skin == 0 && sharedMesh.asset->nodes[2].skin == 1 &&
                sharedMesh.asset->nodes[1].primitives == sharedMesh.asset->nodes[2].primitives &&
                sharedMesh.asset->primitives[0].skin == -1,
            "shared mesh primitive retained a node-specific skin");

    const char* entityModels[] = {"cow", "pig", "sheep", "chicken",
        "zombie", "skeleton", "spider", "blastling"};
    for (const char* name : entityModels) {
        const auto loaded = model::loadGltf(
            std::filesystem::path(MINECRAFTC_SOURCE_DIR) /
            "assets/models/entities" /
            (std::string(name) + ".glb"));
        if (!loaded) std::cerr << name << ": " << loaded.error << '\n';
        require(static_cast<bool>(loaded),
                "generated runtime entity model failed loader validation");
        require(loaded.asset->primitives.size() == 1 &&
                    loaded.asset->primitives[0].skin == 0,
                "generated entity primitive lost its GPU skin binding");
    }
    const auto player = model::loadGltf(
        std::filesystem::path(MINECRAFTC_SOURCE_DIR) /
        "assets/models/player/player.glb");
    require(player && player.asset->findClip("swing") &&
                player.asset->findClip("jump") && player.asset->findClip("fall"),
            "generated player model or its required clips failed loader validation");

    std::cout << "gltf loader tests passed\n";
}
