#include "renderer/backend/opengl/OpenGLSceneTarget.h"
#include "renderer/backend/opengl/OpenGLDebug.h"
#include "renderer/backend/opengl/OpenGLProcs.h"
#include "debug/Log.h"

#include <algorithm>
#include <stdexcept>

void OpenGLSceneTarget::create(int width, int height, VisualQuality quality) {
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
        destroy();
        return;
    }

    destroy();
    m_width = std::max(1, width);
    m_height = std::max(1, height);
    genFramebuffers(1, &m_resolveFramebuffer);
    bindFramebuffer(GL_FRAMEBUFFER_VALUE, m_resolveFramebuffer);
    GL_CHECK(glGenTextures(1, &m_colorTexture));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_colorTexture));
    const auto allocateColor = [&](unsigned int internalFormat,
                                   unsigned int type) {
        GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, internalFormat,
            m_width, m_height, 0, GL_RGBA, type, nullptr));
        framebufferTexture(GL_FRAMEBUFFER_VALUE, GL_COLOR_ATTACHMENT0_VALUE,
                           GL_TEXTURE_2D, m_colorTexture, 0);
        return checkFramebuffer(GL_FRAMEBUFFER_VALUE) ==
               GL_FRAMEBUFFER_COMPLETE_VALUE;
    };
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_VALUE));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR_VALUE));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                             GL_CLAMP_TO_EDGE_VALUE));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                             GL_CLAMP_TO_EDGE_VALUE));
    m_hdr = allocateColor(GL_RGBA16F_VALUE, GL_HALF_FLOAT_VALUE);
    if (!m_hdr) {
        if (!allocateColor(GL_RGBA8_VALUE, GL_UNSIGNED_BYTE)) {
            bindFramebuffer(GL_FRAMEBUFFER_VALUE, 0);
            destroy();
            throw std::runtime_error("OpenGL scene framebuffer is incomplete");
        }
        LOG_WARN("OpenGL half-float color target unavailable; using RGBA8");
    }

    int maximumSamples = 1;
    GL_CHECK(glGetIntegerv(GL_MAX_SAMPLES_VALUE, &maximumSamples));
    const int requestedSamples = visualQualityConfig(quality).sceneSamples;
    m_samples = requestedSamples >= 4 && maximumSamples >= 4 ? 4 :
                requestedSamples >= 2 && maximumSamples >= 2 ? 2 : 1;
    auto multisampleStorage = glProc<RenderbufferStorageMultisampleFn>(
        "glRenderbufferStorageMultisample");
    auto blit = glProc<BlitFramebufferFn>("glBlitFramebuffer");
    if (!multisampleStorage || !blit) m_samples = 1;

    if (m_samples > 1) {
        genFramebuffers(1, &m_framebuffer);
        bindFramebuffer(GL_FRAMEBUFFER_VALUE, m_framebuffer);
        genRenderbuffers(1, &m_colorRenderbuffer);
        bindRenderbuffer(GL_RENDERBUFFER_VALUE, m_colorRenderbuffer);
        multisampleStorage(GL_RENDERBUFFER_VALUE, m_samples,
            m_hdr ? GL_RGBA16F_VALUE : GL_RGBA8_VALUE,
            m_width, m_height);
        framebufferRenderbuffer(GL_FRAMEBUFFER_VALUE, GL_COLOR_ATTACHMENT0_VALUE,
            GL_RENDERBUFFER_VALUE, m_colorRenderbuffer);
    } else {
        m_framebuffer = m_resolveFramebuffer;
    }
    genRenderbuffers(1, &m_depthRenderbuffer);
    bindRenderbuffer(GL_RENDERBUFFER_VALUE, m_depthRenderbuffer);
    if (m_samples > 1)
        multisampleStorage(GL_RENDERBUFFER_VALUE, m_samples,
            GL_DEPTH_COMPONENT24_VALUE, m_width, m_height);
    else
        renderbufferStorage(GL_RENDERBUFFER_VALUE, GL_DEPTH_COMPONENT24_VALUE,
            m_width, m_height);
    framebufferRenderbuffer(GL_FRAMEBUFFER_VALUE, GL_DEPTH_ATTACHMENT_VALUE,
        GL_RENDERBUFFER_VALUE, m_depthRenderbuffer);
    if (checkFramebuffer(GL_FRAMEBUFFER_VALUE) != GL_FRAMEBUFFER_COMPLETE_VALUE &&
        m_samples > 1) {
        // GLES implementations commonly expose a renderable half-float texture
        // without supporting the same format as a multisample renderbuffer.
        // Preserve HDR and fall back to one sample instead of failing startup.
        if (auto destroyRenderbuffers =
                glProc<DeleteRenderbuffersFn>("glDeleteRenderbuffers")) {
            destroyRenderbuffers(1, &m_colorRenderbuffer);
            destroyRenderbuffers(1, &m_depthRenderbuffer);
        }
        if (auto destroyFramebuffers =
                glProc<DeleteFramebuffersFn>("glDeleteFramebuffers"))
            destroyFramebuffers(1, &m_framebuffer);
        m_colorRenderbuffer = 0;
        m_depthRenderbuffer = 0;
        m_framebuffer = m_resolveFramebuffer;
        m_samples = 1;
        bindFramebuffer(GL_FRAMEBUFFER_VALUE, m_framebuffer);
        genRenderbuffers(1, &m_depthRenderbuffer);
        bindRenderbuffer(GL_RENDERBUFFER_VALUE, m_depthRenderbuffer);
        renderbufferStorage(GL_RENDERBUFFER_VALUE, GL_DEPTH_COMPONENT24_VALUE,
            m_width, m_height);
        framebufferRenderbuffer(GL_FRAMEBUFFER_VALUE, GL_DEPTH_ATTACHMENT_VALUE,
            GL_RENDERBUFFER_VALUE, m_depthRenderbuffer);
        LOG_WARN("OpenGL multisample HDR target unavailable; using 1x HDR");
    }
    if (checkFramebuffer(GL_FRAMEBUFFER_VALUE) != GL_FRAMEBUFFER_COMPLETE_VALUE) {
        bindFramebuffer(GL_FRAMEBUFFER_VALUE, 0);
        destroy();
        throw std::runtime_error("OpenGL scene framebuffer is incomplete");
    }
    bindRenderbuffer(GL_RENDERBUFFER_VALUE, 0);
    bindFramebuffer(GL_FRAMEBUFFER_VALUE, m_framebuffer);
    GL_CHECK(glViewport(0, 0, m_width, m_height));
    LOG_INFO("OpenGL scene target: " << (m_hdr ? "RGBA16F" : "RGBA8")
             << ", " << m_samples << "x MSAA");
}

void OpenGLSceneTarget::destroy() {
    if (m_colorTexture)
        GL_CHECK(glDeleteTextures(1, &m_colorTexture));
    m_colorTexture = 0;
    if (auto destroyRenderbuffers =
            glProc<DeleteRenderbuffersFn>("glDeleteRenderbuffers")) {
        if (m_colorRenderbuffer)
            destroyRenderbuffers(1, &m_colorRenderbuffer);
        if (m_depthRenderbuffer)
            destroyRenderbuffers(1, &m_depthRenderbuffer);
    }
    m_colorRenderbuffer = 0;
    m_depthRenderbuffer = 0;
    if (auto destroyFramebuffers =
            glProc<DeleteFramebuffersFn>("glDeleteFramebuffers")) {
        if (m_framebuffer)
            destroyFramebuffers(1, &m_framebuffer);
        if (m_resolveFramebuffer && m_resolveFramebuffer != m_framebuffer)
            destroyFramebuffers(1, &m_resolveFramebuffer);
    }
    m_framebuffer = 0;
    m_resolveFramebuffer = 0;
    m_width = 0;
    m_height = 0;
    m_samples = 1;
    m_hdr = false;
}

void OpenGLSceneTarget::bind() const {
    if (auto bindFramebuffer = glProc<BindFramebufferFn>("glBindFramebuffer"))
        bindFramebuffer(GL_FRAMEBUFFER_VALUE, m_framebuffer);
    GL_CHECK(glViewport(0, 0, std::max(1, m_width),
                        std::max(1, m_height)));
}

void OpenGLSceneTarget::blitResolve() const {
    if (m_samples <= 1) return;
    auto bindFramebuffer = glProc<BindFramebufferFn>("glBindFramebuffer");
    if (!bindFramebuffer) return;
    if (auto blit = glProc<BlitFramebufferFn>("glBlitFramebuffer")) {
        bindFramebuffer(GL_READ_FRAMEBUFFER_VALUE, m_framebuffer);
        bindFramebuffer(GL_DRAW_FRAMEBUFFER_VALUE, m_resolveFramebuffer);
        blit(0, 0, m_width, m_height,
             0, 0, m_width, m_height,
             GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
}
