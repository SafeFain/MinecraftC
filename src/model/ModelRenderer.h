#pragma once

#include "model/ModelAnimation.h"
#include "renderer/RenderEnvironment.h"
#include "world/BlockLightLogic.h"
#include "core/GraphicsApi.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

class VulkanRenderer;

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

class IModelRenderBackend {
public:
    virtual ~IModelRenderBackend() = default;
    virtual ModelHandle upload(std::shared_ptr<const ModelAsset> asset) = 0;
    virtual void queue(const ModelDraw& draw) = 0;
    virtual void flushOpaque(const glm::mat4& viewProjection,
        const RenderEnvironment& environment, const glm::vec3& cameraPosition,
        float fogStart, float fogEnd) = 0;
    virtual void flushBlend(const glm::mat4& viewProjection,
        const RenderEnvironment& environment, const glm::vec3& cameraPosition,
        float fogStart, float fogEnd) = 0;
    virtual void clear() = 0;
};

class ModelRenderer {
public:
    explicit ModelRenderer(std::unique_ptr<IModelRenderBackend> backend);
    ~ModelRenderer();
    ModelRenderer(const ModelRenderer&) = delete;
    ModelRenderer& operator=(const ModelRenderer&) = delete;

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
    std::unique_ptr<IModelRenderBackend> m_backend;
};

std::unique_ptr<ModelRenderer> createOpenGLModelRenderer(
    const std::filesystem::path& assetRoot, bool framebufferSrgb,
    GraphicsApi api);
std::unique_ptr<ModelRenderer> createVulkanModelRenderer(
    ::VulkanRenderer& renderer);

} // namespace model
