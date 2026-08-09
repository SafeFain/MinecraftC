#include "renderer/TexturedCubeScene.h"

#include "core/AssetStore.h"

#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <stdexcept>
#include <stb_image.h>

namespace {

MeshData makeCube() {
    const BasicMeshVertex vertices[] = {
        {{-.5f,-.5f, .5f},{0,1}}, {{ .5f,-.5f, .5f},{1,1}}, {{ .5f, .5f, .5f},{1,0}}, {{-.5f, .5f, .5f},{0,0}},
        {{ .5f,-.5f,-.5f},{0,1}}, {{-.5f,-.5f,-.5f},{1,1}}, {{-.5f, .5f,-.5f},{1,0}}, {{ .5f, .5f,-.5f},{0,0}},
        {{-.5f,-.5f,-.5f},{0,1}}, {{-.5f,-.5f, .5f},{1,1}}, {{-.5f, .5f, .5f},{1,0}}, {{-.5f, .5f,-.5f},{0,0}},
        {{ .5f,-.5f, .5f},{0,1}}, {{ .5f,-.5f,-.5f},{1,1}}, {{ .5f, .5f,-.5f},{1,0}}, {{ .5f, .5f, .5f},{0,0}},
        {{-.5f, .5f,-.5f},{0,1}}, {{-.5f, .5f, .5f},{1,1}}, {{ .5f, .5f, .5f},{1,0}}, {{ .5f, .5f,-.5f},{0,0}},
        {{-.5f,-.5f,-.5f},{0,1}}, {{ .5f,-.5f,-.5f},{1,1}}, {{ .5f,-.5f, .5f},{1,0}}, {{-.5f,-.5f, .5f},{0,0}},
    };
    const uint32_t indices[] = {
         0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4,
         8, 9,10,10,11, 8,12,13,14,14,15,12,
        16,17,18,18,19,16,20,21,22,22,23,20,
    };
    MeshData result;
    result.vertices.assign(std::begin(vertices), std::end(vertices));
    result.indices.assign(std::begin(indices), std::end(indices));
    result.opaqueIndexCount = static_cast<uint32_t>(result.indices.size());
    return result;
}

TextureData loadTexture(const std::filesystem::path& path) {
    const std::vector<uint8_t> encoded = AssetStore::readPath(path);
    if (encoded.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("Texture is too large: " + path.string());
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(
        encoded.data(), static_cast<int>(encoded.size()), &width, &height,
        &channels, STBI_rgb_alpha);
    if (!decoded || width <= 0 || height <= 0) {
        const std::string reason = stbi_failure_reason()
            ? std::string(": ") + stbi_failure_reason() : std::string();
        stbi_image_free(decoded);
        throw std::runtime_error("Could not decode texture " + path.string() + reason);
    }
    TextureData result;
    result.width = static_cast<uint32_t>(width);
    result.height = static_cast<uint32_t>(height);
    result.pixels.assign(decoded, decoded + static_cast<size_t>(width) * height * 4u);
    stbi_image_free(decoded);
    return result;
}

} // namespace

TexturedCubeScene::TexturedCubeScene(
    IRenderDevice& renderer, const std::filesystem::path& assetRoot)
    : m_renderer(renderer), m_started(std::chrono::steady_clock::now()) {
    if (!renderer.capabilities().texturedMesh)
        throw std::runtime_error("Renderer does not support textured meshes");
    m_mesh = renderer.createMesh(makeCube());
    try {
        m_texture = renderer.createTexture(
            loadTexture(assetRoot / "textures" / "generated" / "stone.png"), {});
        m_material = renderer.createMaterial({MaterialPipeline::UnlitTextured,
                                               m_texture, true, true});
    } catch (...) {
        if (m_texture) renderer.destroyTexture(m_texture);
        renderer.destroyMesh(m_mesh);
        throw;
    }
    m_camera.setPosition({2.2f, 1.8f, 2.2f});
    m_camera.updateVectors(-135.0f, -22.0f);
}

TexturedCubeScene::~TexturedCubeScene() {
    m_renderer.waitIdle();
    if (m_material) m_renderer.destroyMaterial(m_material);
    if (m_texture) m_renderer.destroyTexture(m_texture);
    if (m_mesh) m_renderer.destroyMesh(m_mesh);
}

void TexturedCubeScene::render(float aspectRatio) {
    const float seconds = std::chrono::duration<float>(
        std::chrono::steady_clock::now() - m_started).count();
    FrameData frame;
    frame.view = m_camera.getViewMatrix();
    frame.projection = m_camera.getProjectionMatrix(aspectRatio);
    DrawCommand command;
    command.mesh = m_mesh;
    command.material = m_material;
    command.model = glm::rotate(glm::mat4(1.0f), seconds * 0.8f,
                                glm::vec3(0.3f, 1.0f, 0.15f));
    m_renderer.beginFrame(frame);
    m_renderer.draw(command);
    m_renderer.endFrame();
}
