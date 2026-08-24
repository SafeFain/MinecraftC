#pragma once

#include "renderer/GameRenderer.h"

#include <filesystem>
#include <array>
#include <memory>

class Window;
namespace model { class VulkanModelBackend; }

class VulkanRenderer final : public IGameRenderer {
public:
    VulkanRenderer();
    VulkanRenderer(Window& window, const std::filesystem::path& assetRoot);
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    RenderDeviceCapabilities capabilities() const override;
    RenderMeshHandle createMesh(const MeshData& data) override;
    void destroyMesh(RenderMeshHandle handle) override;
    RenderTextureHandle createTexture(
        const TextureData& data, const TextureSamplerDesc& sampler) override;
    void destroyTexture(RenderTextureHandle handle) override;
    RenderMaterialHandle createMaterial(const MaterialDesc& desc) override;
    void destroyMaterial(RenderMaterialHandle handle) override;
    void beginFrame(const FrameData& frame) override;
    void draw(const DrawCommand& command) override;
    void endFrame() override;
    void resize(int width, int height) override;
    void waitIdle() override;
    RendererPerformanceStats performanceStats() const override;

    void initialize(Window& window,
                    const std::filesystem::path& assetRoot) override;
    void reinitialize(const std::filesystem::path& assetRoot) override;
    void suspendPresentation() override;
    void resumePresentation() override;
    void beginFrame() override;
    void setVisualQuality(VisualQuality quality) override;
    void finishScene(const PostProcessState& state) override;
    void setEnvironment(const RenderEnvironment&, const glm::vec3&) override;
    void renderSky(const RenderEnvironment&, const glm::mat4&, const glm::vec3&,
                   bool) override;
    void renderChunk(const ChunkMesh&, const glm::mat4&, const glm::mat4&,
                     bool translucent = false) override;
    void renderLod(const ChunkMesh&, const glm::mat4&, const glm::mat4&,
                   const glm::vec2& worldOffset,
                   float minimumDistance, float maximumDistance,
                   bool translucent = false) override;
    void renderChunkShadows(ShadowQuality, const glm::mat4&, const glm::mat4&,
                            const glm::dvec3&,
                            const std::vector<ShadowChunkSubmission>&) override;
    void uploadChunkMesh(ChunkMesh&) override;
    void releaseChunkMesh(ChunkMesh&) override;
    void beginTranslucent() override;
    void endTranslucent() override;
    void bindBlockShader() const override;
    void unbindBlockShader() const override;
    void renderWireframe(const glm::vec3&, const glm::vec3&,
                         const glm::mat4&) override;
    void renderEntity(const glm::vec3&, const glm::vec3&, const glm::vec3&, int,
                      const glm::mat4&) override;
    void renderCompatibilityEntityCube(const glm::vec3&, const glm::vec3&,
        const glm::vec3&, int, float, const glm::mat4&,
        SmoothLightSample = {}) override;
    model::ModelRenderer& modelRenderer() override;
    void flushModels(const glm::mat4&) override;
    void beginViewModel(const glm::mat4& projection) override;
    void renderEntityPart(const glm::vec3&, const glm::vec3&, const glm::vec3&,
        float, const glm::vec3&, int, const glm::mat4&,
        SmoothLightSample = {1.0f, 0.0f}) override;
    void renderParticles(const std::vector<ParticleRenderData>&, const glm::mat4&,
        const glm::vec3&, const glm::vec3&, float) override;
    void renderClouds(const glm::dvec3&, const glm::mat4&, uint64_t, float,
                      int) override;
    void setViewProjection(const glm::mat4& value) override;
    void setFrustum(const Frustum& value) override { m_frustum = value; }
    const Frustum& getFrustum() const override { return m_frustum; }
    RenderTextureHandle getBlockAtlasTexture() const override { return m_blockAtlas; }
    uint32_t blockAtlasTilesPerSide() const override { return m_blockAtlasTilesPerSide; }
    void queueUiBatch(const std::vector<UiMeshVertex>& vertices,
                      const std::vector<uint32_t>& indices,
                      RenderMaterialHandle material,
                      const glm::mat4& projection);

private:
    friend class model::VulkanModelBackend;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    Window* m_window = nullptr;
    std::filesystem::path m_assetRoot;
    RenderTextureHandle m_blockAtlas{};
    RenderTextureHandle m_blockNormalAtlas{};
    RenderTextureHandle m_blockPropertyAtlas{};
    RenderTextureHandle m_neutralNormalTexture{};
    RenderTextureHandle m_neutralPropertyTexture{};
    uint32_t m_blockAtlasTilesPerSide = 0;
    RenderMaterialHandle m_chunkOpaque{};
    RenderMaterialHandle m_chunkTranslucent{};
    RenderTextureHandle m_entityAtlas{};
    RenderMaterialHandle m_entityMaterial{};
    std::array<RenderMeshHandle, 9> m_compatibilityCubes{};
    glm::mat4 m_viewProjection{1.0f};
    Frustum m_frustum;
    RenderEnvironment m_environment;
    glm::vec3 m_cameraPosition{0.0f};
    VisualQuality m_visualQuality = VisualQuality::Medium;
    std::unique_ptr<model::ModelRenderer> m_modelRenderer;
};
