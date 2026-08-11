#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

#include "renderer/Shader.h"
#include "renderer/Camera.h"
#include "renderer/Frustum.h"
#include "renderer/BlockTextureAtlas.h"
#include "renderer/RenderEnvironment.h"
#include "renderer/ParticleSystem.h"
#include "renderer/CloudRenderData.h"
#include "world/BlockLightLogic.h"
#include "renderer/GameRenderer.h"

// Forward declaration
struct ChunkMesh;
namespace model { class ModelRenderer; }

class Renderer final : public IGameRenderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void initialize(Window& window, const GraphicsCapabilities& capabilities,
                    const std::filesystem::path& assetRoot);
    void reinitialize(const GraphicsCapabilities& capabilities,
                      const std::filesystem::path& assetRoot);
    void beginFrame();
    void endFrame();
    void resize(int width, int height);
    void setEnvironment(const RenderEnvironment& environment,
                        const glm::vec3& cameraPosition);
    void renderSky(const RenderEnvironment& environment,
                   const glm::mat4& inverseViewProjection,
                   const glm::vec3& cameraPosition, bool renderClouds);

    // Chunk rendering
    void renderChunk(const ChunkMesh& mesh, const glm::mat4& modelMatrix,
                     const glm::mat4& viewProjection,
                     bool translucent = false);
    void uploadChunkMesh(ChunkMesh& mesh);
    void releaseChunkMesh(ChunkMesh& mesh);
    void beginTranslucent();
    void endTranslucent();

    // Bind/unbind block shader (call once per frame, not per chunk)
    void bindBlockShader() const;
    void unbindBlockShader() const;

    // Wireframe highlight
    void renderWireframe(const glm::vec3& blockPosition,
                         const glm::mat4& viewProjection);
    void renderEntity(const glm::vec3& position, const glm::vec3& size,
                      const glm::vec3& color, int textureIndex,
                      const glm::mat4& viewProjection);
    void renderCompatibilityEntityCube(
        const glm::vec3& position, const glm::vec3& size,
        const glm::vec3& color, int textureIndex,
        float yaw, const glm::mat4& viewProjection,
        SmoothLightSample light = {});
    model::ModelRenderer& modelRenderer();
    void flushModels(const glm::mat4& viewProjection);
    void renderEntityPart(const glm::vec3& position, const glm::vec3& offset,
                          const glm::vec3& size, float yaw,
                          const glm::vec3& color, int textureIndex,
                          const glm::mat4& viewProjection,
                          SmoothLightSample light = {1.0f, 0.0f});
    void renderParticles(const std::vector<ParticleRenderData>& particles,
                         const glm::mat4& viewProjection,
                         const glm::vec3& cameraRight,
                         const glm::vec3& cameraUp, float intensity);
    void renderClouds(const glm::dvec3& playerPosition,
                      const glm::mat4& viewProjection, uint64_t worldSeed,
                      float timeSeconds, int renderDistanceBlocks);

    // VAO creation helpers
    static uint32_t createVAO(const std::vector<float>& vertices,
                            const std::vector<float>& colors,
                            const std::vector<unsigned int>& indices,
                            size_t& outIndexCount);

    static uint32_t createLineVAO(const std::vector<float>& vertices,
                                size_t& outVertexCount);

    static void deleteVAO(uint32_t vao);

    // Setters for current-frame camera data
    void setViewProjection(const glm::mat4& vp) { m_viewProjection = vp; }
    void setFrustum(const Frustum& f) { m_frustum = f; }
    const Frustum& getFrustum() const { return m_frustum; }
    RenderTextureHandle getBlockAtlasTexture() const { return m_blockAtlas.texture(); }
    bool usesFramebufferSrgb() const { return m_framebufferSrgb; }

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
    void waitIdle() override;
    RendererPerformanceStats performanceStats() const override {
        return m_performanceStats;
    }

private:
    RendererPerformanceStats m_performanceStats{};
    struct GpuChunkMesh {
        uint32_t vao = 0;
        uint32_t vbo = 0;
        uint32_t ebo = 0;
    };
    struct BasicMesh {
        uint32_t vao = 0;
        uint32_t vbo = 0;
        uint32_t ebo = 0;
        size_t indexCount = 0;
        MeshVertexLayout layout = MeshVertexLayout::PositionUv;
    };
    struct BasicTexture { uint32_t texture = 0; };
    struct BasicMaterial { MaterialDesc desc{}; };
    static constexpr size_t CLOUD_INSTANCE_BUFFER_COUNT = 3;

    std::unique_ptr<Shader> m_blockShader;
    std::unique_ptr<Shader> m_wireShader;
    std::unique_ptr<Shader> m_skyShader;
    std::unique_ptr<Shader> m_entityShader;
    std::unique_ptr<Shader> m_cloudShader;
    std::unique_ptr<Shader> m_particleShader;
    std::unique_ptr<model::ModelRenderer> m_modelRenderer;
    BlockTextureAtlas m_blockAtlas;

    // Shared wireframe cube GPU resources
    uint32_t m_wireVAO = 0;
    size_t m_wireVertexCount = 0;
    uint32_t m_skyVAO = 0;
    uint32_t m_entityVAO = 0;
    uint32_t m_entityVBO = 0;
    uint32_t m_entityTexture = 0;
    uint32_t m_cloudVAO = 0;
    uint32_t m_cloudInstanceVBOs[CLOUD_INSTANCE_BUFFER_COUNT]{};
    size_t m_cloudInstanceBufferIndex = 0;
    uint32_t m_particleVAO = 0;
    uint32_t m_particleQuadVBO = 0;
    uint32_t m_particleInstanceVBO = 0;

#if defined(_WIN32)
    using DrawArraysInstancedFn = void (__stdcall *)(uint32_t, int, int, int);
    using VertexAttribDivisorFn = void (__stdcall *)(uint32_t, uint32_t);
    using BufferSubDataFn = void (__stdcall *)(uint32_t, std::intptr_t,
                                               std::intptr_t,
                                               const void*);
#else
    using DrawArraysInstancedFn = void (*)(uint32_t, int, int, int);
    using VertexAttribDivisorFn = void (*)(uint32_t, uint32_t);
    using BufferSubDataFn = void (*)(uint32_t, std::intptr_t, std::intptr_t,
                                     const void*);
#endif
    DrawArraysInstancedFn m_drawArraysInstanced = nullptr;
    VertexAttribDivisorFn m_vertexAttribDivisor = nullptr;
    BufferSubDataFn m_bufferSubData = nullptr;
    std::vector<CloudInstance> m_cloudInstances;
    uint64_t m_cloudCacheSeed = 0;
    int m_cloudCacheCenterX = 0;
    int m_cloudCacheCenterZ = 0;
    int m_cloudCacheRadius = -1;

    glm::mat4 m_viewProjection{1.0f};
    Frustum m_frustum;
    RenderEnvironment m_environment;
    glm::vec3 m_cameraPosition{0.0f};
    bool m_framebufferSrgb = false;
    GraphicsApi m_graphicsApi = GraphicsApi::OpenGL33;
    std::unordered_map<uint32_t, GpuChunkMesh> m_chunkMeshes;
    uint32_t m_nextChunkMeshHandle = 1;
    Window* m_window = nullptr;
    std::filesystem::path m_assetRoot;
    std::unique_ptr<Shader> m_basicShader;
    FrameData m_basicFrame;
    std::unordered_map<uint32_t, BasicMesh> m_basicMeshes;
    std::unordered_map<uint32_t, BasicTexture> m_basicTextures;
    std::unordered_map<uint32_t, BasicMaterial> m_basicMaterials;
    uint32_t m_nextBasicMeshHandle = 0x40000000u;
    uint32_t m_nextBasicTextureHandle = 0x40000000u;
    uint32_t m_nextBasicMaterialHandle = 1;
};
