#pragma once

// GL 1.5 (buffers) and GL 2.0 (shaders, programs, vertex attributes) entry points. vitaGL.h declares every
// one of these directly (it only implements the programmable pipeline, so they are simply core to it); on
// desktop GL 2.1 SDL's SDL_opengl.h declares only GL 1.1, and SDL_opengl_glext.h's declarations for anything
// past that are typedefs only (GL_GLEXT_PROTOTYPES is never defined, matching GlPlatform.h's comment on the
// framebuffer group, which needs the same treatment for the same reason). GlPlatformSdl.cpp loads these
// with SDL_GL_GetProcAddress at Platform::init(), into variables named exactly like the GL functions, so
// everything else under engine/src/render/gl/ calls them the ordinary way on both targets.
#include "GlPlatform.h"

#if defined(MB3_RENDERER_GL)

extern PFNGLGENBUFFERSPROC glGenBuffers;
extern PFNGLDELETEBUFFERSPROC glDeleteBuffers;
extern PFNGLBINDBUFFERPROC glBindBuffer;
extern PFNGLBUFFERDATAPROC glBufferData;
extern PFNGLBUFFERSUBDATAPROC glBufferSubData;

extern PFNGLCREATESHADERPROC glCreateShader;
extern PFNGLDELETESHADERPROC glDeleteShader;
extern PFNGLSHADERSOURCEPROC glShaderSource;
extern PFNGLCOMPILESHADERPROC glCompileShader;
extern PFNGLGETSHADERIVPROC glGetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
extern PFNGLCREATEPROGRAMPROC glCreateProgram;
extern PFNGLDELETEPROGRAMPROC glDeleteProgram;
extern PFNGLATTACHSHADERPROC glAttachShader;
extern PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation;
extern PFNGLLINKPROGRAMPROC glLinkProgram;
extern PFNGLGETPROGRAMIVPROC glGetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
extern PFNGLUSEPROGRAMPROC glUseProgram;
extern PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
extern PFNGLUNIFORM1IPROC glUniform1i;
extern PFNGLUNIFORM1FPROC glUniform1f;
extern PFNGLUNIFORM4FPROC glUniform4f;
extern PFNGLUNIFORM4FVPROC glUniform4fv;
extern PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
extern PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;

#endif  // MB3_RENDERER_GL
