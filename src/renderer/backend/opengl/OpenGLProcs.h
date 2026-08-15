#pragma once

// Shared plumbing for the OpenGL backend components. The minimal GLAD subset
// does not provide the GL 3.0+/GLES framebuffer and renderbuffer entry
// points, so those are resolved at runtime through Window::graphicsProcAddress
// and the matching numeric constants are spelled out here (values are
// identical in desktop GL and GLES). Members of this header live at global
// namespace scope to keep the original call sites unchanged.

#include "core/Window.h"

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
