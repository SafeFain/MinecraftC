#pragma once

#include "model/ModelAnimation.h"
#include "renderer/RenderEnvironment.h"
#include "world/BlockLightLogic.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include <glad/glad.h>

class Shader;

namespace model {

using ModelHandle = uint32_t;

struct ModelDraw {
    ModelHandle model = 0;
    glm::mat4 transform{1.0f};
    const ModelInstance* instance = nullptr;
    glm::vec4 tint{1.0f};
    float distanceSquared = 0.0f;
    SmoothLightSample light;
};

class ModelRenderer {
public:
    ModelRenderer() = default;
    ~ModelRenderer();
    ModelRenderer(const ModelRenderer&) = delete;
    ModelRenderer& operator=(const ModelRenderer&) = delete;

    void initialize(const std::filesystem::path& assetRoot, bool framebufferSrgb);
    ModelHandle upload(std::shared_ptr<const ModelAsset> asset);
    void queue(const ModelDraw& draw);
    void flushOpaque(const glm::mat4& viewProjection,
                     const RenderEnvironment& environment,
                     const glm::vec3& cameraPosition,
                     float fogStart, float fogEnd);
    void flushBlend(const glm::mat4& viewProjection,
                    const RenderEnvironment& environment,
                    const glm::vec3& cameraPosition,
                    float fogStart, float fogEnd);
    void clear();

private:
    struct GpuPrimitive {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ebo = 0;
        GLsizei indexCount = 0;
        int material = -1;
        int skin = -1;
        int node = -1;
    };
    struct GpuModel {
        std::shared_ptr<const ModelAsset> asset;
        std::vector<GpuPrimitive> primitives;
        std::vector<GLuint> textures;
    };
    std::unique_ptr<Shader> m_shader;
    std::vector<GpuModel> m_models;
    std::vector<ModelDraw> m_draws;
    bool m_framebufferSrgb = false;
#if defined(_WIN32)
    using VertexAttribIPointerFn = void (__stdcall *)(GLuint, GLint, GLenum,
                                                      GLsizei, const void*);
#else
    using VertexAttribIPointerFn = void (*)(GLuint, GLint, GLenum, GLsizei,
                                            const void*);
#endif
    VertexAttribIPointerFn m_vertexAttribIPointer = nullptr;

    void flush(bool blended, const glm::mat4& viewProjection,
               const RenderEnvironment& environment,
               const glm::vec3& cameraPosition, float fogStart, float fogEnd);
};

} // namespace model
