#pragma once

#include "debug/Log.h"
#include <cstdlib>

// ── Assertion macros ───────────────────────────────────────────────────
//
// MC_ASSERT(cond, msg)  — Debug-only check.  Compiled out in Release (NDEBUG).
//                          On failure: log FATAL + abort().
//
// MC_CHECK(cond, msg)   — Always-on cheap check.  Logs ERROR on failure, does
//                          NOT abort.  For invariants that should never fail
//                          but where recovery is possible.
//
// MC_VERIFY(cond, msg)  — Always-on fatal check.  Like MC_ASSERT but never
//                          stripped.  Use when the expression has side effects
//                          that MUST execute in all build configs.
//
// ── Examples ───────────────────────────────────────────────────────────
//   MC_ASSERT(indexCount > 0, "Mesh built with zero indices — chunk " << cx << "," << cz);
//   MC_CHECK(glfwInit(), "GLFW initialization failed");
//   MC_VERIFY(gladLoadGL(loader), "Failed to load OpenGL functions");

#ifndef NDEBUG
  #define MC_ASSERT(cond, msg)                                                  \
      do {                                                                      \
          if (!(cond)) {                                                        \
              LOG_FATAL("ASSERTION FAILED: " << #cond << "  — " << msg);       \
              std::abort();                                                     \
          }                                                                     \
      } while (0)
#else
  #define MC_ASSERT(cond, msg) ((void)0)
#endif

#define MC_CHECK(cond, msg)                                                     \
    do {                                                                        \
        if (!(cond)) {                                                          \
            LOG_ERROR("CHECK FAILED: " << #cond << "  — " << msg);             \
        }                                                                       \
    } while (0)

#define MC_VERIFY(cond, msg)                                                    \
    do {                                                                        \
        if (!(cond)) {                                                          \
            LOG_FATAL("VERIFY FAILED: " << #cond << "  — " << msg);            \
            std::abort();                                                       \
        }                                                                       \
    } while (0)
