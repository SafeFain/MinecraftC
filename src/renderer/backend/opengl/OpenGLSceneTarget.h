#pragma once

#include "renderer/VisualQuality.h"

// Owns the offscreen scene target: the HDR color attachment (RGBA8 fallback),
// the optional multisample color/depth renderbuffers, and the resolve
// framebuffer. create()/destroy()/bind()/blitResolve() replace the renderer's
// former createSceneTarget()/destroySceneTarget()/bindSceneTarget() plus the
// resolve step of finishScene(); the fallback chain (half-float -> RGBA8,
// MSAA clamp, multisample-HDR -> 1x, incomplete -> throw) is preserved
// verbatim so desktop GL and GLES degrade identically.

class OpenGLSceneTarget {
public:
    OpenGLSceneTarget() = default;
    ~OpenGLSceneTarget() { destroy(); }
    OpenGLSceneTarget(const OpenGLSceneTarget&) = delete;
    OpenGLSceneTarget& operator=(const OpenGLSceneTarget&) = delete;

    void create(int width, int height, VisualQuality quality);
    void destroy();
    void bind() const;
    void blitResolve() const;

    bool valid() const { return m_framebuffer != 0; }
    unsigned int framebuffer() const { return m_framebuffer; }
    unsigned int colorTexture() const { return m_colorTexture; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    int samples() const { return m_samples; }
    bool hdr() const { return m_hdr; }

private:
    unsigned int m_framebuffer = 0;
    unsigned int m_resolveFramebuffer = 0;
    unsigned int m_colorTexture = 0;
    unsigned int m_colorRenderbuffer = 0;
    unsigned int m_depthRenderbuffer = 0;
    int m_width = 0;
    int m_height = 0;
    int m_samples = 1;
    bool m_hdr = false;
};
