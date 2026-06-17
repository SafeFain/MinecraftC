#include "glad/glad.h"
#include <stdlib.h>

// ── Function pointer definitions ──────────────────────────────────────

PFNGLCLEARPROC                  glad_glClear;
PFNGLCLEARCOLORPROC             glad_glClearColor;
PFNGLENABLEPROC                 glad_glEnable;
PFNGLDISABLEPROC                glad_glDisable;
PFNGLCULLFACEPROC               glad_glCullFace;
PFNGLFRONTFACEPROC              glad_glFrontFace;
PFNGLVIEWPORTPROC               glad_glViewport;
PFNGLGETINTEGERVPROC            glad_glGetIntegerv;
PFNGLGETSTRINGPROC              glad_glGetString;
PFNGLGETERRORPROC               glad_glGetError;

PFNGLCREATESHADERPROC           glad_glCreateShader;
PFNGLSHADERSOURCEPROC           glad_glShaderSource;
PFNGLCOMPILESHADERPROC          glad_glCompileShader;
PFNGLGETSHADERIVPROC            glad_glGetShaderiv;
PFNGLGETSHADERINFOLOGPROC       glad_glGetShaderInfoLog;
PFNGLDELETESHADERPROC           glad_glDeleteShader;
PFNGLCREATEPROGRAMPROC          glad_glCreateProgram;
PFNGLATTACHSHADERPROC           glad_glAttachShader;
PFNGLLINKPROGRAMPROC            glad_glLinkProgram;
PFNGLGETPROGRAMIVPROC           glad_glGetProgramiv;
PFNGLGETPROGRAMINFOLOGPROC      glad_glGetProgramInfoLog;
PFNGLUSEPROGRAMPROC             glad_glUseProgram;
PFNGLDELETEPROGRAMPROC          glad_glDeleteProgram;

PFNGLGETUNIFORMLOCATIONPROC     glad_glGetUniformLocation;
PFNGLUNIFORMMATRIX4FVPROC       glad_glUniformMatrix4fv;
PFNGLUNIFORM3FPROC              glad_glUniform3f;
PFNGLUNIFORM1IPROC              glad_glUniform1i;
PFNGLBLENDFUNCPROC              glad_glBlendFunc;
PFNGLGETBOOLEANVPROC            glad_glGetBooleanv;
PFNGLUNIFORM4FPROC              glad_glUniform4f;

PFNGLGENVERTEXARRAYSPROC        glad_glGenVertexArrays;
PFNGLBINDVERTEXARRAYPROC        glad_glBindVertexArray;
PFNGLDELETEVERTEXARRAYSPROC     glad_glDeleteVertexArrays;
PFNGLGENBUFFERSPROC             glad_glGenBuffers;
PFNGLBINDBUFFERPROC             glad_glBindBuffer;
PFNGLBUFFERDATAPROC             glad_glBufferData;
PFNGLDELETEBUFFERSPROC          glad_glDeleteBuffers;
PFNGLVERTEXATTRIBPOINTERPROC    glad_glVertexAttribPointer;
PFNGLENABLEVERTEXATTRIBARRAYPROC glad_glEnableVertexAttribArray;
PFNGLDISABLEVERTEXATTRIBARRAYPROC glad_glDisableVertexAttribArray;

PFNGLDRAWARRAYSPROC             glad_glDrawArrays;
PFNGLDRAWELEMENTSPROC           glad_glDrawElements;

PFNGLPOLYGONOFFSETPROC          glad_glPolygonOffset;

PFNGLACTIVETEXTUREPROC          glad_glActiveTexture;
PFNGLGENERATEMIPMAPPROC         glad_glGenerateMipmap;
PFNGLGENTEXTURESPROC            glad_glGenTextures;
PFNGLBINDTEXTUREPROC            glad_glBindTexture;
PFNGLTEXIMAGE2DPROC             glad_glTexImage2D;
PFNGLTEXPARAMETERIPROC          glad_glTexParameteri;
PFNGLDELETETEXTURESPROC         glad_glDeleteTextures;

// ── Macro helper ──────────────────────────────────────────────────────

#define LOAD(func) \
    glad_##func = reinterpret_cast<decltype(glad_##func)>(getProcAddress(#func)); \
    if (!glad_##func) return 0

// ── Load all functions ────────────────────────────────────────────────

extern "C" int gladLoadGL(void* (*getProcAddress)(const char*)) {
    LOAD(glClear);
    LOAD(glClearColor);
    LOAD(glEnable);
    LOAD(glDisable);
    LOAD(glCullFace);
    LOAD(glFrontFace);
    LOAD(glViewport);
    LOAD(glGetIntegerv);
    LOAD(glGetString);
    LOAD(glGetError);

    LOAD(glCreateShader);
    LOAD(glShaderSource);
    LOAD(glCompileShader);
    LOAD(glGetShaderiv);
    LOAD(glGetShaderInfoLog);
    LOAD(glDeleteShader);
    LOAD(glCreateProgram);
    LOAD(glAttachShader);
    LOAD(glLinkProgram);
    LOAD(glGetProgramiv);
    LOAD(glGetProgramInfoLog);
    LOAD(glUseProgram);
    LOAD(glDeleteProgram);

    LOAD(glGetUniformLocation);
    LOAD(glUniformMatrix4fv);
    LOAD(glUniform3f);
    LOAD(glUniform1i);
    LOAD(glBlendFunc);
    LOAD(glGetBooleanv);
    LOAD(glUniform4f);

    LOAD(glGenVertexArrays);
    LOAD(glBindVertexArray);
    LOAD(glDeleteVertexArrays);
    LOAD(glGenBuffers);
    LOAD(glBindBuffer);
    LOAD(glBufferData);
    LOAD(glDeleteBuffers);
    LOAD(glVertexAttribPointer);
    LOAD(glEnableVertexAttribArray);
    LOAD(glDisableVertexAttribArray);

    LOAD(glDrawArrays);
    LOAD(glDrawElements);

    LOAD(glPolygonOffset);

    LOAD(glActiveTexture);
    LOAD(glGenerateMipmap);
    LOAD(glGenTextures);
    LOAD(glBindTexture);
    LOAD(glTexImage2D);
    LOAD(glTexParameteri);
    LOAD(glDeleteTextures);

    return 1;
}
