#include "renderer/Renderer.h"
#include "world/ChunkMesh.h"
#include "debug/OpenGL.h"

#include <vector>
#include <cmath>
#include "Config.h"

// ── Wireframe cube geometry (12 line segments = 24 vertices) ──────────

static const std::vector<float> WIRE_CUBE = {
    // Bottom face
    0,0,0, 1,0,0,   1,0,0, 1,0,1,   1,0,1, 0,0,1,   0,0,1, 0,0,0,
    // Top face
    0,1,0, 1,1,0,   1,1,0, 1,1,1,   1,1,1, 0,1,1,   0,1,1, 0,1,0,
    // Vertical edges
    0,0,0, 0,1,0,   1,0,0, 1,1,0,   1,0,1, 1,1,1,   0,0,1, 0,1,1,
};

// ── Constructor / Destructor ──────────────────────────────────────────

Renderer::~Renderer() {
    if (m_wireVAO) deleteVAO(m_wireVAO);
}

// ── Initialization ────────────────────────────────────────────────────

void Renderer::initialize() {
    // Compile shaders
    m_blockShader = std::make_unique<Shader>(
        "assets/shaders/block.vert",
        "assets/shaders/block.frag"
    );
    m_wireShader = std::make_unique<Shader>(
        "assets/shaders/wireframe.vert",
        "assets/shaders/wireframe.frag"
    );

    // Global GL state
    GL_CHECK(glEnable(GL_DEPTH_TEST));
    GL_CHECK(glEnable(GL_CULL_FACE));
    GL_CHECK(glCullFace(GL_BACK));
    GL_CHECK(glClearColor(Config::SKY_COLOR.r, Config::SKY_COLOR.g,
                 Config::SKY_COLOR.b, Config::SKY_COLOR.a));

    // Create shared wireframe cube VAO
    m_wireVAO = createLineVAO(WIRE_CUBE, m_wireVertexCount);
}

// ── Frame management ──────────────────────────────────────────────────

void Renderer::beginFrame() {
    GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
}

void Renderer::endFrame() {
    // Currently a no-op; swap happens in main loop
}

// ── Chunk rendering ───────────────────────────────────────────────────

void Renderer::renderChunk(const ChunkMesh& mesh, const glm::mat4& modelMatrix,
                           const glm::mat4& viewProjection) {
    if (!mesh.gpuReady || mesh.indexCount == 0) return;

    glm::mat4 mvp = viewProjection * modelMatrix;

    // Shader is expected to already be bound (caller binds once per frame)
    m_blockShader->setMat4("uMVP", mvp);
    GL_CHECK(glBindVertexArray(mesh.vao));
    GL_CHECK(glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indexCount),
                   GL_UNSIGNED_INT, nullptr));
    // VAO stays bound — next draw will bind its own
}

void Renderer::bindBlockShader() const {
    m_blockShader->bind();
}

void Renderer::unbindBlockShader() const {
    glUseProgram(0);
}

// ── Wireframe highlight ───────────────────────────────────────────────

void Renderer::renderWireframe(const glm::vec3& blockPos,
                               const glm::mat4& viewProjection) {
    if (m_wireVAO == 0) return;

    glm::mat4 model = glm::translate(glm::mat4(1.0f), blockPos);
    glm::mat4 mvp = viewProjection * model;

    // Slightly enlarge to avoid z-fighting
    // (model matrix includes translation only; we use polygon offset)

    GL_CHECK(glPolygonOffset(-1.0f, -1.0f));
    GL_CHECK(glEnable(GL_POLYGON_OFFSET_LINE));

    m_wireShader->bind();
    m_wireShader->setMat4("uMVP", mvp);
    GL_CHECK(glBindVertexArray(m_wireVAO));
    GL_CHECK(glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(m_wireVertexCount)));
    GL_CHECK(glBindVertexArray(0));

    GL_CHECK(glDisable(GL_POLYGON_OFFSET_LINE));
}

// ── VAO helpers ───────────────────────────────────────────────────────

GLuint Renderer::createVAO(const std::vector<float>& vertices,
                           const std::vector<float>& colors,
                           const std::vector<unsigned int>& indices,
                           size_t& outIndexCount) {
    if (vertices.empty() || indices.empty()) {
        outIndexCount = 0;
        return 0;
    }

    GLuint vao, vboPos, vboCol, ebo;
    GL_CHECK(glGenVertexArrays(1, &vao));
    GL_CHECK(glBindVertexArray(vao));

    // Position VBO
    GL_CHECK(glGenBuffers(1, &vboPos));
    GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, vboPos));
    GL_CHECK(glBufferData(GL_ARRAY_BUFFER,
                 vertices.size() * sizeof(float),
                 vertices.data(), GL_STATIC_DRAW));
    GL_CHECK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr));
    GL_CHECK(glEnableVertexAttribArray(0));

    // Color VBO
    GL_CHECK(glGenBuffers(1, &vboCol));
    GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, vboCol));
    GL_CHECK(glBufferData(GL_ARRAY_BUFFER,
                 colors.size() * sizeof(float),
                 colors.data(), GL_STATIC_DRAW));
    GL_CHECK(glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr));
    GL_CHECK(glEnableVertexAttribArray(1));

    // Element buffer
    GL_CHECK(glGenBuffers(1, &ebo));
    GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo));
    GL_CHECK(glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 indices.size() * sizeof(unsigned int),
                 indices.data(), GL_STATIC_DRAW));

    GL_CHECK(glBindVertexArray(0));
    // NOTE: VBO and EBO names are intentionally NOT deleted here.
    // The VAO holds references to the buffer objects; deleting buffer names
    // while the VAO references them is implementation-defined behavior.
    // Buffer objects are freed when deleteVAO() deletes the VAO.

    outIndexCount = indices.size();
    return vao;
}

GLuint Renderer::createLineVAO(const std::vector<float>& vertices,
                               size_t& outVertexCount) {
    if (vertices.empty()) {
        outVertexCount = 0;
        return 0;
    }

    GLuint vao, vbo;
    GL_CHECK(glGenVertexArrays(1, &vao));
    GL_CHECK(glBindVertexArray(vao));

    GL_CHECK(glGenBuffers(1, &vbo));
    GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, vbo));
    GL_CHECK(glBufferData(GL_ARRAY_BUFFER,
                 vertices.size() * sizeof(float),
                 vertices.data(), GL_STATIC_DRAW));
    GL_CHECK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr));
    GL_CHECK(glEnableVertexAttribArray(0));

    GL_CHECK(glBindVertexArray(0));
    // NOTE: VBO name intentionally not deleted here (same reason as createVAO)

    outVertexCount = vertices.size() / 3;
    return vao;
}

void Renderer::deleteVAO(GLuint vao) {
    if (vao != 0) {
        GL_CHECK(glDeleteVertexArrays(1, &vao));
    }
}
