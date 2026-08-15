#pragma once

// Move-only RAII wrappers for owned OpenGL objects. Each wrapper releases its
// object on destruction; moved-from wrappers are empty. Framebuffer and
// renderbuffer entry points are resolved at runtime (see OpenGLProcs.h)
// because the minimal GLAD subset does not provide them.

#include "renderer/backend/opengl/OpenGLDebug.h"
#include "renderer/backend/opengl/OpenGLProcs.h"

class GlBuffer {
public:
    GlBuffer() = default;
    ~GlBuffer() { reset(); }
    GlBuffer(const GlBuffer&) = delete;
    GlBuffer& operator=(const GlBuffer&) = delete;
    GlBuffer(GlBuffer&& other) noexcept : m_name(other.m_name) { other.m_name = 0; }
    GlBuffer& operator=(GlBuffer&& other) noexcept {
        if (this != &other) {
            reset();
            m_name = other.m_name;
            other.m_name = 0;
        }
        return *this;
    }
    void allocate() { GL_CHECK(glGenBuffers(1, &m_name)); }
    void reset() {
        if (m_name) {
            GL_CHECK(glDeleteBuffers(1, &m_name));
            m_name = 0;
        }
    }
    explicit operator bool() const { return m_name != 0; }
    GLuint name() const { return m_name; }
private:
    GLuint m_name = 0;
};

class GlVertexArray {
public:
    GlVertexArray() = default;
    ~GlVertexArray() { reset(); }
    GlVertexArray(const GlVertexArray&) = delete;
    GlVertexArray& operator=(const GlVertexArray&) = delete;
    GlVertexArray(GlVertexArray&& other) noexcept : m_name(other.m_name) { other.m_name = 0; }
    GlVertexArray& operator=(GlVertexArray&& other) noexcept {
        if (this != &other) {
            reset();
            m_name = other.m_name;
            other.m_name = 0;
        }
        return *this;
    }
    void allocate() { GL_CHECK(glGenVertexArrays(1, &m_name)); }
    void reset() {
        if (m_name) {
            GL_CHECK(glDeleteVertexArrays(1, &m_name));
            m_name = 0;
        }
    }
    explicit operator bool() const { return m_name != 0; }
    GLuint name() const { return m_name; }
private:
    GLuint m_name = 0;
};

class GlTexture {
public:
    GlTexture() = default;
    ~GlTexture() { reset(); }
    GlTexture(const GlTexture&) = delete;
    GlTexture& operator=(const GlTexture&) = delete;
    GlTexture(GlTexture&& other) noexcept : m_name(other.m_name) { other.m_name = 0; }
    GlTexture& operator=(GlTexture&& other) noexcept {
        if (this != &other) {
            reset();
            m_name = other.m_name;
            other.m_name = 0;
        }
        return *this;
    }
    void allocate() { GL_CHECK(glGenTextures(1, &m_name)); }
    void reset() {
        if (m_name) {
            GL_CHECK(glDeleteTextures(1, &m_name));
            m_name = 0;
        }
    }
    explicit operator bool() const { return m_name != 0; }
    GLuint name() const { return m_name; }
private:
    GLuint m_name = 0;
};

class GlFramebuffer {
public:
    GlFramebuffer() = default;
    ~GlFramebuffer() { reset(); }
    GlFramebuffer(const GlFramebuffer&) = delete;
    GlFramebuffer& operator=(const GlFramebuffer&) = delete;
    GlFramebuffer(GlFramebuffer&& other) noexcept : m_name(other.m_name) { other.m_name = 0; }
    GlFramebuffer& operator=(GlFramebuffer&& other) noexcept {
        if (this != &other) {
            reset();
            m_name = other.m_name;
            other.m_name = 0;
        }
        return *this;
    }
    void allocate() {
        auto gen = glProc<GenFramebuffersFn>("glGenFramebuffers");
        if (gen) gen(1, &m_name);
    }
    void reset() {
        if (m_name) {
            if (auto destroy = glProc<DeleteFramebuffersFn>("glDeleteFramebuffers"))
                destroy(1, &m_name);
            m_name = 0;
        }
    }
    explicit operator bool() const { return m_name != 0; }
    GLuint name() const { return m_name; }
private:
    GLuint m_name = 0;
};

class GlRenderbuffer {
public:
    GlRenderbuffer() = default;
    ~GlRenderbuffer() { reset(); }
    GlRenderbuffer(const GlRenderbuffer&) = delete;
    GlRenderbuffer& operator=(const GlRenderbuffer&) = delete;
    GlRenderbuffer(GlRenderbuffer&& other) noexcept : m_name(other.m_name) { other.m_name = 0; }
    GlRenderbuffer& operator=(GlRenderbuffer&& other) noexcept {
        if (this != &other) {
            reset();
            m_name = other.m_name;
            other.m_name = 0;
        }
        return *this;
    }
    void allocate() {
        auto gen = glProc<GenRenderbuffersFn>("glGenRenderbuffers");
        if (gen) gen(1, &m_name);
    }
    void reset() {
        if (m_name) {
            if (auto destroy = glProc<DeleteRenderbuffersFn>("glDeleteRenderbuffers"))
                destroy(1, &m_name);
            m_name = 0;
        }
    }
    explicit operator bool() const { return m_name != 0; }
    GLuint name() const { return m_name; }
private:
    GLuint m_name = 0;
};
