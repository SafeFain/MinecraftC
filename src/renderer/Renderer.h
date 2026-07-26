#pragma once

#include <memory>
#include <vector>
#include <glm/glm.hpp>

#include <glad/glad.h>
#include "renderer/Shader.h"
#include "renderer/Camera.h"
#include "renderer/Frustum.h"
#include "renderer/BlockTextureAtlas.h"
#include "renderer/RenderEnvironment.h"

// Forward declaration
class ChunkMesh;

struct WeatherParticle {
    glm::vec3 position{0.0f};
    float kind = 0.0f;
    float phase = 0.0f;
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void initialize(bool framebufferSrgb);
    void beginFrame();
    void endFrame();
    void setEnvironment(const RenderEnvironment& environment,
                        const glm::vec3& cameraPosition);
    void renderSky(const RenderEnvironment& environment,
                   const glm::mat4& inverseViewProjection,
                   const glm::vec3& cameraPosition);

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
    void renderWeather(const std::vector<WeatherParticle>& particles,
                       const glm::mat4& viewProjection,
                       const glm::vec3& cameraRight, float intensity);

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
    std::unique_ptr<Shader> m_blockShader;
    std::unique_ptr<Shader> m_wireShader;
    std::unique_ptr<Shader> m_skyShader;
    std::unique_ptr<Shader> m_entityShader;
    std::unique_ptr<Shader> m_weatherShader;
    BlockTextureAtlas m_blockAtlas;

    // Shared wireframe cube GPU resources
    GLuint m_wireVAO = 0;
    size_t m_wireVertexCount = 0;
    GLuint m_skyVAO = 0;
    GLuint m_entityVAO = 0;
    GLuint m_entityVBO = 0;
    GLuint m_entityTexture = 0;
    GLuint m_weatherVAO = 0;
    GLuint m_weatherQuadVBO = 0;
    GLuint m_weatherInstanceVBO = 0;

    using DrawArraysInstancedFn = void (*)(GLenum, GLint, GLsizei, GLsizei);
    using VertexAttribDivisorFn = void (*)(GLuint, GLuint);
    DrawArraysInstancedFn m_drawArraysInstanced = nullptr;
    VertexAttribDivisorFn m_vertexAttribDivisor = nullptr;

    glm::mat4 m_viewProjection{1.0f};
    Frustum m_frustum;
    RenderEnvironment m_environment;
    glm::vec3 m_cameraPosition{0.0f};
    bool m_framebufferSrgb = false;
};
