#include "renderer/Renderer.h"
#include "world/ChunkMesh.h"
#include "debug/OpenGL.h"
#include "debug/Log.h"

#include <vector>
#include <cmath>
#include <stb_image.h>
#include <GLFW/glfw3.h>
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
    if (m_skyVAO) GL_CHECK(glDeleteVertexArrays(1, &m_skyVAO));
    if (m_entityVBO) GL_CHECK(glDeleteBuffers(1, &m_entityVBO));
    if (m_entityVAO) GL_CHECK(glDeleteVertexArrays(1, &m_entityVAO));
    if (m_entityTexture) GL_CHECK(glDeleteTextures(1, &m_entityTexture));
    if (m_particleInstanceVBO) GL_CHECK(glDeleteBuffers(1, &m_particleInstanceVBO));
    if (m_particleQuadVBO) GL_CHECK(glDeleteBuffers(1, &m_particleQuadVBO));
    if (m_particleVAO) GL_CHECK(glDeleteVertexArrays(1, &m_particleVAO));
}

// ── Initialization ────────────────────────────────────────────────────

void Renderer::initialize(bool framebufferSrgb) {
    m_framebufferSrgb = framebufferSrgb;
    // Compile shaders
    m_blockShader = std::make_unique<Shader>(
        "assets/shaders/block.vert",
        "assets/shaders/block.frag"
    );
    m_wireShader = std::make_unique<Shader>(
        "assets/shaders/wireframe.vert",
        "assets/shaders/wireframe.frag"
    );
    m_skyShader = std::make_unique<Shader>(
        "assets/shaders/sky.vert",
        "assets/shaders/sky.frag"
    );
    m_entityShader = std::make_unique<Shader>(
        "assets/shaders/entity.vert",
        "assets/shaders/entity.frag"
    );
    m_particleShader = std::make_unique<Shader>(
        "assets/shaders/weather.vert", "assets/shaders/weather.frag");
    m_blockAtlas.initialize();

    // Global GL state
    GL_CHECK(glEnable(GL_DEPTH_TEST));
    GL_CHECK(glEnable(GL_CULL_FACE));
    GL_CHECK(glEnable(GL_MULTISAMPLE));
    if (m_framebufferSrgb) GL_CHECK(glEnable(GL_FRAMEBUFFER_SRGB));
    GLint samples = 0;
    GL_CHECK(glGetIntegerv(GL_SAMPLES, &samples));
    LOG_INFO("Visual pipeline: " << samples << "x MSAA, "
             << (m_framebufferSrgb ? "hardware sRGB" : "shader gamma fallback"));
    GL_CHECK(glCullFace(GL_BACK));
    GL_CHECK(glClearColor(Config::SKY_COLOR.r, Config::SKY_COLOR.g,
                 Config::SKY_COLOR.b, Config::SKY_COLOR.a));

    // Create shared wireframe cube VAO
    m_wireVAO = createLineVAO(WIRE_CUBE, m_wireVertexCount);
    GL_CHECK(glGenVertexArrays(1, &m_skyVAO));
    const float cubePositions[] = {
        0,0,0, 1,0,0, 1,1,0, 0,0,0, 1,1,0, 0,1,0,
        1,0,1, 0,0,1, 0,1,1, 1,0,1, 0,1,1, 1,1,1,
        0,0,1, 0,0,0, 0,1,0, 0,0,1, 0,1,0, 0,1,1,
        1,0,0, 1,0,1, 1,1,1, 1,0,0, 1,1,1, 1,1,0,
        0,1,0, 1,1,0, 1,1,1, 0,1,0, 1,1,1, 0,1,1,
        0,0,1, 1,0,1, 1,0,0, 0,0,1, 1,0,0, 0,0,0
    };
    constexpr float faceUvs[] = {
        0,0, 1,0, 1,1, 0,0, 1,1, 0,1
    };
    std::vector<float> entityVertices;
    entityVertices.reserve(36 * 5);
    for (int vertex = 0; vertex < 36; ++vertex) {
        entityVertices.insert(
            entityVertices.end(), cubePositions + vertex * 3,
            cubePositions + vertex * 3 + 3);
        const int faceVertex = vertex % 6;
        entityVertices.push_back(faceUvs[faceVertex * 2]);
        entityVertices.push_back(faceUvs[faceVertex * 2 + 1]);
    }
    GL_CHECK(glGenVertexArrays(1, &m_entityVAO));
    GL_CHECK(glGenBuffers(1, &m_entityVBO));
    GL_CHECK(glBindVertexArray(m_entityVAO));
    GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, m_entityVBO));
    GL_CHECK(glBufferData(GL_ARRAY_BUFFER,
                         entityVertices.size() * sizeof(float),
                         entityVertices.data(), GL_STATIC_DRAW));
    GL_CHECK(glVertexAttribPointer(
        0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr));
    GL_CHECK(glEnableVertexAttribArray(0));
    GL_CHECK(glVertexAttribPointer(
        1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
        reinterpret_cast<void*>(3 * sizeof(float))));
    GL_CHECK(glEnableVertexAttribArray(1));
    GL_CHECK(glBindVertexArray(0));

    m_drawArraysInstanced = reinterpret_cast<DrawArraysInstancedFn>(
        glfwGetProcAddress("glDrawArraysInstanced"));
    m_vertexAttribDivisor = reinterpret_cast<VertexAttribDivisorFn>(
        glfwGetProcAddress("glVertexAttribDivisor"));
    if (m_drawArraysInstanced && m_vertexAttribDivisor) {
        constexpr float quad[] = {
            -0.5f, 0.0f,  0.5f, 0.0f,  0.5f, 1.0f,
            -0.5f, 0.0f,  0.5f, 1.0f, -0.5f, 1.0f
        };
        GL_CHECK(glGenVertexArrays(1, &m_particleVAO));
        GL_CHECK(glGenBuffers(1, &m_particleQuadVBO));
        GL_CHECK(glGenBuffers(1, &m_particleInstanceVBO));
        GL_CHECK(glBindVertexArray(m_particleVAO));
        GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, m_particleQuadVBO));
        GL_CHECK(glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW));
        GL_CHECK(glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                                      2 * sizeof(float), nullptr));
        GL_CHECK(glEnableVertexAttribArray(0));
        GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, m_particleInstanceVBO));
        GL_CHECK(glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE,
                                      sizeof(ParticleRenderData), nullptr));
        GL_CHECK(glEnableVertexAttribArray(1));
        GL_CHECK(glVertexAttribPointer(
            2, 4, GL_FLOAT, GL_FALSE, sizeof(ParticleRenderData),
            reinterpret_cast<void*>(4 * sizeof(float))));
        GL_CHECK(glEnableVertexAttribArray(2));
        m_vertexAttribDivisor(1, 1);
        m_vertexAttribDivisor(2, 1);
        GL_CHECK(glBindVertexArray(0));
    } else {
        LOG_WARN("OpenGL instanced particle rendering is unavailable");
    }

    int atlasWidth = 0, atlasHeight = 0, atlasChannels = 0;
    stbi_set_flip_vertically_on_load(1);
    stbi_uc* atlas = stbi_load(
        "assets/textures/entity_atlas.png",
        &atlasWidth, &atlasHeight, &atlasChannels, 4);
    if (atlas && atlasWidth == atlasHeight && atlasWidth % 3 == 0) {
        GL_CHECK(glGenTextures(1, &m_entityTexture));
        GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_entityTexture));
        GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8,
                             atlasWidth, atlasHeight, 0, GL_RGBA,
                             GL_UNSIGNED_BYTE, atlas));
        GL_CHECK(glTexParameteri(
            GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
        GL_CHECK(glTexParameteri(
            GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
        GL_CHECK(glTexParameteri(
            GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        GL_CHECK(glTexParameteri(
            GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
        GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
    } else {
        LOG_WARN("Entity texture atlas unavailable or not a square 3x3 atlas");
    }
    stbi_image_free(atlas);
}

// ── Frame management ──────────────────────────────────────────────────

void Renderer::beginFrame() {
    GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
}

void Renderer::endFrame() {
    // Currently a no-op; swap happens in main loop
}

void Renderer::setEnvironment(const RenderEnvironment& environment,
                              const glm::vec3& cameraPosition) {
    m_environment = environment;
    m_cameraPosition = cameraPosition;
}

void Renderer::renderSky(const RenderEnvironment& environment,
                         const glm::mat4& inverseViewProjection,
                         const glm::vec3& cameraPosition) {
    GL_CHECK(glDisable(GL_DEPTH_TEST));
    GL_CHECK(glDisable(GL_CULL_FACE));
    GL_CHECK(glDepthMask(GL_FALSE));

    m_skyShader->bind();
    m_skyShader->setMat4("uInverseViewProjection", inverseViewProjection);
    m_skyShader->setVec3("uCameraPosition", cameraPosition);
    m_skyShader->setVec3("uSunDirection", environment.sunDirection);
    m_skyShader->setVec3("uMoonDirection", environment.moonDirection);
    m_skyShader->setVec3("uZenithColor", environment.zenithColor);
    m_skyShader->setVec3("uHorizonColor", environment.horizonColor);
    m_skyShader->setFloat("uStarIntensity", environment.starIntensity);
    m_skyShader->setFloat("uRainIntensity", environment.rainIntensity);
    m_skyShader->setFloat("uThunderIntensity", environment.thunderIntensity);
    m_skyShader->setFloat("uWeatherTime", static_cast<float>(glfwGetTime()));
    m_skyShader->setInt("uManualGamma", m_framebufferSrgb ? 0 : 1);
    GL_CHECK(glBindVertexArray(m_skyVAO));
    GL_CHECK(glDrawArrays(GL_TRIANGLES, 0, 3));
    GL_CHECK(glBindVertexArray(0));

    GL_CHECK(glDepthMask(GL_TRUE));
    GL_CHECK(glEnable(GL_CULL_FACE));
    GL_CHECK(glEnable(GL_DEPTH_TEST));
}

// ── Chunk rendering ───────────────────────────────────────────────────

void Renderer::renderChunk(const ChunkMesh& mesh, const glm::mat4& modelMatrix,
                           const glm::mat4& viewProjection, bool translucent) {
    if (!mesh.gpuReady || mesh.indexCount == 0) return;
    size_t count = translucent ? mesh.translucentIndexCount : mesh.opaqueIndexCount;
    size_t offset = translucent ? mesh.translucentIndexOffset : 0;
    if (count == 0) return;

    glm::mat4 mvp = viewProjection * modelMatrix;

    // Shader is expected to already be bound (caller binds once per frame)
    m_blockShader->setMat4("uMVP", mvp);
    m_blockShader->setVec3("uChunkOrigin", glm::vec3(modelMatrix[3]));
    GL_CHECK(glBindVertexArray(mesh.vao));
    GL_CHECK(glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(count),
                   GL_UNSIGNED_INT,
                   reinterpret_cast<void*>(offset * sizeof(unsigned int))));
    // VAO stays bound — next draw will bind its own
}

void Renderer::beginTranslucent() {
    GL_CHECK(glEnable(GL_BLEND));
    GL_CHECK(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    GL_CHECK(glDepthMask(GL_FALSE));
}

void Renderer::endTranslucent() {
    GL_CHECK(glDepthMask(GL_TRUE));
    GL_CHECK(glDisable(GL_BLEND));
}

void Renderer::bindBlockShader() const {
    m_blockShader->bind();
    m_blockAtlas.bind();
    m_blockShader->setInt("uBlockAtlas", 0);
    m_blockShader->setFloat("uAtlasTiles", 8.0f);
    m_blockShader->setFloat(
        "uLavaTile", static_cast<float>(getAtlasTextureIndex(BlockTexture::Lava)));
    m_blockShader->setFloat(
        "uWaterTile", static_cast<float>(getAtlasTextureIndex(BlockTexture::Water)));
    m_blockShader->setVec3("uCameraPosition", m_cameraPosition);
    m_blockShader->setVec3("uLightDirection", m_environment.lightDirection);
    m_blockShader->setVec3("uDirectColor", m_environment.directColor);
    m_blockShader->setVec3("uAmbientColor", m_environment.ambientColor);
    m_blockShader->setVec3("uFogColor", m_environment.fogColor);
    m_blockShader->setFloat("uDirectIntensity", m_environment.directIntensity);
    m_blockShader->setFloat("uAmbientIntensity", m_environment.ambientIntensity);
    m_blockShader->setFloat(
        "uFogEnd", (static_cast<float>(Config::RENDER_DISTANCE) + 0.5f) *
                   Config::CHUNK_SIZE_X);
    m_blockShader->setFloat("uFogStartFraction", Config::FOG_START_FRACTION);
    m_blockShader->setInt("uManualGamma", m_framebufferSrgb ? 0 : 1);
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

void Renderer::renderEntity(const glm::vec3& position, const glm::vec3& size,
                            const glm::vec3& color, int textureIndex,
                            const glm::mat4& viewProjection) {
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), position) *
                            glm::scale(glm::mat4(1.0f), size);
    m_entityShader->bind();
    m_entityShader->setMat4("uMVP", viewProjection * model);
    m_entityShader->setVec3("uColor", color);
    m_entityShader->setInt("uTextureIndex", textureIndex);
    m_entityShader->setInt("uEntityAtlas", 0);
    m_entityShader->setInt("uUseTexture", m_entityTexture ? 1 : 0);
    m_entityShader->setInt("uManualGamma", m_framebufferSrgb ? 0 : 1);
    GL_CHECK(glActiveTexture(GL_TEXTURE0));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_entityTexture));
    GL_CHECK(glBindVertexArray(m_entityVAO));
    GL_CHECK(glDrawArrays(GL_TRIANGLES, 0, 36));
    GL_CHECK(glBindVertexArray(0));
}

void Renderer::renderParticles(const std::vector<ParticleRenderData>& particles,
                               const glm::mat4& viewProjection,
                               const glm::vec3& cameraRight,
                               const glm::vec3& cameraUp, float intensity) {
    if (particles.empty() || !m_particleVAO || !m_drawArraysInstanced) return;
    m_particleShader->bind();
    m_particleShader->setMat4("uViewProjection", viewProjection);
    m_particleShader->setVec3("uCameraRight", cameraRight);
    m_particleShader->setVec3("uCameraUp", cameraUp);
    m_particleShader->setFloat("uTime", static_cast<float>(glfwGetTime()));
    m_particleShader->setFloat("uIntensity", intensity);
    m_particleShader->setInt("uBlockAtlas", 0);
    m_particleShader->setFloat("uAtlasTiles", 8.0f);
    m_particleShader->setInt("uManualGamma", m_framebufferSrgb ? 0 : 1);
    m_blockAtlas.bind();
    GL_CHECK(glEnable(GL_BLEND));
    GL_CHECK(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    GL_CHECK(glDepthMask(GL_FALSE));
    GL_CHECK(glDisable(GL_CULL_FACE));
    GL_CHECK(glBindVertexArray(m_particleVAO));
    GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, m_particleInstanceVBO));
    GL_CHECK(glBufferData(GL_ARRAY_BUFFER,
                         particles.size() * sizeof(ParticleRenderData),
                         particles.data(), GL_DYNAMIC_DRAW));
    m_drawArraysInstanced(
        GL_TRIANGLES, 0, 6, static_cast<GLsizei>(particles.size()));
    GL_CHECK(glBindVertexArray(0));
    GL_CHECK(glEnable(GL_CULL_FACE));
    GL_CHECK(glDepthMask(GL_TRUE));
    GL_CHECK(glDisable(GL_BLEND));
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
