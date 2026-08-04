#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>
#include <glm/glm.hpp>

#include <glad/glad.h>
#include "renderer/Shader.h"
#include "renderer/Camera.h"
#include "renderer/Frustum.h"
#include "renderer/BlockTextureAtlas.h"
#include "renderer/RenderEnvironment.h"
#include "renderer/ParticleSystem.h"
#include "world/BlockLightLogic.h"

// Forward declaration
struct ChunkMesh;
namespace model { class ModelRenderer; }

class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void initialize(const GraphicsCapabilities& capabilities,
                    const std::filesystem::path& assetRoot);
    void reinitialize(const GraphicsCapabilities& capabilities,
                      const std::filesystem::path& assetRoot);
    void beginFrame();
    void endFrame();
    void setEnvironment(const RenderEnvironment& environment,
                        const glm::vec3& cameraPosition);
    void renderSky(const RenderEnvironment& environment,
                   const glm::mat4& inverseViewProjection,
                   const glm::vec3& cameraPosition, bool renderClouds);

    // Chunk rendering
    void renderChunk(const ChunkMesh& mesh, const glm::mat4& modelMatrix,
                     const glm::mat4& viewProjection,
                     bool translucent = false);
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
        const glm::mat4& viewProjection, SmoothLightSample light = {});
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
    static GLuint createVAO(const std::vector<float>& vertices,
                            const std::vector<float>& colors,
                            const std::vector<unsigned int>& indices,
                            size_t& outIndexCount);

    static GLuint createLineVAO(const std::vector<float>& vertices,
                                size_t& outVertexCount);

    static void deleteVAO(GLuint vao);

    // Setters for current-frame camera data
    void setViewProjection(const glm::mat4& vp) { m_viewProjection = vp; }
    void setFrustum(const Frustum& f) { m_frustum = f; }
    const Frustum& getFrustum() const { return m_frustum; }
    GLuint getBlockAtlasTexture() const { return m_blockAtlas.textureId(); }
    bool usesFramebufferSrgb() const { return m_framebufferSrgb; }

private:
    struct CloudInstance {
        float x, y, z;
        float width, depth, height;
    };
    static constexpr size_t MAX_CLOUD_INSTANCES = 2u * 129u * 129u;
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
    GLuint m_wireVAO = 0;
    size_t m_wireVertexCount = 0;
    GLuint m_skyVAO = 0;
    GLuint m_entityVAO = 0;
    GLuint m_entityVBO = 0;
    GLuint m_entityTexture = 0;
    GLuint m_cloudVAO = 0;
    GLuint m_cloudInstanceVBOs[CLOUD_INSTANCE_BUFFER_COUNT]{};
    size_t m_cloudInstanceBufferIndex = 0;
    GLuint m_particleVAO = 0;
    GLuint m_particleQuadVBO = 0;
    GLuint m_particleInstanceVBO = 0;

#if defined(_WIN32)
    using DrawArraysInstancedFn = void (__stdcall *)(GLenum, GLint, GLsizei,
                                                     GLsizei);
    using VertexAttribDivisorFn = void (__stdcall *)(GLuint, GLuint);
    using BufferSubDataFn = void (__stdcall *)(GLenum, GLintptr, GLsizeiptr,
                                               const void*);
#else
    using DrawArraysInstancedFn = void (*)(GLenum, GLint, GLsizei, GLsizei);
    using VertexAttribDivisorFn = void (*)(GLuint, GLuint);
    using BufferSubDataFn = void (*)(GLenum, GLintptr, GLsizeiptr, const void*);
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
};
