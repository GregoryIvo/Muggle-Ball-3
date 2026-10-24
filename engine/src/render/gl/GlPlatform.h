#pragma once

// The one file in the GL back end that knows which OpenGL it runs on:
//   MB3_RENDERER=VITAGL  vitaGL on the PlayStation Vita (GLSL ES 1.00 compiled on the console by libshacccg);
//   MB3_RENDERER=GL      a desktop OpenGL 2.1 compatibility context through SDL, so the same back end can be run
//                        and tested on the Mac (tests/render_test.cpp) before any device run.
// Everything else under engine/src/render/gl/ is written against the subset both offer: an ES 2.0-style
// programmable pipeline (vertex buffers, shaders, textures with generated mipmaps, framebuffer objects for
// renderToImage), with no fixed function, no UBOs, no VAOs, no sRGB and no instancing. The two platform files
// (GlPlatformVita.cpp, GlPlatformSdl.cpp) implement this header; exactly one is compiled.

#if defined(MB3_RENDERER_VITAGL)
#include <vitaGL.h>
#elif defined(MB3_RENDERER_GL)
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
#else
#error "GlPlatform.h is for MB3_RENDERER=GL or VITAGL only"
#endif

#include <cstdint>

namespace mb {
class Window;
}

namespace mb::render::gl {

// Framebuffer objects and mipmap generation are core names on vitaGL and EXT names on desktop GL 2.1
// (GL_EXT_framebuffer_object); the enum values are the same. The back end calls these and nothing else for
// those features.
#if defined(MB3_RENDERER_VITAGL)
inline void genFramebuffers(GLsizei n, GLuint* ids) { glGenFramebuffers(n, ids); }
inline void deleteFramebuffers(GLsizei n, const GLuint* ids) { glDeleteFramebuffers(n, ids); }
inline void bindFramebuffer(GLuint id) { glBindFramebuffer(GL_FRAMEBUFFER, id); }
inline void framebufferTexture2D(GLenum attachment, GLuint texture) {
    glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, texture, 0);
}
inline void genRenderbuffers(GLsizei n, GLuint* ids) { glGenRenderbuffers(n, ids); }
inline void deleteRenderbuffers(GLsizei n, const GLuint* ids) { glDeleteRenderbuffers(n, ids); }
inline void bindRenderbuffer(GLuint id) { glBindRenderbuffer(GL_RENDERBUFFER, id); }
inline void renderbufferStorage(GLenum format, GLsizei width, GLsizei height) {
    glRenderbufferStorage(GL_RENDERBUFFER, format, width, height);
}
inline void framebufferRenderbuffer(GLenum attachment, GLuint renderbuffer) {
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, renderbuffer);
}
inline GLenum checkFramebufferStatus() { return glCheckFramebufferStatus(GL_FRAMEBUFFER); }
inline void generateMipmap() { glGenerateMipmap(GL_TEXTURE_2D); }
#else
// Desktop GL 2.1 gets these through SDL_GL_GetProcAddress in GlPlatformSdl.cpp (Platform::init loads them):
// SDL's glext header declares the EXT entry points only as typedefs unless GL_GLEXT_PROTOTYPES is set, and
// Linux's libGL exports them only by lookup, so direct calls would not link everywhere the desktop build runs.
void genFramebuffers(GLsizei n, GLuint* ids);
void deleteFramebuffers(GLsizei n, const GLuint* ids);
void bindFramebuffer(GLuint id);
void framebufferTexture2D(GLenum attachment, GLuint texture);
void genRenderbuffers(GLsizei n, GLuint* ids);
void deleteRenderbuffers(GLsizei n, const GLuint* ids);
void bindRenderbuffer(GLuint id);
void renderbufferStorage(GLenum format, GLsizei width, GLsizei height);
void framebufferRenderbuffer(GLenum attachment, GLuint renderbuffer);
GLenum checkFramebufferStatus();
void generateMipmap();
#endif

// The display, the context and what the target can do. All static: there is one GL context per process on
// both targets.
struct Platform {
    // Brings up GL. `window` null means headless (tests, screenshots): on the desktop a hidden window of the
    // platform's own carries the context; on the Vita the screen is initialised either way (vitaGL owns the
    // display: vglInitExtended(0, 960, 544, threshold, SCE_GXM_MULTISAMPLE_NONE), then eglSwapInterval 2).
    // Returns false after logging why; a second call is a harmless no-op that returns true.
    static bool init(Window* window);
    static void shutdown();

    // Presents the back buffer. `commonDialogShown` is vitaGL's vglSwapBuffers(GL_TRUE), which the console
    // needs while its IME dialog is up (SDL never updates the common dialog itself); the desktop ignores it.
    static void present(bool commonDialogShown);

    // The drawable area in pixels: the window's (0 x 0 when headless) on the desktop, 960 x 544 on the Vita.
    static void drawableSize(int& width, int& height);

    static const char* name();  // "vitaGL" or "OpenGL 2.1", for Renderer::backendName()

    // What goes in front of every shader body in engine/shaders/gl/ before glShaderSource:
    //   vitaGL:  "#version 100\n" (+ "precision mediump float;\n" for a fragment shader);
    //   desktop: "#version 120\n#define lowp\n#define mediump\n#define highp\n" (GLSL 1.20 has no precision
    //            qualifiers and otherwise accepts the ES 1.00 bodies).
    static const char* preamble(bool fragmentShader);

    // Whether a non-power-of-two texture may have mipmaps and REPEAT. Desktop GL 2.1 with
    // GL_ARB_texture_non_power_of_two: yes. vitaGL: no (its mip generator truncates odd sizes and stops
    // early), so createTexture resamples such an image to the next power of two before upload.
    static bool npotMipmaps();

    // The largest texture side the target takes (GL_MAX_TEXTURE_SIZE; 4096 on the Vita).
    static int maxTextureSize();
};

}  // namespace mb::render::gl
