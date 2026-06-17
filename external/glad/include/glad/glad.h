#ifndef GLAD_H
#define GLAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#ifndef APIENTRY
#define APIENTRY
#endif

typedef void* (*GLADloadfunc)(const char* name);

// ── OpenGL Core Types ─────────────────────────────────────────────────

#ifndef GL_TYPES_DEFINED
#define GL_TYPES_DEFINED

typedef unsigned int    GLenum;
typedef unsigned char   GLboolean;
typedef unsigned int    GLbitfield;
typedef void            GLvoid;
typedef int8_t          GLbyte;
typedef uint8_t         GLubyte;
typedef int16_t         GLshort;
typedef uint16_t        GLushort;
typedef int              GLint;
typedef unsigned int     GLuint;
typedef int              GLsizei;
typedef float            GLfloat;
typedef float            GLclampf;
typedef double           GLdouble;
typedef double           GLclampd;
typedef ptrdiff_t        GLsizeiptr;
typedef ptrdiff_t        GLintptr;
typedef char             GLchar;

#endif // GL_TYPES_DEFINED

#ifndef GL_FALSE
#define GL_FALSE                    0
#define GL_TRUE                     1
#endif

#ifndef GL_NO_ERROR
#define GL_NO_ERROR                 0
#define GL_INVALID_ENUM             0x0500
#define GL_INVALID_VALUE            0x0501
#define GL_INVALID_OPERATION        0x0502
#define GL_OUT_OF_MEMORY            0x0505
#endif

#ifndef GL_COLOR_BUFFER_BIT
#define GL_COLOR_BUFFER_BIT         0x00004000
#define GL_DEPTH_BUFFER_BIT         0x00000100
#define GL_DEPTH_TEST               0x0B71
#define GL_CULL_FACE                0x0B44
#define GL_BACK                     0x0405
#define GL_FRONT                    0x0404
#define GL_CCW                      0x0901
#define GL_CW                       0x0900
#define GL_LINES                    0x0001
#define GL_TRIANGLES                0x0004
#define GL_UNSIGNED_INT             0x1405
#define GL_FLOAT                    0x1406
#define GL_STATIC_DRAW              0x88E4
#define GL_DYNAMIC_DRAW             0x88E8
#endif

#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER            0x8B31
#define GL_FRAGMENT_SHADER          0x8B30
#define GL_COMPILE_STATUS           0x8B81
#define GL_LINK_STATUS              0x8B82
#define GL_INFO_LOG_LENGTH          0x8B84
#endif

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER             0x8892
#define GL_ELEMENT_ARRAY_BUFFER     0x8893
#endif

#ifndef GL_POLYGON_OFFSET_LINE
#define GL_POLYGON_OFFSET_LINE      0x2A01
#endif

#ifndef GL_POLYGON_OFFSET_FILL
#define GL_POLYGON_OFFSET_FILL      0x8037
#endif

#ifndef GL_MAJOR_VERSION
#define GL_MAJOR_VERSION            0x821B
#define GL_MINOR_VERSION            0x821C
#endif

#ifndef GL_VERSION
#define GL_VERSION                  0x1F02
#define GL_RENDERER                 0x1F01
#define GL_VENDOR                   0x1F00
#endif

// ── Function Pointer Types ────────────────────────────────────────────

typedef void          (APIENTRY *PFNGLCLEARPROC)(GLbitfield);
typedef void          (APIENTRY *PFNGLCLEARCOLORPROC)(GLclampf, GLclampf, GLclampf, GLclampf);
typedef void          (APIENTRY *PFNGLENABLEPROC)(GLenum);
typedef void          (APIENTRY *PFNGLDISABLEPROC)(GLenum);
typedef void          (APIENTRY *PFNGLCULLFACEPROC)(GLenum);
typedef void          (APIENTRY *PFNGLFRONTFACEPROC)(GLenum);
typedef void          (APIENTRY *PFNGLVIEWPORTPROC)(GLint, GLint, GLsizei, GLsizei);
typedef void          (APIENTRY *PFNGLGETINTEGERVPROC)(GLenum, GLint*);
typedef const GLubyte*(APIENTRY *PFNGLGETSTRINGPROC)(GLenum);
typedef GLenum        (APIENTRY *PFNGLGETERRORPROC)(void);

typedef GLuint        (APIENTRY *PFNGLCREATESHADERPROC)(GLenum);
typedef void          (APIENTRY *PFNGLSHADERSOURCEPROC)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void          (APIENTRY *PFNGLCOMPILESHADERPROC)(GLuint);
typedef void          (APIENTRY *PFNGLGETSHADERIVPROC)(GLuint, GLenum, GLint*);
typedef void          (APIENTRY *PFNGLGETSHADERINFOLOGPROC)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void          (APIENTRY *PFNGLDELETESHADERPROC)(GLuint);
typedef GLuint        (APIENTRY *PFNGLCREATEPROGRAMPROC)(void);
typedef void          (APIENTRY *PFNGLATTACHSHADERPROC)(GLuint, GLuint);
typedef void          (APIENTRY *PFNGLLINKPROGRAMPROC)(GLuint);
typedef void          (APIENTRY *PFNGLGETPROGRAMIVPROC)(GLuint, GLenum, GLint*);
typedef void          (APIENTRY *PFNGLGETPROGRAMINFOLOGPROC)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void          (APIENTRY *PFNGLUSEPROGRAMPROC)(GLuint);
typedef void          (APIENTRY *PFNGLDELETEPROGRAMPROC)(GLuint);

typedef GLint         (APIENTRY *PFNGLGETUNIFORMLOCATIONPROC)(GLuint, const GLchar*);
typedef void          (APIENTRY *PFNGLUNIFORMMATRIX4FVPROC)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void          (APIENTRY *PFNGLUNIFORM3FPROC)(GLint, GLfloat, GLfloat, GLfloat);
typedef void          (APIENTRY *PFNGLUNIFORM1IPROC)(GLint, GLint);
typedef void          (APIENTRY *PFNGLBLENDFUNCPROC)(GLenum, GLenum);
typedef void          (APIENTRY *PFNGLGETBOOLEANVPROC)(GLenum, GLboolean*);
typedef void          (APIENTRY *PFNGLUNIFORM4FPROC)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);

typedef void          (APIENTRY *PFNGLGENVERTEXARRAYSPROC)(GLsizei, GLuint*);
typedef void          (APIENTRY *PFNGLBINDVERTEXARRAYPROC)(GLuint);
typedef void          (APIENTRY *PFNGLDELETEVERTEXARRAYSPROC)(GLsizei, const GLuint*);
typedef void          (APIENTRY *PFNGLGENBUFFERSPROC)(GLsizei, GLuint*);
typedef void          (APIENTRY *PFNGLBINDBUFFERPROC)(GLenum, GLuint);
typedef void          (APIENTRY *PFNGLBUFFERDATAPROC)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void          (APIENTRY *PFNGLDELETEBUFFERSPROC)(GLsizei, const GLuint*);
typedef void          (APIENTRY *PFNGLVERTEXATTRIBPOINTERPROC)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void          (APIENTRY *PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void          (APIENTRY *PFNGLDISABLEVERTEXATTRIBARRAYPROC)(GLuint);

typedef void          (APIENTRY *PFNGLDRAWARRAYSPROC)(GLenum, GLint, GLsizei);
typedef void          (APIENTRY *PFNGLDRAWELEMENTSPROC)(GLenum, GLsizei, GLenum, const void*);

typedef void          (APIENTRY *PFNGLPOLYGONOFFSETPROC)(GLfloat, GLfloat);

typedef void          (APIENTRY *PFNGLACTIVETEXTUREPROC)(GLenum);
typedef void          (APIENTRY *PFNGLGENERATEMIPMAPPROC)(GLenum);

#ifndef GL_TEXTURE0
#define GL_TEXTURE0                 0x84C0
#define GL_TEXTURE_2D               0x0DE1
#define GL_TEXTURE_MIN_FILTER       0x2801
#define GL_TEXTURE_MAG_FILTER       0x2800
#define GL_NEAREST                  0x2600
#define GL_NEAREST_MIPMAP_NEAREST   0x2700
#define GL_RGBA                     0x1908
#define GL_UNSIGNED_BYTE            0x1401
#endif

#ifndef GL_BLEND
#define GL_BLEND                    0x0BE2
#define GL_SRC_ALPHA                0x0302
#define GL_ONE_MINUS_SRC_ALPHA      0x0303
#define GL_BLEND_SRC                0x0BE1
#define GL_BLEND_DST                0x0BE0
#define GL_RED                      0x1903
#define GL_R8                       0x8229
#define GL_STREAM_DRAW              0x88E0
#define GL_CLAMP_TO_EDGE            0x812F
#define GL_TEXTURE_WRAP_S           0x2802
#define GL_TEXTURE_WRAP_T           0x2803
#define GL_ACTIVE_TEXTURE           0x84E0
#endif

typedef void          (APIENTRY *PFNGLGENTEXTURESPROC)(GLsizei, GLuint*);
typedef void          (APIENTRY *PFNGLBINDTEXTUREPROC)(GLenum, GLuint);
typedef void          (APIENTRY *PFNGLTEXIMAGE2DPROC)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
typedef void          (APIENTRY *PFNGLTEXPARAMETERIPROC)(GLenum, GLenum, GLint);
typedef void          (APIENTRY *PFNGLDELETETEXTURESPROC)(GLsizei, const GLuint*);

// ── External Function Pointers ────────────────────────────────────────

extern PFNGLCLEARPROC                  glad_glClear;
extern PFNGLCLEARCOLORPROC             glad_glClearColor;
extern PFNGLENABLEPROC                 glad_glEnable;
extern PFNGLDISABLEPROC                glad_glDisable;
extern PFNGLCULLFACEPROC               glad_glCullFace;
extern PFNGLFRONTFACEPROC              glad_glFrontFace;
extern PFNGLVIEWPORTPROC               glad_glViewport;
extern PFNGLGETINTEGERVPROC            glad_glGetIntegerv;
extern PFNGLGETSTRINGPROC              glad_glGetString;
extern PFNGLGETERRORPROC               glad_glGetError;

extern PFNGLCREATESHADERPROC           glad_glCreateShader;
extern PFNGLSHADERSOURCEPROC           glad_glShaderSource;
extern PFNGLCOMPILESHADERPROC          glad_glCompileShader;
extern PFNGLGETSHADERIVPROC            glad_glGetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC       glad_glGetShaderInfoLog;
extern PFNGLDELETESHADERPROC           glad_glDeleteShader;
extern PFNGLCREATEPROGRAMPROC          glad_glCreateProgram;
extern PFNGLATTACHSHADERPROC           glad_glAttachShader;
extern PFNGLLINKPROGRAMPROC            glad_glLinkProgram;
extern PFNGLGETPROGRAMIVPROC           glad_glGetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC      glad_glGetProgramInfoLog;
extern PFNGLUSEPROGRAMPROC             glad_glUseProgram;
extern PFNGLDELETEPROGRAMPROC          glad_glDeleteProgram;

extern PFNGLGETUNIFORMLOCATIONPROC     glad_glGetUniformLocation;
extern PFNGLUNIFORMMATRIX4FVPROC       glad_glUniformMatrix4fv;
extern PFNGLUNIFORM3FPROC              glad_glUniform3f;
extern PFNGLUNIFORM1IPROC              glad_glUniform1i;
extern PFNGLBLENDFUNCPROC              glad_glBlendFunc;
extern PFNGLGETBOOLEANVPROC            glad_glGetBooleanv;
extern PFNGLUNIFORM4FPROC              glad_glUniform4f;

extern PFNGLGENVERTEXARRAYSPROC        glad_glGenVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC        glad_glBindVertexArray;
extern PFNGLDELETEVERTEXARRAYSPROC     glad_glDeleteVertexArrays;
extern PFNGLGENBUFFERSPROC             glad_glGenBuffers;
extern PFNGLBINDBUFFERPROC             glad_glBindBuffer;
extern PFNGLBUFFERDATAPROC             glad_glBufferData;
extern PFNGLDELETEBUFFERSPROC          glad_glDeleteBuffers;
extern PFNGLVERTEXATTRIBPOINTERPROC    glad_glVertexAttribPointer;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC glad_glEnableVertexAttribArray;
extern PFNGLDISABLEVERTEXATTRIBARRAYPROC glad_glDisableVertexAttribArray;

extern PFNGLDRAWARRAYSPROC             glad_glDrawArrays;
extern PFNGLDRAWELEMENTSPROC           glad_glDrawElements;

extern PFNGLPOLYGONOFFSETPROC          glad_glPolygonOffset;

extern PFNGLACTIVETEXTUREPROC          glad_glActiveTexture;
extern PFNGLGENERATEMIPMAPPROC         glad_glGenerateMipmap;
extern PFNGLGENTEXTURESPROC            glad_glGenTextures;
extern PFNGLBINDTEXTUREPROC            glad_glBindTexture;
extern PFNGLTEXIMAGE2DPROC             glad_glTexImage2D;
extern PFNGLTEXPARAMETERIPROC          glad_glTexParameteri;
extern PFNGLDELETETEXTURESPROC         glad_glDeleteTextures;

// ── Loader ────────────────────────────────────────────────────────────

int gladLoadGL(void* (*getProcAddress)(const char*));

#ifdef __cplusplus
}
#endif

// ── Convenience macros (so code can use glClear etc. directly) ────────

#define glClear                 glad_glClear
#define glClearColor            glad_glClearColor
#define glEnable                glad_glEnable
#define glDisable               glad_glDisable
#define glCullFace              glad_glCullFace
#define glFrontFace             glad_glFrontFace
#define glViewport              glad_glViewport
#define glGetIntegerv           glad_glGetIntegerv
#define glGetString             glad_glGetString
#define glGetError              glad_glGetError

#define glCreateShader          glad_glCreateShader
#define glShaderSource          glad_glShaderSource
#define glCompileShader         glad_glCompileShader
#define glGetShaderiv           glad_glGetShaderiv
#define glGetShaderInfoLog      glad_glGetShaderInfoLog
#define glDeleteShader          glad_glDeleteShader
#define glCreateProgram         glad_glCreateProgram
#define glAttachShader          glad_glAttachShader
#define glLinkProgram           glad_glLinkProgram
#define glGetProgramiv          glad_glGetProgramiv
#define glGetProgramInfoLog     glad_glGetProgramInfoLog
#define glUseProgram            glad_glUseProgram
#define glDeleteProgram         glad_glDeleteProgram

#define glGetUniformLocation    glad_glGetUniformLocation
#define glUniformMatrix4fv      glad_glUniformMatrix4fv
#define glUniform3f             glad_glUniform3f
#define glUniform1i             glad_glUniform1i
#define glBlendFunc             glad_glBlendFunc
#define glGetBooleanv           glad_glGetBooleanv
#define glUniform4f             glad_glUniform4f

#define glGenVertexArrays       glad_glGenVertexArrays
#define glBindVertexArray       glad_glBindVertexArray
#define glDeleteVertexArrays    glad_glDeleteVertexArrays
#define glGenBuffers            glad_glGenBuffers
#define glBindBuffer            glad_glBindBuffer
#define glBufferData            glad_glBufferData
#define glDeleteBuffers         glad_glDeleteBuffers
#define glVertexAttribPointer   glad_glVertexAttribPointer
#define glEnableVertexAttribArray   glad_glEnableVertexAttribArray
#define glDisableVertexAttribArray  glad_glDisableVertexAttribArray

#define glDrawArrays            glad_glDrawArrays
#define glDrawElements          glad_glDrawElements

#define glPolygonOffset         glad_glPolygonOffset

#define glActiveTexture         glad_glActiveTexture
#define glGenerateMipmap        glad_glGenerateMipmap
#define glGenTextures           glad_glGenTextures
#define glBindTexture           glad_glBindTexture
#define glTexImage2D            glad_glTexImage2D
#define glTexParameteri         glad_glTexParameteri
#define glDeleteTextures        glad_glDeleteTextures

#endif // GLAD_H
