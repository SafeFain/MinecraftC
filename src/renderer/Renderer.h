#pragma once

#include <memory>
#include <vector>
#include <glm/glm.hpp>

#include <glad/glad.h>
#include "renderer/Shader.h"
#include "renderer/Camera.h"
#include "renderer/Frustum.h"

// Forward declaration
class ChunkMesh;

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void initialize();
    void beginFrame();
    void endFrame();

    // Chunk rendering
    void renderChunk(const ChunkMesh& mesh, const glm::mat4& modelMatrix,
                     const glm::mat4& viewProjection);

    // Bind/unbind block shader (call once per frame, not per chunk)
    void bindBlockShader() const;
    void unbindBlockShader() const;

    // Wireframe highlight
    void renderWireframe(const glm::vec3& blockPosition,
                         const glm::mat4& viewProjection);

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

private:
    std::unique_ptr<Shader> m_blockShader;
    std::unique_ptr<Shader> m_wireShader;

    // Shared wireframe cube GPU resources
    GLuint m_wireVAO = 0;
    size_t m_wireVertexCount = 0;

    glm::mat4 m_viewProjection{1.0f};
    Frustum m_frustum;
};
