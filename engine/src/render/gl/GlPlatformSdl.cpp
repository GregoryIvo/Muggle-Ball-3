// Platform::* for MB3_RENDERER=GL: an SDL GL context on desktop OpenGL 2.1, compatibility profile, so the
// game's GL back end can be built and tested on a Mac (tests/render_test.cpp) before any Vita device run.
// SDL's own headers declare only GL 1.1 directly (SDL_opengl.h), so this file also loads every GL 1.5/2.0
// entry point GlFunctions.h declares, plus the GL_EXT_framebuffer_object group GlPlatform.h declares, with
// SDL_GL_GetProcAddress, the same technique for both.
#include "GlFunctions.h"
#include "GlPlatform.h"
#include "mb/engine/Log.h"
#include "mb/engine/Window.h"

#include <SDL3/SDL.h>

#include <cstring>

#if defined(MB3_RENDERER_GL)

// Definitions of the extern variables GlFunctions.h declares.
PFNGLGENBUFFERSPROC glGenBuffers = nullptr;
PFNGLDELETEBUFFERSPROC glDeleteBuffers = nullptr;
PFNGLBINDBUFFERPROC glBindBuffer = nullptr;
PFNGLBUFFERDATAPROC glBufferData = nullptr;
PFNGLBUFFERSUBDATAPROC glBufferSubData = nullptr;

PFNGLCREATESHADERPROC glCreateShader = nullptr;
PFNGLDELETESHADERPROC glDeleteShader = nullptr;
PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;
PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
PFNGLDELETEPROGRAMPROC glDeleteProgram = nullptr;
PFNGLATTACHSHADERPROC glAttachShader = nullptr;
PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation = nullptr;
PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = nullptr;
PFNGLUSEPROGRAMPROC glUseProgram = nullptr;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = nullptr;
PFNGLUNIFORM1IPROC glUniform1i = nullptr;
PFNGLUNIFORM1FPROC glUniform1f = nullptr;
PFNGLUNIFORM4FPROC glUniform4f = nullptr;
PFNGLUNIFORM4FVPROC glUniform4fv = nullptr;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;
PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = nullptr;

namespace mb::render::gl {

namespace {

SDL_Window* headlessWindow = nullptr;
SDL_GLContext context = nullptr;
Window* targetWindow = nullptr;
bool initedVideo = false;
bool npotMipmapsSupported = false;
int maxTexSize = 4096;

PFNGLGENFRAMEBUFFERSEXTPROC pGenFramebuffersExt = nullptr;
PFNGLDELETEFRAMEBUFFERSEXTPROC pDeleteFramebuffersExt = nullptr;
PFNGLBINDFRAMEBUFFEREXTPROC pBindFramebufferExt = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DEXTPROC pFramebufferTexture2DExt = nullptr;
PFNGLGENRENDERBUFFERSEXTPROC pGenRenderbuffersExt = nullptr;
PFNGLDELETERENDERBUFFERSEXTPROC pDeleteRenderbuffersExt = nullptr;
PFNGLBINDRENDERBUFFEREXTPROC pBindRenderbufferExt = nullptr;
PFNGLRENDERBUFFERSTORAGEEXTPROC pRenderbufferStorageExt = nullptr;
PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC pFramebufferRenderbufferExt = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC pCheckFramebufferStatusExt = nullptr;
PFNGLGENERATEMIPMAPEXTPROC pGenerateMipmapExt = nullptr;

template <class Proc>
Proc loadProc(const char* name) {
    return reinterpret_cast<Proc>(SDL_GL_GetProcAddress(name));
}

// GL_EXTENSIONS is a space-separated list; a name must match a whole word, not a prefix of a longer one.
bool hasExtension(const char* extensions, const char* name) {
    if (!extensions) {
        return false;
    }
    const std::size_t len = std::strlen(name);
    for (const char* p = std::strstr(extensions, name); p; p = std::strstr(p + 1, name)) {
        const bool atStart = p == extensions || p[-1] == ' ';
        const bool atEnd = p[len] == ' ' || p[len] == '\0';
        if (atStart && atEnd) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool Platform::init(Window* window) {
    if (context) {
        // A later Renderer may differ (headless vs. windowed) from whichever window the context is current
        // on right now: the header promises a harmless no-op, but without this the context would stay
        // current on the *old* target, so present()/renderToWindow would swap or draw into the wrong window
        // (or, if that window was since destroyed, into nothing at all).
        SDL_Window* handle = window ? window->handle() : headlessWindow;
        if (!handle) {
            // Was windowed before; this call wants headless and no headless window exists yet.
            handle = headlessWindow = SDL_CreateWindow("mb3 headless GL", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
            if (!handle) {
                logError("GlPlatformSdl: SDL_CreateWindow (headless) failed: {}", SDL_GetError());
                return false;
            }
        }
        if (!SDL_GL_MakeCurrent(handle, context)) {
            logError("GlPlatformSdl: SDL_GL_MakeCurrent failed: {}", SDL_GetError());
            return false;
        }
        targetWindow = window;
        return true;
    }

    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            logError("GlPlatformSdl: SDL_InitSubSystem(VIDEO) failed: {}", SDL_GetError());
            return false;
        }
        initedVideo = true;
    }

    // Attributes apply to the context SDL_GL_CreateContext makes below, whether or not the window (if any)
    // already exists.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* handle = window ? window->handle() : nullptr;
    if (!handle) {
        // Headless (tests, --screenshot): a hidden window of our own carries the context.
        headlessWindow = SDL_CreateWindow("mb3 headless GL", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (!headlessWindow) {
            logError("GlPlatformSdl: SDL_CreateWindow (headless) failed: {}", SDL_GetError());
            shutdown();  // undoes SDL_InitSubSystem(VIDEO) above if this call made it
            return false;
        }
        handle = headlessWindow;
    }

    context = SDL_GL_CreateContext(handle);
    if (!context) {
        logError("GlPlatformSdl: SDL_GL_CreateContext failed: {}", SDL_GetError());
        shutdown();
        return false;
    }
    if (!SDL_GL_MakeCurrent(handle, context)) {
        logError("GlPlatformSdl: SDL_GL_MakeCurrent failed: {}", SDL_GetError());
        shutdown();
        return false;
    }
    targetWindow = window;
    SDL_GL_SetSwapInterval(1);

#define MB3_LOAD(var) var = loadProc<decltype(var)>(#var)
    MB3_LOAD(glGenBuffers);
    MB3_LOAD(glDeleteBuffers);
    MB3_LOAD(glBindBuffer);
    MB3_LOAD(glBufferData);
    MB3_LOAD(glBufferSubData);
    MB3_LOAD(glCreateShader);
    MB3_LOAD(glDeleteShader);
    MB3_LOAD(glShaderSource);
    MB3_LOAD(glCompileShader);
    MB3_LOAD(glGetShaderiv);
    MB3_LOAD(glGetShaderInfoLog);
    MB3_LOAD(glCreateProgram);
    MB3_LOAD(glDeleteProgram);
    MB3_LOAD(glAttachShader);
    MB3_LOAD(glBindAttribLocation);
    MB3_LOAD(glLinkProgram);
    MB3_LOAD(glGetProgramiv);
    MB3_LOAD(glGetProgramInfoLog);
    MB3_LOAD(glUseProgram);
    MB3_LOAD(glGetUniformLocation);
    MB3_LOAD(glUniform1i);
    MB3_LOAD(glUniform1f);
    MB3_LOAD(glUniform4f);
    MB3_LOAD(glUniform4fv);
    MB3_LOAD(glUniformMatrix4fv);
    MB3_LOAD(glEnableVertexAttribArray);
    MB3_LOAD(glDisableVertexAttribArray);
    MB3_LOAD(glVertexAttribPointer);
#undef MB3_LOAD
    if (!glGenBuffers || !glBufferData || !glCreateShader || !glCreateProgram || !glUseProgram ||
        !glUniformMatrix4fv || !glVertexAttribPointer || !glDisableVertexAttribArray) {
        logError("GlPlatformSdl: a required GL 1.5/2.0 entry point is missing");
        shutdown();  // leaves `context` null, so a later init() tries again instead of running on broken GL
        return false;
    }

    pGenFramebuffersExt = loadProc<PFNGLGENFRAMEBUFFERSEXTPROC>("glGenFramebuffersEXT");
    pDeleteFramebuffersExt = loadProc<PFNGLDELETEFRAMEBUFFERSEXTPROC>("glDeleteFramebuffersEXT");
    pBindFramebufferExt = loadProc<PFNGLBINDFRAMEBUFFEREXTPROC>("glBindFramebufferEXT");
    pFramebufferTexture2DExt = loadProc<PFNGLFRAMEBUFFERTEXTURE2DEXTPROC>("glFramebufferTexture2DEXT");
    pGenRenderbuffersExt = loadProc<PFNGLGENRENDERBUFFERSEXTPROC>("glGenRenderbuffersEXT");
    pDeleteRenderbuffersExt = loadProc<PFNGLDELETERENDERBUFFERSEXTPROC>("glDeleteRenderbuffersEXT");
    pBindRenderbufferExt = loadProc<PFNGLBINDRENDERBUFFEREXTPROC>("glBindRenderbufferEXT");
    pRenderbufferStorageExt = loadProc<PFNGLRENDERBUFFERSTORAGEEXTPROC>("glRenderbufferStorageEXT");
    pFramebufferRenderbufferExt = loadProc<PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC>("glFramebufferRenderbufferEXT");
    pCheckFramebufferStatusExt = loadProc<PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC>("glCheckFramebufferStatusEXT");
    pGenerateMipmapExt = loadProc<PFNGLGENERATEMIPMAPEXTPROC>("glGenerateMipmapEXT");
    if (!pGenFramebuffersExt || !pBindFramebufferExt || !pFramebufferTexture2DExt || !pGenRenderbuffersExt ||
        !pRenderbufferStorageExt || !pFramebufferRenderbufferExt || !pCheckFramebufferStatusExt ||
        !pGenerateMipmapExt) {
        logError("GlPlatformSdl: GL_EXT_framebuffer_object is required but this context does not have it");
        shutdown();
        return false;
    }

    const auto* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    npotMipmapsSupported = hasExtension(extensions, "GL_ARB_texture_non_power_of_two");
    GLint maxSize = 4096;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    maxTexSize = maxSize;

    logInfo("GL: {} ({}), GLSL {}, NPOT mipmaps {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
            reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)),
            npotMipmapsSupported ? "on" : "off");
    return true;
}

void Platform::shutdown() {
    if (context) {
        SDL_GL_DestroyContext(context);
        context = nullptr;
    }
    if (headlessWindow) {
        SDL_DestroyWindow(headlessWindow);
        headlessWindow = nullptr;
    }
    if (initedVideo) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        initedVideo = false;
    }
    targetWindow = nullptr;
}

void Platform::present(bool) {
    // The desktop never shows a common dialog over the GL back end: the parameter is vitaGL's alone.
    if (targetWindow) {
        SDL_GL_SwapWindow(targetWindow->handle());
    } else if (headlessWindow) {
        SDL_GL_SwapWindow(headlessWindow);
    }
}

void Platform::drawableSize(int& width, int& height) {
    width = height = 0;
    if (targetWindow) {
        SDL_GetWindowSizeInPixels(targetWindow->handle(), &width, &height);
    }
}

const char* Platform::name() { return "OpenGL 2.1"; }

const char* Platform::preamble(bool /*fragmentShader*/) {
    // GLSL 1.20 has no precision qualifiers; the empty defines let the ES 1.00 bodies' lowp/mediump/highp
    // compile unchanged on both stages.
    return "#version 120\n#define lowp\n#define mediump\n#define highp\n";
}

bool Platform::npotMipmaps() { return npotMipmapsSupported; }

int Platform::maxTextureSize() { return maxTexSize; }

void genFramebuffers(GLsizei n, GLuint* ids) { pGenFramebuffersExt(n, ids); }
void deleteFramebuffers(GLsizei n, const GLuint* ids) { pDeleteFramebuffersExt(n, ids); }
void bindFramebuffer(GLuint id) { pBindFramebufferExt(GL_FRAMEBUFFER_EXT, id); }
void framebufferTexture2D(GLenum attachment, GLuint texture) {
    pFramebufferTexture2DExt(GL_FRAMEBUFFER_EXT, attachment, GL_TEXTURE_2D, texture, 0);
}
void genRenderbuffers(GLsizei n, GLuint* ids) { pGenRenderbuffersExt(n, ids); }
void deleteRenderbuffers(GLsizei n, const GLuint* ids) { pDeleteRenderbuffersExt(n, ids); }
void bindRenderbuffer(GLuint id) { pBindRenderbufferExt(GL_RENDERBUFFER_EXT, id); }
void renderbufferStorage(GLenum format, GLsizei width, GLsizei height) {
    pRenderbufferStorageExt(GL_RENDERBUFFER_EXT, format, width, height);
}
void framebufferRenderbuffer(GLenum attachment, GLuint renderbuffer) {
    pFramebufferRenderbufferExt(GL_FRAMEBUFFER_EXT, attachment, GL_RENDERBUFFER_EXT, renderbuffer);
}
GLenum checkFramebufferStatus() { return pCheckFramebufferStatusExt(GL_FRAMEBUFFER_EXT); }
void generateMipmap() { pGenerateMipmapExt(GL_TEXTURE_2D); }

}  // namespace mb::render::gl

#endif  // MB3_RENDERER_GL
