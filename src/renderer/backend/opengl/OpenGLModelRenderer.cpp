#include "model/ModelRenderer.h"
#include "renderer/backend/opengl/OpenGLDebug.h"
#include "model/ModelRenderLogic.h"
#include "renderer/Shader.h"
#include "core/Window.h"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace model {
namespace {

class OpenGLModelBackend final : public IModelRenderBackend {
public:
    OpenGLModelBackend(const std::filesystem::path& root, bool srgb, GraphicsApi api)
        : m_framebufferSrgb(srgb) {
        m_vertexAttribIPointer = reinterpret_cast<VertexAttribIPointerFn>(
            Window::graphicsProcAddress("glVertexAttribIPointer"));
        if (!m_vertexAttribIPointer)
            throw std::runtime_error("OpenGL integer vertex attributes are unavailable");
        m_shader = std::make_unique<Shader>(
            root / "shaders/model.vert", root / "shaders/model.frag", api);
    }

    ~OpenGLModelBackend() override { clear(); }

    ModelHandle upload(std::shared_ptr<const ModelAsset> asset) override {
        if (!asset) throw std::invalid_argument("cannot upload a null model asset");
        GpuModel gpu;
        gpu.asset = std::move(asset);
        for (const ImageData& image : gpu.asset->images) {
            GLuint texture = 0;
            GL_CHECK(glGenTextures(1, &texture));
            GL_CHECK(glBindTexture(GL_TEXTURE_2D, texture));
            GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
            GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
            GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
            GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
            GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, image.width,
                image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data()));
            gpu.textures.push_back(texture);
        }
        std::vector<int> nodes(gpu.asset->primitives.size(), -1);
        for (std::size_t n = 0; n < gpu.asset->nodes.size(); ++n)
            for (int primitive : gpu.asset->nodes[n].primitives)
                if (primitive >= 0 && static_cast<std::size_t>(primitive) < nodes.size())
                    nodes[static_cast<std::size_t>(primitive)] = static_cast<int>(n);
        for (std::size_t i = 0; i < gpu.asset->primitives.size(); ++i) {
            const Primitive& source = gpu.asset->primitives[i];
            GpuPrimitive primitive;
            primitive.indexCount = static_cast<GLsizei>(source.indices.size());
            primitive.material = source.material;
            primitive.skin = source.skin;
            primitive.node = nodes[i];
            GL_CHECK(glGenVertexArrays(1, &primitive.vao));
            GL_CHECK(glGenBuffers(1, &primitive.vbo));
            GL_CHECK(glGenBuffers(1, &primitive.ebo));
            GL_CHECK(glBindVertexArray(primitive.vao));
            GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, primitive.vbo));
            GL_CHECK(glBufferData(GL_ARRAY_BUFFER, source.vertices.size() * sizeof(Vertex),
                                  source.vertices.data(), GL_STATIC_DRAW));
            GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, primitive.ebo));
            GL_CHECK(glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                                  source.indices.size() * sizeof(uint32_t),
                                  source.indices.data(), GL_STATIC_DRAW));
            GL_CHECK(glEnableVertexAttribArray(0));
            GL_CHECK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                reinterpret_cast<void*>(offsetof(Vertex, position))));
            GL_CHECK(glEnableVertexAttribArray(1));
            GL_CHECK(glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                reinterpret_cast<void*>(offsetof(Vertex, normal))));
            GL_CHECK(glEnableVertexAttribArray(2));
            GL_CHECK(glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                reinterpret_cast<void*>(offsetof(Vertex, uv))));
            GL_CHECK(glEnableVertexAttribArray(3));
            GL_CHECK(m_vertexAttribIPointer(3, 4, GL_UNSIGNED_INT, sizeof(Vertex),
                reinterpret_cast<void*>(offsetof(Vertex, joints))));
            GL_CHECK(glEnableVertexAttribArray(4));
            GL_CHECK(glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                reinterpret_cast<void*>(offsetof(Vertex, weights))));
            gpu.primitives.push_back(primitive);
        }
        GL_CHECK(glBindVertexArray(0));
        m_models.push_back(std::move(gpu));
        return static_cast<ModelHandle>(m_models.size());
    }

    void queue(const ModelDraw& draw) override {
        if (draw.model && draw.model <= m_models.size()) m_draws.push_back(draw);
    }

    void flushOpaque(const glm::mat4& vp, const RenderEnvironment& environment,
        const glm::vec3& camera, float fogStart, float fogEnd) override {
        flush(false, vp, environment, camera, fogStart, fogEnd);
    }

    void flushBlend(const glm::mat4& vp, const RenderEnvironment& environment,
        const glm::vec3& camera, float fogStart, float fogEnd) override {
        sortBlended(m_draws);
        flush(true, vp, environment, camera, fogStart, fogEnd);
        m_draws.clear();
    }

    void clear() override {
        m_draws.clear();
        for (GpuModel& model : m_models) {
            for (GpuPrimitive& primitive : model.primitives) {
                if (primitive.ebo) GL_CHECK(glDeleteBuffers(1, &primitive.ebo));
                if (primitive.vbo) GL_CHECK(glDeleteBuffers(1, &primitive.vbo));
                if (primitive.vao) GL_CHECK(glDeleteVertexArrays(1, &primitive.vao));
            }
            for (GLuint texture : model.textures)
                if (texture) GL_CHECK(glDeleteTextures(1, &texture));
        }
        m_models.clear();
    }

private:
    struct GpuPrimitive {
        uint32_t vao = 0, vbo = 0, ebo = 0;
        int indexCount = 0, material = -1, skin = -1, node = -1;
    };
    struct GpuModel {
        std::shared_ptr<const ModelAsset> asset;
        std::vector<GpuPrimitive> primitives;
        std::vector<uint32_t> textures;
    };
#if defined(_WIN32)
    using VertexAttribIPointerFn = void (__stdcall *)(uint32_t, int, uint32_t,
                                                      int, const void*);
#else
    using VertexAttribIPointerFn = void (*)(uint32_t, int, uint32_t, int,
                                            const void*);
#endif
    std::unique_ptr<Shader> m_shader;
    std::vector<GpuModel> m_models;
    std::vector<ModelDraw> m_draws;
    bool m_framebufferSrgb = false;
    VertexAttribIPointerFn m_vertexAttribIPointer = nullptr;

    void flush(bool blended, const glm::mat4& vp,
        const RenderEnvironment& environment, const glm::vec3& camera,
        float fogStart, float fogEnd) {
        if (!m_shader) return;
        GLboolean oldBlend = GL_FALSE, oldCull = GL_FALSE, oldDepth = GL_TRUE;
        GL_CHECK(glGetBooleanv(GL_BLEND, &oldBlend));
        GL_CHECK(glGetBooleanv(GL_CULL_FACE, &oldCull));
        GL_CHECK(glGetBooleanv(0x0B72, &oldDepth));
        GLint oldActiveTexture = GL_TEXTURE0;
        GL_CHECK(glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActiveTexture));
        if (blended) {
            GL_CHECK(glEnable(GL_BLEND));
            GL_CHECK(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
            GL_CHECK(glDepthMask(GL_FALSE));
        }
        m_shader->bind();
        m_shader->setMat4("uViewProjection", vp);
        m_shader->setVec3("uCameraPosition", camera);
        m_shader->setVec3("uAmbientColor", environment.ambientColor * environment.ambientIntensity);
        m_shader->setVec3("uDirectColor", environment.directColor * environment.directIntensity);
        m_shader->setVec3("uLightDirection", environment.lightDirection);
        m_shader->setVec3("uFogColor", environment.fogColor);
        m_shader->setFloat("uFogStart", fogStart);
        m_shader->setFloat("uFogEnd", fogEnd);
        m_shader->setInt("uManualGamma", m_framebufferSrgb ? 0 : 1);
        m_shader->setInt("uTexture", 0);
        std::array<glm::mat4, MAX_JOINTS> identity;
        identity.fill(glm::mat4(1));
        for (const ModelDraw& draw : m_draws) {
            const GpuModel& model = m_models[draw.model - 1];
            for (const GpuPrimitive& primitive : model.primitives) {
                const Material fallback;
                const Material& material = primitive.material >= 0 &&
                    static_cast<std::size_t>(primitive.material) < model.asset->materials.size()
                    ? model.asset->materials[static_cast<std::size_t>(primitive.material)] : fallback;
                if ((modelPass(material.alphaMode) == ModelPass::Blend) != blended) continue;
                const glm::mat4 node = draw.instance && primitive.node >= 0 &&
                    static_cast<std::size_t>(primitive.node) < draw.instance->pose.global.size()
                    ? draw.instance->pose.global[static_cast<std::size_t>(primitive.node)]
                    : glm::mat4(1);
                m_shader->setMat4("uModel", draw.transform);
                m_shader->setMat4("uNode", primitive.skin >= 0 ? glm::mat4(1) : node);
                const std::vector<glm::mat4>* palette = nullptr;
                if (draw.instance && primitive.skin >= 0 &&
                    static_cast<std::size_t>(primitive.skin) < draw.instance->jointPalettes.size())
                    palette = &draw.instance->jointPalettes[static_cast<std::size_t>(primitive.skin)];
                m_shader->setMat4Array("uJoints[0]", palette && !palette->empty()
                    ? palette->data() : identity.data(), palette && !palette->empty()
                    ? palette->size() : identity.size());
                m_shader->setVec4("uBaseColor", material.baseColor * draw.tint);
                m_shader->setFloat("uAlphaCutoff",
                    material.alphaMode == AlphaMode::Mask ? material.alphaCutoff : 0.0f);
                const bool textured = material.image >= 0 &&
                    static_cast<std::size_t>(material.image) < model.textures.size();
                m_shader->setInt("uUseTexture", textured ? 1 : 0);
                if (textured) {
                    GL_CHECK(glActiveTexture(GL_TEXTURE0));
                    GL_CHECK(glBindTexture(GL_TEXTURE_2D,
                        model.textures[static_cast<std::size_t>(material.image)]));
                }
                if (material.doubleSided) GL_CHECK(glDisable(GL_CULL_FACE));
                else GL_CHECK(glEnable(GL_CULL_FACE));
                GL_CHECK(glBindVertexArray(primitive.vao));
                GL_CHECK(glDrawElements(GL_TRIANGLES, primitive.indexCount,
                                        GL_UNSIGNED_INT, nullptr));
            }
        }
        GL_CHECK(glBindVertexArray(0));
        m_shader->unbind();
        if (oldBlend) GL_CHECK(glEnable(GL_BLEND)); else GL_CHECK(glDisable(GL_BLEND));
        if (oldCull) GL_CHECK(glEnable(GL_CULL_FACE)); else GL_CHECK(glDisable(GL_CULL_FACE));
        GL_CHECK(glDepthMask(oldDepth));
        GL_CHECK(glActiveTexture(static_cast<GLenum>(oldActiveTexture)));
    }
};

} // namespace

std::unique_ptr<ModelRenderer> createOpenGLModelRenderer(
    const std::filesystem::path& root, bool srgb, GraphicsApi api) {
    return std::make_unique<ModelRenderer>(
        std::make_unique<OpenGLModelBackend>(root, srgb, api));
}

} // namespace model
