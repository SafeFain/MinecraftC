#include "renderer/Renderer.h"
#include "model/ModelRenderer.h"
#include "world/ChunkMesh.h"
#include "renderer/backend/opengl/OpenGLDebug.h"
#include "debug/Log.h"

#include <algorithm>
#include <vector>
#include <cmath>
#include <new>
#include <stb_image.h>
#include "core/Window.h"
#include "Config.h"
#include "core/AssetStore.h"
#include "core/RuntimeClock.h"

namespace {
constexpr unsigned int GL_FRAMEBUFFER_VALUE = 0x8D40;
constexpr unsigned int GL_DEPTH_ATTACHMENT_VALUE = 0x8D00;
constexpr unsigned int GL_COLOR_ATTACHMENT0_VALUE = 0x8CE0;
constexpr unsigned int GL_FRAMEBUFFER_COMPLETE_VALUE = 0x8CD5;
constexpr unsigned int GL_RENDERBUFFER_VALUE = 0x8D41;
constexpr unsigned int GL_READ_FRAMEBUFFER_VALUE = 0x8CA8;
constexpr unsigned int GL_DRAW_FRAMEBUFFER_VALUE = 0x8CA9;
constexpr unsigned int GL_DEPTH_COMPONENT_VALUE = 0x1902;
constexpr unsigned int GL_DEPTH_COMPONENT24_VALUE = 0x81A6;
constexpr unsigned int GL_RGBA16F_VALUE = 0x881A;
constexpr unsigned int GL_RGBA8_VALUE = 0x8058;
constexpr unsigned int GL_HALF_FLOAT_VALUE = 0x140B;
constexpr unsigned int GL_MAX_SAMPLES_VALUE = 0x8D57;
constexpr unsigned int GL_CLAMP_TO_EDGE_VALUE = 0x812F;
constexpr unsigned int GL_NONE_VALUE = 0;
constexpr unsigned int GL_TEXTURE1_VALUE = 0x84C1;
constexpr unsigned int GL_NEAREST_VALUE = 0x2600;
constexpr unsigned int GL_LINEAR_VALUE = 0x2601;
using GenFramebuffersFn = void (*)(int, unsigned int*);
using DeleteFramebuffersFn = void (*)(int, const unsigned int*);
using BindFramebufferFn = void (*)(unsigned int, unsigned int);
using FramebufferTexture2DFn = void (*)(unsigned int,unsigned int,unsigned int,unsigned int,int);
using CheckFramebufferStatusFn = unsigned int (*)(unsigned int);
using DrawBufferFn = void (*)(unsigned int);
using ReadBufferFn = void (*)(unsigned int);
using ColorMaskFn = void (*)(unsigned char,unsigned char,unsigned char,unsigned char);
using GenRenderbuffersFn = void (*)(int, unsigned int*);
using DeleteRenderbuffersFn = void (*)(int, const unsigned int*);
using BindRenderbufferFn = void (*)(unsigned int, unsigned int);
using RenderbufferStorageFn = void (*)(unsigned int, unsigned int, int, int);
using RenderbufferStorageMultisampleFn = void (*)(unsigned int, int, unsigned int,
                                                  int, int);
using FramebufferRenderbufferFn = void (*)(unsigned int, unsigned int,
                                           unsigned int, unsigned int);
using BlitFramebufferFn = void (*)(int, int, int, int, int, int, int, int,
                                   unsigned int, unsigned int);

template<typename T> T glProc(const char* name) {
    return reinterpret_cast<T>(Window::graphicsProcAddress(name));
}
}

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

Renderer::Renderer() = default;

Renderer::~Renderer() {
    destroySceneTarget();
    if (m_shadowTexture) GL_CHECK(glDeleteTextures(1, &m_shadowTexture));
    if (m_shadowFramebuffer) {
        if (auto destroy = glProc<DeleteFramebuffersFn>("glDeleteFramebuffers"))
            destroy(1, &m_shadowFramebuffer);
    }
    for (auto& [handle, texture] : m_basicTextures) {
        (void)handle;
        if (texture.texture) GL_CHECK(glDeleteTextures(1, &texture.texture));
    }
    for (auto& [handle, mesh] : m_basicMeshes) {
        (void)handle;
        if (mesh.vbo) GL_CHECK(glDeleteBuffers(1, &mesh.vbo));
        if (mesh.ebo) GL_CHECK(glDeleteBuffers(1, &mesh.ebo));
        if (mesh.vao) GL_CHECK(glDeleteVertexArrays(1, &mesh.vao));
    }
    for (auto& [handle, mesh] : m_chunkMeshes) {
        (void)handle;
        if (mesh.vbo) GL_CHECK(glDeleteBuffers(1, &mesh.vbo));
        if (mesh.ebo) GL_CHECK(glDeleteBuffers(1, &mesh.ebo));
        if (mesh.vao) GL_CHECK(glDeleteVertexArrays(1, &mesh.vao));
    }
    if (m_wireVAO) deleteVAO(m_wireVAO);
    if (m_skyVAO) GL_CHECK(glDeleteVertexArrays(1, &m_skyVAO));
    if (m_entityVBO) GL_CHECK(glDeleteBuffers(1, &m_entityVBO));
    if (m_entityVAO) GL_CHECK(glDeleteVertexArrays(1, &m_entityVAO));
    if (m_entityTexture) GL_CHECK(glDeleteTextures(1, &m_entityTexture));
    GL_CHECK(glDeleteBuffers(
        static_cast<GLsizei>(CLOUD_INSTANCE_BUFFER_COUNT),
        m_cloudInstanceVBOs));
    if (m_cloudVAO) GL_CHECK(glDeleteVertexArrays(1, &m_cloudVAO));
    if (m_particleInstanceVBO) GL_CHECK(glDeleteBuffers(1, &m_particleInstanceVBO));
    if (m_particleQuadVBO) GL_CHECK(glDeleteBuffers(1, &m_particleQuadVBO));
    if (m_particleVAO) GL_CHECK(glDeleteVertexArrays(1, &m_particleVAO));
}

void Renderer::reinitialize(const GraphicsCapabilities& capabilities,
                            const std::filesystem::path& assetRoot) {
    if (!gladLoadGL(Window::graphicsProcAddress))
        throw std::runtime_error("Failed to reload OpenGL functions");
    Window* window = m_window;
    this->~Renderer();
    new (this) Renderer();
    initialize(*window, capabilities, assetRoot);
}

// ── Initialization ────────────────────────────────────────────────────

void Renderer::initialize(Window& window, const GraphicsCapabilities& capabilities,
                          const std::filesystem::path& assetRoot) {
    if (!gladLoadGL(Window::graphicsProcAddress))
        throw std::runtime_error("Failed to load OpenGL functions");
    if (capabilities.api == GraphicsApi::OpenGLES30 && capabilities.majorVersion < 3)
        throw std::runtime_error("OpenGL ES 3.0 or newer is required");
    LOG_INFO("OpenGL: " << reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    LOG_INFO("Renderer: " << reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    m_window = &window;
    m_assetRoot = assetRoot;
    m_framebufferSrgb = capabilities.framebufferSrgb;
    m_graphicsApi = capabilities.api;
    // Models are rendered into a linear scene target. Transfer encoding occurs
    // once in the composition pass, after every world-space draw.
    m_modelRenderer = model::createOpenGLModelRenderer(
        assetRoot, true, m_graphicsApi);
    // Compile shaders
    m_blockShader = std::make_unique<Shader>(
        assetRoot / "shaders" / "block.vert",
        assetRoot / "shaders" / "block.frag", m_graphicsApi
    );
    m_shadowShader = std::make_unique<Shader>(
        assetRoot / "shaders" / "shadow.vert",
        assetRoot / "shaders" / "shadow.frag", m_graphicsApi);
    m_wireShader = std::make_unique<Shader>(
        assetRoot / "shaders" / "wireframe.vert",
        assetRoot / "shaders" / "wireframe.frag", m_graphicsApi
    );
    m_skyShader = std::make_unique<Shader>(
        assetRoot / "shaders" / "sky.vert",
        assetRoot / "shaders" / "sky.frag", m_graphicsApi
    );
    m_entityShader = std::make_unique<Shader>(
        assetRoot / "shaders" / "entity.vert",
        assetRoot / "shaders" / "entity.frag", m_graphicsApi
    );
    m_cloudShader = std::make_unique<Shader>(
        assetRoot / "shaders" / "cloud.vert",
        assetRoot / "shaders" / "cloud.frag", m_graphicsApi
    );
    m_particleShader = std::make_unique<Shader>(
        assetRoot / "shaders" / "weather.vert",
        assetRoot / "shaders" / "weather.frag", m_graphicsApi);
    m_postShader = std::make_unique<Shader>(
        assetRoot / "shaders" / "post.vert",
        assetRoot / "shaders" / "post.frag", m_graphicsApi);
    m_blockAtlas.initialize(assetRoot);

    // Global GL state
    GL_CHECK(glEnable(GL_DEPTH_TEST));
    GL_CHECK(glEnable(GL_CULL_FACE));
    if (m_graphicsApi == GraphicsApi::OpenGL33) {
        GL_CHECK(glEnable(GL_MULTISAMPLE));
        if (m_framebufferSrgb) GL_CHECK(glEnable(GL_FRAMEBUFFER_SRGB));
    }
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
        Window::graphicsProcAddress("glDrawArraysInstanced"));
    m_vertexAttribDivisor = reinterpret_cast<VertexAttribDivisorFn>(
        Window::graphicsProcAddress("glVertexAttribDivisor"));
    m_vertexAttribIPointer = reinterpret_cast<VertexAttribIPointerFn>(
        Window::graphicsProcAddress("glVertexAttribIPointer"));
    m_bufferSubData = reinterpret_cast<BufferSubDataFn>(
        Window::graphicsProcAddress("glBufferSubData"));
    m_cloudInstances.reserve(MAX_CLOUD_INSTANCES);
    if (m_drawArraysInstanced && m_vertexAttribDivisor &&
        m_vertexAttribIPointer) {
        // Clouds share the static entity cube but provide position and size
        // per instance, reducing the entire layer to one draw call.
        GL_CHECK(glGenVertexArrays(1, &m_cloudVAO));
        GL_CHECK(glGenBuffers(
            static_cast<GLsizei>(CLOUD_INSTANCE_BUFFER_COUNT),
            m_cloudInstanceVBOs));
        GL_CHECK(glBindVertexArray(m_cloudVAO));
        GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, m_entityVBO));
        GL_CHECK(glVertexAttribPointer(
            0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr));
        GL_CHECK(glEnableVertexAttribArray(0));
        for (GLuint buffer : m_cloudInstanceVBOs) {
            GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, buffer));
            GL_CHECK(glBufferData(GL_ARRAY_BUFFER,
                MAX_CLOUD_INSTANCES * sizeof(CloudInstance), nullptr,
                GL_STREAM_DRAW));
        }
        GL_CHECK(glBindBuffer(
            GL_ARRAY_BUFFER,
            m_cloudInstanceVBOs[m_cloudInstanceBufferIndex]));
        GL_CHECK(glVertexAttribPointer(
            2, 4, GL_FLOAT, GL_FALSE, sizeof(CloudInstance), nullptr));
        GL_CHECK(glEnableVertexAttribArray(2));
        GL_CHECK(glVertexAttribPointer(
            3, 2, GL_FLOAT, GL_FALSE, sizeof(CloudInstance),
            reinterpret_cast<void*>(4 * sizeof(float))));
        GL_CHECK(glEnableVertexAttribArray(3));
        m_vertexAttribIPointer(
            4, 1, GL_UNSIGNED_INT, sizeof(CloudInstance),
            reinterpret_cast<void*>(offsetof(CloudInstance, visibleFaces)));
        GL_CHECK(glEnableVertexAttribArray(4));
        m_vertexAttribDivisor(2, 1);
        m_vertexAttribDivisor(3, 1);
        m_vertexAttribDivisor(4, 1);
        GL_CHECK(glBindVertexArray(0));
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
    stbi_uc* atlas=nullptr;
    try { const auto encoded=AssetStore::readPath(assetRoot/"textures"/"generated"/"entity_atlas.png");
        atlas=stbi_load_from_memory(encoded.data(),static_cast<int>(encoded.size()),&atlasWidth,&atlasHeight,&atlasChannels,4);
    } catch(const std::exception&) {}
    if (!atlas) {
        try { const auto encoded=AssetStore::readPath(assetRoot/"textures"/"entity_atlas.png");
            atlas=stbi_load_from_memory(encoded.data(),static_cast<int>(encoded.size()),&atlasWidth,&atlasHeight,&atlasChannels,4);
        } catch(const std::exception&) {}
        if (atlas) LOG_WARN("Using legacy entity portrait atlas fallback");
    }
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
    createSceneTarget(std::max(1, window.width()), std::max(1, window.height()));
}

// ── Frame management ──────────────────────────────────────────────────

void Renderer::beginFrame() {
    m_performanceStats = {};
    bindSceneTarget();
    m_sceneFinished = false;
    GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
}

void Renderer::setVisualQuality(VisualQuality quality) {
    if (quality == m_visualQuality) return;
    const int oldSamples = visualQualityConfig(m_visualQuality).sceneSamples;
    m_visualQuality = quality;
    if (m_window && visualQualityConfig(quality).sceneSamples != oldSamples)
        createSceneTarget(std::max(1, m_window->width()),
                          std::max(1, m_window->height()));
}

void Renderer::finishScene(const PostProcessState& state) {
    m_postProcessState = state;
    if (!m_sceneColorTexture || !m_postShader || m_sceneFinished) return;
    auto bindFramebuffer = glProc<BindFramebufferFn>("glBindFramebuffer");
    if (!bindFramebuffer) return;
    if (m_sceneSamples > 1) {
        if (auto blit = glProc<BlitFramebufferFn>("glBlitFramebuffer")) {
            bindFramebuffer(GL_READ_FRAMEBUFFER_VALUE, m_sceneFramebuffer);
            bindFramebuffer(GL_DRAW_FRAMEBUFFER_VALUE, m_sceneResolveFramebuffer);
            blit(0, 0, m_sceneWidth, m_sceneHeight,
                 0, 0, m_sceneWidth, m_sceneHeight,
                 GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }
    }
    bindFramebuffer(GL_FRAMEBUFFER_VALUE, 0);
    GL_CHECK(glViewport(0, 0, m_sceneWidth, m_sceneHeight));
    GL_CHECK(glDisable(GL_DEPTH_TEST));
    GL_CHECK(glDisable(GL_CULL_FACE));
    GL_CHECK(glDepthMask(GL_FALSE));
    m_postShader->bind();
    m_postShader->setInt("uSceneColor", 0);
    const VisualQualityConfig visual = visualQualityConfig(m_visualQuality);
    m_postShader->setVec4("uExposureBloom", {
        std::clamp(state.exposure, 0.75f, 1.65f),
        visual.bloomLevels > 0 ? 0.07f + visual.bloomLevels * 0.012f : 0.0f,
        visual.bloomLevels > 0 ? 1.15f + visual.bloomLevels * 0.18f : 1.0f,
        visual.bloomLevels <= 0 ? 0.0f :
            visual.bloomLevels <= 3 ? 4.0f :
            visual.bloomLevels <= 5 ? 8.0f : 12.0f});
    m_postShader->setVec4("uEffects", {
        std::clamp(state.underwater, 0.0f, 1.0f),
        std::clamp(state.hurt, 0.0f, 1.0f),
        m_framebufferSrgb ? 0.0f : 1.0f,
        static_cast<float>(static_cast<int>(m_visualQuality))});
    m_postShader->setVec4("uTexelTime", {
        1.0f / static_cast<float>(m_sceneWidth),
        1.0f / static_cast<float>(m_sceneHeight),
        static_cast<float>(RuntimeClock::seconds(RuntimeClock{}.now())), 0.0f});
    m_postShader->setVec4("uEnvironment", {
        state.environment.rainIntensity, state.environment.thunderIntensity,
        state.environment.lightningFlash, state.environment.daylight});
    GL_CHECK(glActiveTexture(GL_TEXTURE0));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_sceneColorTexture));
    GL_CHECK(glBindVertexArray(m_skyVAO));
    GL_CHECK(glDrawArrays(GL_TRIANGLES, 0, 3));
    GL_CHECK(glBindVertexArray(0));
    GL_CHECK(glDepthMask(GL_TRUE));
    GL_CHECK(glEnable(GL_CULL_FACE));
    GL_CHECK(glEnable(GL_DEPTH_TEST));
    ++m_performanceStats.drawCalls;
    m_sceneFinished = true;
}

void Renderer::resize(int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);
    if (m_sceneFramebuffer && width == m_sceneWidth && height == m_sceneHeight)
        return;
    createSceneTarget(width, height);
}

void Renderer::endFrame() {
    if (m_window) m_window->swapBuffers();
}

void Renderer::destroySceneTarget() {
    if (m_sceneColorTexture)
        GL_CHECK(glDeleteTextures(1, &m_sceneColorTexture));
    m_sceneColorTexture = 0;
    if (auto destroyRenderbuffers =
            glProc<DeleteRenderbuffersFn>("glDeleteRenderbuffers")) {
        if (m_sceneColorRenderbuffer)
            destroyRenderbuffers(1, &m_sceneColorRenderbuffer);
        if (m_sceneDepthRenderbuffer)
            destroyRenderbuffers(1, &m_sceneDepthRenderbuffer);
    }
    m_sceneColorRenderbuffer = 0;
    m_sceneDepthRenderbuffer = 0;
    if (auto destroyFramebuffers =
            glProc<DeleteFramebuffersFn>("glDeleteFramebuffers")) {
        if (m_sceneFramebuffer)
            destroyFramebuffers(1, &m_sceneFramebuffer);
        if (m_sceneResolveFramebuffer &&
            m_sceneResolveFramebuffer != m_sceneFramebuffer)
            destroyFramebuffers(1, &m_sceneResolveFramebuffer);
    }
    m_sceneFramebuffer = 0;
    m_sceneResolveFramebuffer = 0;
    m_sceneWidth = 0;
    m_sceneHeight = 0;
    m_sceneSamples = 1;
    m_sceneHdr = false;
}

void Renderer::createSceneTarget(int width, int height) {
    auto genFramebuffers = glProc<GenFramebuffersFn>("glGenFramebuffers");
    auto bindFramebuffer = glProc<BindFramebufferFn>("glBindFramebuffer");
    auto framebufferTexture =
        glProc<FramebufferTexture2DFn>("glFramebufferTexture2D");
    auto checkFramebuffer =
        glProc<CheckFramebufferStatusFn>("glCheckFramebufferStatus");
    auto genRenderbuffers =
        glProc<GenRenderbuffersFn>("glGenRenderbuffers");
    auto bindRenderbuffer =
        glProc<BindRenderbufferFn>("glBindRenderbuffer");
    auto renderbufferStorage =
        glProc<RenderbufferStorageFn>("glRenderbufferStorage");
    auto framebufferRenderbuffer =
        glProc<FramebufferRenderbufferFn>("glFramebufferRenderbuffer");
    if (!genFramebuffers || !bindFramebuffer || !framebufferTexture ||
        !checkFramebuffer || !genRenderbuffers || !bindRenderbuffer ||
        !renderbufferStorage || !framebufferRenderbuffer) {
        LOG_WARN("OpenGL scene composition unavailable: framebuffer API missing");
        destroySceneTarget();
        return;
    }

    destroySceneTarget();
    m_sceneWidth = std::max(1, width);
    m_sceneHeight = std::max(1, height);
    genFramebuffers(1, &m_sceneResolveFramebuffer);
    bindFramebuffer(GL_FRAMEBUFFER_VALUE, m_sceneResolveFramebuffer);
    GL_CHECK(glGenTextures(1, &m_sceneColorTexture));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_sceneColorTexture));
    const auto allocateColor = [&](unsigned int internalFormat,
                                   unsigned int type) {
        GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, internalFormat,
            m_sceneWidth, m_sceneHeight, 0, GL_RGBA, type, nullptr));
        framebufferTexture(GL_FRAMEBUFFER_VALUE, GL_COLOR_ATTACHMENT0_VALUE,
                           GL_TEXTURE_2D, m_sceneColorTexture, 0);
        return checkFramebuffer(GL_FRAMEBUFFER_VALUE) ==
               GL_FRAMEBUFFER_COMPLETE_VALUE;
    };
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_VALUE));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR_VALUE));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                             GL_CLAMP_TO_EDGE_VALUE));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                             GL_CLAMP_TO_EDGE_VALUE));
    m_sceneHdr = allocateColor(GL_RGBA16F_VALUE, GL_HALF_FLOAT_VALUE);
    if (!m_sceneHdr) {
        if (!allocateColor(GL_RGBA8_VALUE, GL_UNSIGNED_BYTE)) {
            bindFramebuffer(GL_FRAMEBUFFER_VALUE, 0);
            destroySceneTarget();
            throw std::runtime_error("OpenGL scene framebuffer is incomplete");
        }
        LOG_WARN("OpenGL half-float color target unavailable; using RGBA8");
    }

    int maximumSamples = 1;
    GL_CHECK(glGetIntegerv(GL_MAX_SAMPLES_VALUE, &maximumSamples));
    const int requestedSamples = visualQualityConfig(m_visualQuality).sceneSamples;
    m_sceneSamples = requestedSamples >= 4 && maximumSamples >= 4 ? 4 :
                     requestedSamples >= 2 && maximumSamples >= 2 ? 2 : 1;
    auto multisampleStorage = glProc<RenderbufferStorageMultisampleFn>(
        "glRenderbufferStorageMultisample");
    auto blit = glProc<BlitFramebufferFn>("glBlitFramebuffer");
    if (!multisampleStorage || !blit) m_sceneSamples = 1;

    if (m_sceneSamples > 1) {
        genFramebuffers(1, &m_sceneFramebuffer);
        bindFramebuffer(GL_FRAMEBUFFER_VALUE, m_sceneFramebuffer);
        genRenderbuffers(1, &m_sceneColorRenderbuffer);
        bindRenderbuffer(GL_RENDERBUFFER_VALUE, m_sceneColorRenderbuffer);
        multisampleStorage(GL_RENDERBUFFER_VALUE, m_sceneSamples,
            m_sceneHdr ? GL_RGBA16F_VALUE : GL_RGBA8_VALUE,
            m_sceneWidth, m_sceneHeight);
        framebufferRenderbuffer(GL_FRAMEBUFFER_VALUE, GL_COLOR_ATTACHMENT0_VALUE,
            GL_RENDERBUFFER_VALUE, m_sceneColorRenderbuffer);
    } else {
        m_sceneFramebuffer = m_sceneResolveFramebuffer;
    }
    genRenderbuffers(1, &m_sceneDepthRenderbuffer);
    bindRenderbuffer(GL_RENDERBUFFER_VALUE, m_sceneDepthRenderbuffer);
    if (m_sceneSamples > 1)
        multisampleStorage(GL_RENDERBUFFER_VALUE, m_sceneSamples,
            GL_DEPTH_COMPONENT24_VALUE, m_sceneWidth, m_sceneHeight);
    else
        renderbufferStorage(GL_RENDERBUFFER_VALUE, GL_DEPTH_COMPONENT24_VALUE,
            m_sceneWidth, m_sceneHeight);
    framebufferRenderbuffer(GL_FRAMEBUFFER_VALUE, GL_DEPTH_ATTACHMENT_VALUE,
        GL_RENDERBUFFER_VALUE, m_sceneDepthRenderbuffer);
    if (checkFramebuffer(GL_FRAMEBUFFER_VALUE) != GL_FRAMEBUFFER_COMPLETE_VALUE &&
        m_sceneSamples > 1) {
        // GLES implementations commonly expose a renderable half-float texture
        // without supporting the same format as a multisample renderbuffer.
        // Preserve HDR and fall back to one sample instead of failing startup.
        if (auto destroyRenderbuffers =
                glProc<DeleteRenderbuffersFn>("glDeleteRenderbuffers")) {
            destroyRenderbuffers(1, &m_sceneColorRenderbuffer);
            destroyRenderbuffers(1, &m_sceneDepthRenderbuffer);
        }
        if (auto destroyFramebuffers =
                glProc<DeleteFramebuffersFn>("glDeleteFramebuffers"))
            destroyFramebuffers(1, &m_sceneFramebuffer);
        m_sceneColorRenderbuffer = 0;
        m_sceneDepthRenderbuffer = 0;
        m_sceneFramebuffer = m_sceneResolveFramebuffer;
        m_sceneSamples = 1;
        bindFramebuffer(GL_FRAMEBUFFER_VALUE, m_sceneFramebuffer);
        genRenderbuffers(1, &m_sceneDepthRenderbuffer);
        bindRenderbuffer(GL_RENDERBUFFER_VALUE, m_sceneDepthRenderbuffer);
        renderbufferStorage(GL_RENDERBUFFER_VALUE, GL_DEPTH_COMPONENT24_VALUE,
            m_sceneWidth, m_sceneHeight);
        framebufferRenderbuffer(GL_FRAMEBUFFER_VALUE, GL_DEPTH_ATTACHMENT_VALUE,
            GL_RENDERBUFFER_VALUE, m_sceneDepthRenderbuffer);
        LOG_WARN("OpenGL multisample HDR target unavailable; using 1x HDR");
    }
    if (checkFramebuffer(GL_FRAMEBUFFER_VALUE) != GL_FRAMEBUFFER_COMPLETE_VALUE) {
        bindFramebuffer(GL_FRAMEBUFFER_VALUE, 0);
        destroySceneTarget();
        throw std::runtime_error("OpenGL scene framebuffer is incomplete");
    }
    bindRenderbuffer(GL_RENDERBUFFER_VALUE, 0);
    bindFramebuffer(GL_FRAMEBUFFER_VALUE, m_sceneFramebuffer);
    GL_CHECK(glViewport(0, 0, m_sceneWidth, m_sceneHeight));
    LOG_INFO("OpenGL scene target: " << (m_sceneHdr ? "RGBA16F" : "RGBA8")
             << ", " << m_sceneSamples << "x MSAA");
}

void Renderer::bindSceneTarget() {
    if (auto bindFramebuffer = glProc<BindFramebufferFn>("glBindFramebuffer"))
        bindFramebuffer(GL_FRAMEBUFFER_VALUE, m_sceneFramebuffer);
    GL_CHECK(glViewport(0, 0, std::max(1, m_sceneWidth),
                        std::max(1, m_sceneHeight)));
}

RenderDeviceCapabilities Renderer::capabilities() const {
    return {true, true, true};
}

RenderMeshHandle Renderer::createMesh(const MeshData& data) {
    validateMeshData(data);
    BasicMesh mesh;
    GL_CHECK(glGenVertexArrays(1, &mesh.vao));
    GL_CHECK(glGenBuffers(1, &mesh.vbo));
    GL_CHECK(glGenBuffers(1, &mesh.ebo));
    GL_CHECK(glBindVertexArray(mesh.vao));
    GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo));
    const bool chunkLayout = data.layout == MeshVertexLayout::Chunk;
    GL_CHECK(glBufferData(GL_ARRAY_BUFFER,
                          chunkLayout ? data.chunkVertices.size() * sizeof(MeshVertex)
                                      : data.vertices.size() * sizeof(BasicMeshVertex),
                          chunkLayout ? static_cast<const void*>(data.chunkVertices.data())
                                      : static_cast<const void*>(data.vertices.data()),
                          GL_STATIC_DRAW));
    GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo));
    GL_CHECK(glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                          data.indices.size() * sizeof(uint32_t),
                          data.indices.data(), GL_STATIC_DRAW));
    if (chunkLayout) {
        GL_CHECK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
            reinterpret_cast<void*>(offsetof(MeshVertex, px))));
        GL_CHECK(glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
            reinterpret_cast<void*>(offsetof(MeshVertex, ao))));
        GL_CHECK(glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
            reinterpret_cast<void*>(offsetof(MeshVertex, u))));
        GL_CHECK(glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
            reinterpret_cast<void*>(offsetof(MeshVertex, face))));
        for (GLuint attribute = 0; attribute < 4; ++attribute)
            GL_CHECK(glEnableVertexAttribArray(attribute));
    } else {
        GL_CHECK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BasicMeshVertex),
            reinterpret_cast<void*>(offsetof(BasicMeshVertex, position))));
        GL_CHECK(glEnableVertexAttribArray(0));
        GL_CHECK(glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(BasicMeshVertex),
            reinterpret_cast<void*>(offsetof(BasicMeshVertex, uv))));
        GL_CHECK(glEnableVertexAttribArray(1));
    }
    GL_CHECK(glBindVertexArray(0));
    mesh.indexCount = data.indices.size();
    mesh.layout = data.layout;
    const RenderMeshHandle handle{m_nextBasicMeshHandle++};
    m_basicMeshes.emplace(handle.value, mesh);
    return handle;
}

void Renderer::destroyMesh(RenderMeshHandle handle) {
    const auto it = m_basicMeshes.find(handle.value);
    if (it == m_basicMeshes.end())
        throw std::invalid_argument("Unknown OpenGL mesh handle");
    BasicMesh& mesh = it->second;
    GL_CHECK(glDeleteBuffers(1, &mesh.vbo));
    GL_CHECK(glDeleteBuffers(1, &mesh.ebo));
    GL_CHECK(glDeleteVertexArrays(1, &mesh.vao));
    m_basicMeshes.erase(it);
}

RenderTextureHandle Renderer::createTexture(
    const TextureData& data, const TextureSamplerDesc& sampler) {
    validateTextureData(data);
    GLuint texture = 0;
    GL_CHECK(glGenTextures(1, &texture));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, texture));
    constexpr GLint LINEAR_FILTER = 0x2601;
    constexpr GLint REPEAT_WRAP = 0x2901;
    constexpr GLint NEAREST_MIPMAP_LINEAR_FILTER = 0x2702;
    const GLint minFilter = sampler.minFilter == TextureFilter::Nearest
        ? GL_NEAREST : sampler.minFilter == TextureFilter::NearestMipmapLinear
        ? NEAREST_MIPMAP_LINEAR_FILTER : LINEAR_FILTER;
    const GLint magFilter = sampler.magFilter == TextureFilter::Nearest
        ? GL_NEAREST : LINEAR_FILTER;
    const GLint wrapU = sampler.addressU == TextureAddressMode::Repeat
        ? REPEAT_WRAP : GL_CLAMP_TO_EDGE;
    const GLint wrapV = sampler.addressV == TextureAddressMode::Repeat
        ? REPEAT_WRAP : GL_CLAMP_TO_EDGE;
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapU));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapV));
    constexpr GLint RGBA8_UNORM = 0x8058;
    const GLint internalFormat = data.format == TextureFormat::Rgba8Srgb
        ? GL_SRGB8_ALPHA8 : RGBA8_UNORM;
    GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, internalFormat,
                          static_cast<GLsizei>(data.width),
                          static_cast<GLsizei>(data.height), 0,
                          GL_RGBA, GL_UNSIGNED_BYTE, data.pixels.data()));
    for (size_t level = 0; level < data.mipLevels.size(); ++level) {
        const auto& mip = data.mipLevels[level];
        GL_CHECK(glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level + 1),
            internalFormat, static_cast<GLsizei>(mip.width),
            static_cast<GLsizei>(mip.height), 0, GL_RGBA, GL_UNSIGNED_BYTE,
            mip.pixels.data()));
    }
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,
                            static_cast<GLint>(data.mipLevels.size())));
    const RenderTextureHandle handle{m_nextBasicTextureHandle++};
    m_basicTextures.emplace(handle.value, BasicTexture{texture});
    return handle;
}

void Renderer::destroyTexture(RenderTextureHandle handle) {
    for (const auto& [id, material] : m_basicMaterials) {
        (void)id;
        if (material.desc.baseColorTexture == handle)
            throw std::logic_error("Texture is still referenced by a material");
    }
    const auto it = m_basicTextures.find(handle.value);
    if (it == m_basicTextures.end())
        throw std::invalid_argument("Unknown OpenGL texture handle");
    GL_CHECK(glDeleteTextures(1, &it->second.texture));
    m_basicTextures.erase(it);
}

RenderMaterialHandle Renderer::createMaterial(const MaterialDesc& desc) {
    if (m_basicTextures.find(desc.baseColorTexture.value) == m_basicTextures.end() &&
        !(desc.baseColorTexture == m_blockAtlas.texture()))
        throw std::invalid_argument("Invalid OpenGL textured material");
    if (desc.pipeline != MaterialPipeline::UnlitTextured &&
        (!desc.depthTest || !desc.backfaceCull))
        throw std::invalid_argument("Chunk/UI material requires depth test and culling");
    const RenderMaterialHandle handle{m_nextBasicMaterialHandle++};
    m_basicMaterials.emplace(handle.value, BasicMaterial{desc});
    return handle;
}

void Renderer::destroyMaterial(RenderMaterialHandle handle) {
    if (m_basicMaterials.erase(handle.value) == 0)
        throw std::invalid_argument("Unknown OpenGL material handle");
}

void Renderer::beginFrame(const FrameData& frame) {
    m_performanceStats = {};
    m_basicFrame = frame;
    bindSceneTarget();
    m_sceneFinished = false;
    GL_CHECK(glEnable(GL_DEPTH_TEST));
    GL_CHECK(glEnable(GL_CULL_FACE));
    GL_CHECK(glCullFace(GL_BACK));
    GL_CHECK(glClearColor(frame.clearColor.r, frame.clearColor.g,
                          frame.clearColor.b, frame.clearColor.a));
    GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
}

void Renderer::draw(const DrawCommand& command) {
    const auto mesh = m_basicMeshes.find(command.mesh.value);
    const auto material = m_basicMaterials.find(command.material.value);
    if (mesh == m_basicMeshes.end() || material == m_basicMaterials.end())
        throw std::invalid_argument("Draw command contains an unknown handle");
    if (!isMeshMaterialCompatible(mesh->second.layout,
                                  material->second.desc.pipeline))
        throw std::invalid_argument("OpenGL mesh/material layout mismatch");
    const auto texture = m_basicTextures.find(material->second.desc.baseColorTexture.value);
    const uint32_t textureName = texture != m_basicTextures.end()
        ? texture->second.texture
        : (material->second.desc.baseColorTexture == m_blockAtlas.texture()
            ? m_blockAtlas.texture().value : 0u);
    if (!textureName)
        throw std::logic_error("Material texture no longer exists");
    ++m_performanceStats.drawCalls;
    if (material->second.desc.pipeline == MaterialPipeline::UnlitTextured && !m_basicShader) {
        m_basicShader = std::make_unique<Shader>(
            m_assetRoot / "shaders" / "basic_textured.vert",
            m_assetRoot / "shaders" / "basic_textured.frag", m_graphicsApi);
    }
    Shader* shader = nullptr;
    const bool chunkPipeline =
        material->second.desc.pipeline == MaterialPipeline::ChunkOpaqueCutout ||
        material->second.desc.pipeline == MaterialPipeline::ChunkTranslucent;
    const bool translucent =
        material->second.desc.pipeline == MaterialPipeline::ChunkTranslucent;
    GLboolean oldDepth = GL_TRUE, oldCull = GL_TRUE;
    const bool unlit = material->second.desc.pipeline ==
                       MaterialPipeline::UnlitTextured;
    if (unlit) {
        GL_CHECK(glGetBooleanv(GL_DEPTH_TEST, &oldDepth));
        GL_CHECK(glGetBooleanv(GL_CULL_FACE, &oldCull));
        if (material->second.desc.depthTest) GL_CHECK(glEnable(GL_DEPTH_TEST));
        else GL_CHECK(glDisable(GL_DEPTH_TEST));
        if (material->second.desc.backfaceCull) GL_CHECK(glEnable(GL_CULL_FACE));
        else GL_CHECK(glDisable(GL_CULL_FACE));
    }
    if (chunkPipeline) {
        if (!m_blockShader) throw std::logic_error("Chunk shader is unavailable");
        shader = m_blockShader.get();
        shader->bind();
        shader->setMat4("uMVP", (command.useCustomViewProjection
            ? command.viewProjection : m_basicFrame.projection * m_basicFrame.view) * command.model);
        shader->setVec3("uChunkOrigin", glm::vec3(0.0f));
        shader->setVec3("uCameraPosition", glm::vec3(glm::inverse(m_basicFrame.view)[3]));
        shader->setVec3("uLightDirection", m_basicFrame.lightDirection);
        shader->setVec3("uDirectColor", m_basicFrame.directColor);
        shader->setVec3("uAmbientColor", m_basicFrame.ambientColor);
        shader->setVec3("uFogColor", glm::vec3(m_basicFrame.clearColor));
        shader->setFloat("uDirectIntensity", 1.0f);
        shader->setFloat("uAmbientIntensity", 1.0f);
        shader->setFloat("uFogEnd", 10000.0f);
        shader->setFloat("uFogStartFraction", 1.0f);
        shader->setInt("uManualGamma", 0);
        shader->setInt("uSmoothLighting", material->second.desc.smoothLighting ? 1 : 0);
        shader->setFloat("uAtlasTiles", static_cast<float>(material->second.desc.atlasTilesPerSide));
        shader->setFloat("uLavaTile", static_cast<float>(getAtlasTextureIndex(BlockTexture::Lava)));
        shader->setFloat("uWaterTile", static_cast<float>(getAtlasTextureIndex(BlockTexture::Water)));
        shader->setFloat("uTime", static_cast<float>(
            RuntimeClock::seconds(RuntimeClock{}.now())));
        shader->setFloat("uRainIntensity", m_environment.rainIntensity);
        shader->setFloat("uThunderIntensity", m_environment.thunderIntensity);
        const VisualQualityConfig visual = visualQualityConfig(m_visualQuality);
        shader->setFloat("uCloudShadowStrength", visual.cloudShadowSamples > 0
            ? 0.12f + 0.035f * visual.cloudShadowSamples : 0.0f);
        shader->setFloat("uNormalStrength", visual.normalStrength);
        shader->setFloat("uAoStrength", visual.aoDirections > 0
            ? std::min(1.0f, 0.58f + visual.aoDirections * 0.055f) : 0.0f);
        shader->setInt("uBlockAtlas", 0);
        shader->setInt("uNormalAtlas", 2);
        shader->setInt("uPropertyAtlas", 3);
        if (material->second.desc.baseColorTexture == m_blockAtlas.texture())
            m_blockAtlas.bindMaterialMaps();
        shader->setVec4("uTint", command.tint);
    } else {
        shader = m_basicShader.get();
        shader->bind();
        shader->setMat4("uMVP", (command.useCustomViewProjection
            ? command.viewProjection : m_basicFrame.projection * m_basicFrame.view) * command.model);
        shader->setInt("uTexture", 0);
        shader->setVec4("uTint", command.tint);
    }
    GL_CHECK(glActiveTexture(GL_TEXTURE0));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, textureName));
    GL_CHECK(glBindVertexArray(mesh->second.vao));
    if (translucent) {
        GL_CHECK(glEnable(GL_BLEND));
        GL_CHECK(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
        GL_CHECK(glDepthMask(GL_FALSE));
    }
    const size_t count = command.indexCount ? command.indexCount : mesh->second.indexCount;
    if (static_cast<uint64_t>(command.firstIndex) + count > mesh->second.indexCount)
        throw std::invalid_argument("OpenGL draw range exceeds mesh indices");
    GL_CHECK(glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_INT,
        reinterpret_cast<void*>(static_cast<uintptr_t>(command.firstIndex) * sizeof(uint32_t))));
    if (translucent) {
        GL_CHECK(glDepthMask(GL_TRUE));
        GL_CHECK(glDisable(GL_BLEND));
    }
    if (unlit) {
        if (oldDepth) GL_CHECK(glEnable(GL_DEPTH_TEST));
        else GL_CHECK(glDisable(GL_DEPTH_TEST));
        if (oldCull) GL_CHECK(glEnable(GL_CULL_FACE));
        else GL_CHECK(glDisable(GL_CULL_FACE));
    }
    GL_CHECK(glBindVertexArray(0));
}

void Renderer::waitIdle() {
    // OpenGL resource deletion is ordered after previously issued commands in
    // the current context; explicit synchronization is unnecessary here.
}

void Renderer::setEnvironment(const RenderEnvironment& environment,
                              const glm::vec3& cameraPosition) {
    m_environment = environment;
    m_cameraPosition = cameraPosition;
}

void Renderer::renderSky(const RenderEnvironment& environment,
                         const glm::mat4& inverseViewProjection,
                         const glm::vec3& cameraPosition, bool renderClouds) {
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
    m_skyShader->setFloat("uWeatherTime", static_cast<float>(RuntimeClock::seconds(RuntimeClock{}.now())));
    const VisualQualityConfig visual = visualQualityConfig(m_visualQuality);
    m_skyShader->setInt("uRenderClouds",
                        renderClouds && visual.voxelClouds ? 1 : 0);
    m_skyShader->setInt("uRenderCirrus", visual.cirrusClouds ? 1 : 0);
    m_skyShader->setInt("uManualGamma", 0);
    GL_CHECK(glBindVertexArray(m_skyVAO));
    GL_CHECK(glDrawArrays(GL_TRIANGLES, 0, 3));
    GL_CHECK(glBindVertexArray(0));

    GL_CHECK(glDepthMask(GL_TRUE));
    GL_CHECK(glEnable(GL_CULL_FACE));
    GL_CHECK(glEnable(GL_DEPTH_TEST));
}

// ── Chunk rendering ───────────────────────────────────────────────────

void Renderer::uploadChunkMesh(ChunkMesh& mesh) {
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        releaseChunkMesh(mesh);
        mesh.indexCount = 0;
        return;
    }
    static_assert(sizeof(MeshVertex) == 44,
                  "MeshVertex must be 11 tightly-packed floats");
    static_assert(offsetof(MeshVertex, px) == 0, "px at offset 0");
    static_assert(offsetof(MeshVertex, ao) == 12, "ao at offset 12");

    if (!mesh.renderHandle) {
        if (m_nextChunkMeshHandle == 0)
            throw std::runtime_error("Chunk mesh handle space exhausted");
        mesh.renderHandle.value = m_nextChunkMeshHandle++;
    }
    GpuChunkMesh& gpu = m_chunkMeshes[mesh.renderHandle.value];
    const bool initializeLayout = gpu.vao == 0;
    if (initializeLayout) {
        GL_CHECK(glGenVertexArrays(1, &gpu.vao));
        GL_CHECK(glGenBuffers(1, &gpu.vbo));
        GL_CHECK(glGenBuffers(1, &gpu.ebo));
    }
    GL_CHECK(glBindVertexArray(gpu.vao));
    GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo));
    GL_CHECK(glBufferData(GL_ARRAY_BUFFER,
        mesh.vertices.size() * sizeof(MeshVertex), mesh.vertices.data(),
        GL_STATIC_DRAW));
    if (initializeLayout) {
        GL_CHECK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
            sizeof(MeshVertex), reinterpret_cast<void*>(0)));
        GL_CHECK(glEnableVertexAttribArray(0));
        GL_CHECK(glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE,
            sizeof(MeshVertex), reinterpret_cast<void*>(12)));
        GL_CHECK(glEnableVertexAttribArray(1));
        GL_CHECK(glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE,
            sizeof(MeshVertex), reinterpret_cast<void*>(28)));
        GL_CHECK(glEnableVertexAttribArray(2));
        GL_CHECK(glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE,
            sizeof(MeshVertex), reinterpret_cast<void*>(40)));
        GL_CHECK(glEnableVertexAttribArray(3));
    }
    GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ebo));
    GL_CHECK(glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        mesh.indices.size() * sizeof(unsigned int), mesh.indices.data(),
        GL_STATIC_DRAW));
    GL_CHECK(glBindVertexArray(0));
    mesh.indexCount = mesh.indices.size();
    mesh.gpuReady = true;
}

void Renderer::releaseChunkMesh(ChunkMesh& mesh) {
    if (mesh.renderHandle) {
        const auto found = m_chunkMeshes.find(mesh.renderHandle.value);
        if (found != m_chunkMeshes.end()) {
            GpuChunkMesh& gpu = found->second;
            if (gpu.vbo) GL_CHECK(glDeleteBuffers(1, &gpu.vbo));
            if (gpu.ebo) GL_CHECK(glDeleteBuffers(1, &gpu.ebo));
            if (gpu.vao) GL_CHECK(glDeleteVertexArrays(1, &gpu.vao));
            m_chunkMeshes.erase(found);
        }
    }
    mesh.abandonGpuResources();
}

void Renderer::renderChunk(const ChunkMesh& mesh, const glm::mat4& modelMatrix,
                           const glm::mat4& viewProjection, bool translucent) {
    if (!mesh.gpuReady || mesh.indexCount == 0) return;
    size_t count = translucent ? mesh.translucentIndexCount : mesh.opaqueIndexCount;
    size_t offset = translucent ? mesh.translucentIndexOffset : 0;
    if (count == 0) return;
    ++m_performanceStats.drawCalls;

    glm::mat4 mvp = viewProjection * modelMatrix;

    // Shader is expected to already be bound (caller binds once per frame)
    m_blockShader->setMat4("uMVP", mvp);
    m_blockShader->setVec3("uChunkOrigin", glm::vec3(modelMatrix[3]));
    const auto found = m_chunkMeshes.find(mesh.renderHandle.value);
    if (found == m_chunkMeshes.end()) return;
    GL_CHECK(glBindVertexArray(found->second.vao));
    GL_CHECK(glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(count),
                   GL_UNSIGNED_INT,
                   reinterpret_cast<void*>(offset * sizeof(unsigned int))));
    // VAO stays bound — next draw will bind its own
}

void Renderer::renderChunkShadows(ShadowQuality quality,
                                  const glm::mat4& inverseViewProjection,
                                  const glm::mat4& view,
                                  const glm::dvec3& worldOrigin,
                                  const std::vector<ShadowChunkSubmission>& chunks) {
    const bool enabled = quality != ShadowQuality::Off &&
        m_environment.daylight >= 0.12f && m_environment.directIntensity >= 0.08f;
    if (!enabled || chunks.empty()) {
        m_shadowCascades = {};
        m_shadowBaseCascades = {};
        return;
    }
    const ShadowConfig config = shadowConfig(quality);
    auto bindFramebuffer = glProc<BindFramebufferFn>("glBindFramebuffer");
    auto framebufferTexture = glProc<FramebufferTexture2DFn>("glFramebufferTexture2D");
    auto checkFramebuffer = glProc<CheckFramebufferStatusFn>("glCheckFramebufferStatus");
    auto genFramebuffers = glProc<GenFramebuffersFn>("glGenFramebuffers");
    if (!bindFramebuffer || !framebufferTexture || !checkFramebuffer || !genFramebuffers) {
        m_shadowCascades = {};
        return;
    }
    const bool qualityChanged = quality != m_shadowQuality;
    if (qualityChanged) {
        if (m_shadowTexture) GL_CHECK(glDeleteTextures(1, &m_shadowTexture));
        m_shadowTexture = 0;
        if (!m_shadowFramebuffer) genFramebuffers(1, &m_shadowFramebuffer);
        const int columns = config.cascadeCount == 1 ? 1 : 2;
        const int rows = (config.cascadeCount + columns - 1) / columns;
        GL_CHECK(glGenTextures(1, &m_shadowTexture));
        GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_shadowTexture));
        GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24_VALUE,
            config.resolution * columns, config.resolution * rows, 0,
            GL_DEPTH_COMPONENT_VALUE, GL_UNSIGNED_INT, nullptr));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_VALUE));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST_VALUE));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE_VALUE));
        GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE_VALUE));
        bindFramebuffer(GL_FRAMEBUFFER_VALUE, m_shadowFramebuffer);
        framebufferTexture(GL_FRAMEBUFFER_VALUE, GL_DEPTH_ATTACHMENT_VALUE,
                           GL_TEXTURE_2D, m_shadowTexture, 0);
        if (auto drawBuffer = glProc<DrawBufferFn>("glDrawBuffer")) drawBuffer(GL_NONE_VALUE);
        if (auto readBuffer = glProc<ReadBufferFn>("glReadBuffer")) readBuffer(GL_NONE_VALUE);
        if (checkFramebuffer(GL_FRAMEBUFFER_VALUE) != GL_FRAMEBUFFER_COMPLETE_VALUE)
            throw std::runtime_error("OpenGL shadow framebuffer is incomplete");
        bindFramebuffer(GL_FRAMEBUFFER_VALUE, 0);
        m_shadowQuality = quality;
    }
    const double nowSeconds = RuntimeClock::seconds(RuntimeClock{}.now());
    const bool moved = glm::distance(worldOrigin, m_lastShadowWorldOrigin) >=
        shadowMovementThreshold(quality);
    const float lightDelta = glm::length(glm::normalize(m_environment.lightDirection) -
        glm::normalize(m_lastShadowDirection));
    const bool timeDue = nowSeconds - m_lastShadowUpdateSeconds >=
        (1.0 / shadowUpdateHz(quality));
    const bool updateShadow = qualityChanged || m_lastShadowUpdateSeconds < 0.0 ||
        moved || lightDelta >= 0.01f || (timeDue && lightDelta >= 0.0002f);
    if (!updateShadow) {
        m_shadowCascades = m_shadowBaseCascades;
        const glm::dvec3 delta = worldOrigin - m_lastShadowWorldOrigin;
        const glm::mat4 translation = glm::translate(glm::mat4(1.0f), glm::vec3(
            static_cast<float>(delta.x), 0.0f, static_cast<float>(delta.z)));
        for (int i = 0; i < m_shadowCascades.count; ++i)
            m_shadowCascades.lightViewProjection[i] *= translation;
        return;
    }
    const float fogDistance = (static_cast<float>(Config::RENDER_DISTANCE) + 0.5f) *
                              Config::CHUNK_SIZE_X;
    m_shadowCascades = buildShadowCascades(quality, inverseViewProjection, view,
        m_environment.lightDirection, Config::NEAR_PLANE, fogDistance);
    m_shadowBaseCascades = m_shadowCascades;
    m_lastShadowUpdateSeconds = nowSeconds;
    m_lastShadowWorldOrigin = worldOrigin;
    m_lastShadowDirection = m_environment.lightDirection;
    bindFramebuffer(GL_FRAMEBUFFER_VALUE, m_shadowFramebuffer);
    auto colorMask = glProc<ColorMaskFn>("glColorMask");
    if (!colorMask) {
        m_shadowCascades = {};
        bindFramebuffer(GL_FRAMEBUFFER_VALUE, m_sceneFramebuffer);
        return;
    }
    colorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    GL_CHECK(glEnable(GL_DEPTH_TEST));
    GL_CHECK(glDepthMask(GL_TRUE));
    GL_CHECK(glEnable(GL_CULL_FACE));
    GL_CHECK(glEnable(GL_POLYGON_OFFSET_FILL));
    GL_CHECK(glPolygonOffset(2.0f, 4.0f));
    GL_CHECK(glClear(GL_DEPTH_BUFFER_BIT));
    m_shadowShader->bind();
    m_shadowShader->setInt("uBlockAtlas", 0);
    m_shadowShader->setFloat("uAtlasTiles",
        static_cast<float>(BlockTextureAtlas::tilesPerSide()));
    m_blockAtlas.bind();
    for (int cascade = 0; cascade < m_shadowCascades.count; ++cascade) {
        const int columns = m_shadowCascades.count == 1 ? 1 : 2;
        GL_CHECK(glViewport((cascade % columns) * config.resolution,
                            (cascade / columns) * config.resolution,
                            config.resolution, config.resolution));
        for (const ShadowChunkSubmission& submission : chunks) {
            if (!submission.mesh || !submission.mesh->gpuReady ||
                submission.mesh->shadowCasterIndexCount == 0) continue;
            if (!shadowIntersectsAabb(m_shadowCascades.lightViewProjection[cascade],
                                      submission.aabbMin, submission.aabbMax, false)) continue;
            const auto found = m_chunkMeshes.find(submission.mesh->renderHandle.value);
            if (found == m_chunkMeshes.end()) continue;
            m_shadowShader->setMat4("uLightMVP",
                m_shadowCascades.lightViewProjection[cascade] * submission.model);
            GL_CHECK(glBindVertexArray(found->second.vao));
            GL_CHECK(glDrawElements(GL_TRIANGLES,
                static_cast<GLsizei>(submission.mesh->shadowCasterIndexCount), GL_UNSIGNED_INT,
                reinterpret_cast<void*>(submission.mesh->shadowCasterIndexOffset *
                                        sizeof(unsigned int))));
            ++m_performanceStats.drawCalls;
        }
    }
    GL_CHECK(glDisable(GL_POLYGON_OFFSET_FILL));
    colorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    bindFramebuffer(GL_FRAMEBUFFER_VALUE, m_sceneFramebuffer);
    GL_CHECK(glViewport(0, 0, std::max(1, m_window->width()), std::max(1, m_window->height())));
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
    m_blockAtlas.bindMaterialMaps();
    m_blockShader->setInt("uBlockAtlas", 0);
    m_blockShader->setInt("uNormalAtlas", 2);
    m_blockShader->setInt("uPropertyAtlas", 3);
    m_blockShader->setInt("uShadowMap", 1);
    m_blockShader->setInt("uShadowCascadeCount", m_shadowCascades.count);
    m_blockShader->setFloat("uShadowResolution",
        static_cast<float>(std::max(1, m_shadowCascades.resolution)));
    m_blockShader->setInt("uShadowAtlasColumns",
        m_shadowCascades.count <= 1 ? 1 : 2);
    m_blockShader->setVec4("uShadowSplits", m_shadowCascades.splits);
    m_blockShader->setMat4Array("uShadowMatrices",
        m_shadowCascades.lightViewProjection.data(), 4);
    GL_CHECK(glActiveTexture(GL_TEXTURE1_VALUE));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_shadowTexture));
    GL_CHECK(glActiveTexture(GL_TEXTURE0));
    m_blockShader->setVec4("uTint", glm::vec4(1.0f));
    m_blockShader->setFloat("uAtlasTiles",
                            static_cast<float>(BlockTextureAtlas::tilesPerSide()));
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
    m_blockShader->setFloat("uTime", static_cast<float>(
        RuntimeClock::seconds(RuntimeClock{}.now())));
    m_blockShader->setFloat("uRainIntensity", m_environment.rainIntensity);
    m_blockShader->setFloat("uThunderIntensity", m_environment.thunderIntensity);
    const VisualQualityConfig visual = visualQualityConfig(m_visualQuality);
    m_blockShader->setFloat("uCloudShadowStrength", visual.cloudShadowSamples > 0
        ? 0.12f + 0.035f * visual.cloudShadowSamples : 0.0f);
    m_blockShader->setFloat("uNormalStrength", visual.normalStrength);
    m_blockShader->setFloat("uAoStrength", visual.aoDirections > 0
        ? std::min(1.0f, 0.58f + visual.aoDirections * 0.055f) : 0.0f);
    m_blockShader->setInt("uManualGamma", 0);
    m_blockShader->setInt("uSmoothLighting", Config::SMOOTH_LIGHTING ? 1 : 0);
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

    if (m_graphicsApi == GraphicsApi::OpenGL33) {
        GL_CHECK(glPolygonOffset(-1.0f, -1.0f));
        GL_CHECK(glEnable(GL_POLYGON_OFFSET_LINE));
    } else {
        model = glm::translate(model, glm::vec3(0.5f));
        model = glm::scale(model, glm::vec3(1.003f));
        model = glm::translate(model, glm::vec3(-0.5f));
        mvp = viewProjection * model;
    }

    m_wireShader->bind();
    m_wireShader->setMat4("uMVP", mvp);
    GL_CHECK(glBindVertexArray(m_wireVAO));
    GL_CHECK(glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(m_wireVertexCount)));
    GL_CHECK(glBindVertexArray(0));

    if (m_graphicsApi == GraphicsApi::OpenGL33)
        GL_CHECK(glDisable(GL_POLYGON_OFFSET_LINE));
}

void Renderer::renderEntity(const glm::vec3& position, const glm::vec3& size,
                            const glm::vec3& color, int textureIndex,
                            const glm::mat4& viewProjection) {
    renderEntityPart(position, glm::vec3(0.0f), size, 0.0f, color,
                     textureIndex, viewProjection);
}

void Renderer::renderCompatibilityEntityCube(
    const glm::vec3& position, const glm::vec3& size,
    const glm::vec3& color, int textureIndex,
    float yaw, const glm::mat4& viewProjection, SmoothLightSample light) {
    renderEntityPart(position, glm::vec3(0.0f), size, yaw, color,
                     textureIndex, viewProjection, light);
}

model::ModelRenderer& Renderer::modelRenderer() {
    return *m_modelRenderer;
}

void Renderer::flushModels(const glm::mat4& viewProjection) {
    const float fogEnd = (static_cast<float>(Config::RENDER_DISTANCE) + 0.5f) *
                         Config::CHUNK_SIZE_X;
    const glm::vec3 renderSpaceCamera(0.0f);
    m_modelRenderer->flushOpaque(viewProjection, m_environment,
        renderSpaceCamera, fogEnd * Config::FOG_START_FRACTION, fogEnd);
    m_modelRenderer->flushBlend(viewProjection, m_environment,
        renderSpaceCamera, fogEnd * Config::FOG_START_FRACTION, fogEnd);
}

void Renderer::beginViewModel(const glm::mat4& projection) {
    GL_CHECK(glClear(GL_DEPTH_BUFFER_BIT));
    (void)projection;
}

void Renderer::renderEntityPart(
    const glm::vec3& position, const glm::vec3& offset,
    const glm::vec3& size, float yaw, const glm::vec3& color,
    int textureIndex, const glm::mat4& viewProjection, SmoothLightSample light) {
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), position) *
                            glm::rotate(glm::mat4(1.0f), yaw,
                                        glm::vec3(0.0f, 1.0f, 0.0f)) *
                            glm::translate(glm::mat4(1.0f), offset) *
                            glm::scale(glm::mat4(1.0f), size);
    m_entityShader->bind();
    m_entityShader->setMat4("uMVP", viewProjection * model);
    m_entityShader->setVec3("uColor", color);
    m_entityShader->setInt("uTextureIndex", textureIndex);
    m_entityShader->setInt("uEntityAtlas", 0);
    m_entityShader->setInt("uUseTexture",
                           m_entityTexture && textureIndex >= 0 ? 1 : 0);
    m_entityShader->setInt("uManualGamma", 0);
    m_entityShader->setFloat("uSkyLight", light.sky);
    m_entityShader->setFloat("uBlockLight", light.block);
    GL_CHECK(glActiveTexture(GL_TEXTURE0));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_entityTexture));
    GL_CHECK(glBindVertexArray(m_entityVAO));
    GL_CHECK(glDrawArrays(GL_TRIANGLES, 0, 36));
    GL_CHECK(glBindVertexArray(0));
}

void Renderer::renderClouds(const glm::dvec3& playerPosition,
                            const glm::mat4& viewProjection,
                            uint64_t worldSeed, float timeSeconds,
                            int renderDistanceBlocks) {
    if (!visualQualityConfig(m_visualQuality).voxelClouds) return;
    // A coherent density field creates broad voxel cloud masses. Instancing
    // keeps them to one draw while per-cell face masks remove shared surfaces.
    const CloudView cloud = cloudView(
        playerPosition, timeSeconds, renderDistanceBlocks);
    const bool rebuild = m_cloudCacheRadius != cloud.radius ||
        m_cloudCacheCenterX != cloud.centerX ||
        m_cloudCacheCenterZ != cloud.centerZ ||
        m_cloudCacheSeed != worldSeed;
    if (rebuild) {
        m_cloudInstances = buildCloudInstances(
            worldSeed, cloud.centerX, cloud.centerZ, cloud.radius);
        m_cloudCacheRadius = cloud.radius;
        m_cloudCacheCenterX = cloud.centerX;
        m_cloudCacheCenterZ = cloud.centerZ;
        m_cloudCacheSeed = worldSeed;
    }
    if (m_cloudInstances.empty()) return;
    const glm::vec3 cloudColor = cloudColorForEnvironment(m_environment);

    if (!m_cloudVAO || !m_drawArraysInstanced) {
        for (const CloudInstance& instance : m_cloudInstances) {
            renderEntity(cloud.origin + glm::vec3(instance.x, instance.y, instance.z),
                         glm::vec3(instance.width, instance.height,
                                   instance.depth),
                         cloudColor, -1, viewProjection);
        }
        return;
    }

    m_cloudShader->bind();
    m_cloudShader->setMat4("uViewProjection", viewProjection);
    m_cloudShader->setVec3("uCloudOrigin", cloud.origin);
    m_cloudShader->setVec3("uColor", cloudColor);
    m_cloudShader->setVec3("uLightDirection", m_environment.lightDirection);
    m_cloudShader->setFloat("uRainIntensity", m_environment.rainIntensity);
    m_cloudShader->setInt("uManualGamma", 0);
    GL_CHECK(glBindVertexArray(m_cloudVAO));
    if (rebuild) {
        // Rotate allocations so a grid transition never overwrites instance
        // data that an earlier frame may still be consuming on the GPU.
        m_cloudInstanceBufferIndex =
            (m_cloudInstanceBufferIndex + 1) % CLOUD_INSTANCE_BUFFER_COUNT;
        GL_CHECK(glBindBuffer(
            GL_ARRAY_BUFFER,
            m_cloudInstanceVBOs[m_cloudInstanceBufferIndex]));
        GL_CHECK(glVertexAttribPointer(
            2, 4, GL_FLOAT, GL_FALSE, sizeof(CloudInstance), nullptr));
        GL_CHECK(glVertexAttribPointer(
            3, 2, GL_FLOAT, GL_FALSE, sizeof(CloudInstance),
            reinterpret_cast<void*>(4 * sizeof(float))));
        m_vertexAttribIPointer(
            4, 1, GL_UNSIGNED_INT, sizeof(CloudInstance),
            reinterpret_cast<void*>(offsetof(CloudInstance, visibleFaces)));
        const GLsizeiptr bytes = static_cast<GLsizeiptr>(
            m_cloudInstances.size() * sizeof(CloudInstance));
        if (m_bufferSubData)
            GL_CHECK(m_bufferSubData(
                GL_ARRAY_BUFFER, 0, bytes, m_cloudInstances.data()));
        else
            GL_CHECK(glBufferData(GL_ARRAY_BUFFER, bytes, m_cloudInstances.data(),
                                  GL_STREAM_DRAW));
    }
    m_drawArraysInstanced(GL_TRIANGLES, 0, 36,
                          static_cast<GLsizei>(m_cloudInstances.size()));
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
    m_particleShader->setFloat("uTime", static_cast<float>(RuntimeClock::seconds(RuntimeClock{}.now())));
    m_particleShader->setFloat("uIntensity", intensity);
    m_particleShader->setInt("uBlockAtlas", 0);
    m_particleShader->setFloat("uAtlasTiles",
                               static_cast<float>(BlockTextureAtlas::tilesPerSide()));
    m_particleShader->setInt("uManualGamma", 0);
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
    // The VAO retains references to these buffer objects, so deleting the
    // names now is safe: storage is freed when deleteVAO() removes the last
    // reference (glDeleteVertexArrays itself does NOT free referenced buffers).
    GL_CHECK(glDeleteBuffers(1, &vboPos));
    GL_CHECK(glDeleteBuffers(1, &vboCol));
    GL_CHECK(glDeleteBuffers(1, &ebo));

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
    // Delete the name; the VAO still references the storage, which is freed
    // when deleteVAO() removes the last reference.
    GL_CHECK(glDeleteBuffers(1, &vbo));

    outVertexCount = vertices.size() / 3;
    return vao;
}

void Renderer::deleteVAO(GLuint vao) {
    if (vao != 0) {
        GL_CHECK(glDeleteVertexArrays(1, &vao));
    }
}
