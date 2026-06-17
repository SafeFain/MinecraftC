#pragma once

#include "debug/Log.h"
#include <glad/glad.h>

namespace Debug {

// ── GL error code → human-readable string ──────────────────────────────

inline const char* glErrorString(GLenum error) {
    switch (error) {
        case GL_NO_ERROR:          return "GL_NO_ERROR";
        case GL_INVALID_ENUM:      return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE:     return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
        case GL_OUT_OF_MEMORY:     return "GL_OUT_OF_MEMORY";
        // GL_INVALID_FRAMEBUFFER_OPERATION = 0x0506 (not in minimal GLAD)
        case 0x0506:               return "GL_INVALID_FRAMEBUFFER_OPERATION";
        default:                   return "GL_UNKNOWN_ERROR";
    }
}

// ── Drain and log all pending GL errors ────────────────────────────────
// Returns true if any errors were found. Called by the GL_CHECK macro.

inline bool checkGLErrors(const char* file, int line, const char* expr) {
    bool hadError = false;
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        LOG_WARN("OpenGL error after `" << expr << "` "
                 "[" << file << ":" << line << "]: "
                 << glErrorString(err) << " (0x"
                 << std::hex << err << std::dec << ")");
        hadError = true;
    }
    return hadError;
}

} // namespace Debug

// ── GL_CHECK macro ─────────────────────────────────────────────────────
//
// Usage:
//   GL_CHECK(glBindVertexArray(vao));   // executes expr, then checks for errors
//
// In Debug builds: executes the expression, drains glGetError, and logs
// any errors with file/line/expression info.
//
// In Release builds (NDEBUG): executes the expression only — no error
// checking overhead. The GL call still happens; only the check is stripped.

#ifndef NDEBUG
  #define GL_CHECK(expr)                                                        \
      do {                                                                      \
          expr;                                                                 \
          Debug::checkGLErrors(__FILE__, __LINE__, #expr);                      \
      } while (0)
#else
  #define GL_CHECK(expr) expr
#endif
