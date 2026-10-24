// Platform::* for MB3_RENDERER=VITAGL: vitaGL owns the display (SDL's video driver touches no GXM, so SDL is
// free for events, touch and audio; see docs/VITA.md "Decisions and findings"). The numbers here (screen
// size, RAM threshold, swap interval) match the M1a hello, which found them on the device.
#include "GlPlatform.h"
#include "mb/engine/Log.h"
#include "mb/engine/Window.h"

#include <vitaGL.h>

#if defined(MB3_RENDERER_VITAGL)

namespace mb::render::gl {

namespace {

constexpr int kScreenWidth = 960;
constexpr int kScreenHeight = 544;
// vglInitExtended takes all of CDRAM and PHYCONT plus the free RAM above this threshold (the hello's value;
// the game's own budget is decided in M6b).
constexpr int kVitaGlRamThreshold = 16 * 1024 * 1024;
constexpr int kSwapInterval = 2;  // two vblanks: the 30 fps target

bool initialised = false;

const char* glString(GLenum name) {
    const auto* value = reinterpret_cast<const char*>(glGetString(name));
    return value ? value : "(null)";
}

}  // namespace

bool Platform::init(Window*) {
    if (initialised) {
        return true;  // a second call is a harmless no-op, as the header promises
    }
    // The return value is not success: it is GL_TRUE only when vitaGL had to clamp the resolution, GL_FALSE
    // on a normal start (the hello's comment on vglInitWithCustomSizes). A failed start shows up as null GL
    // strings, which the log below would show.
    vglInitExtended(0, kScreenWidth, kScreenHeight, kVitaGlRamThreshold, SCE_GXM_MULTISAMPLE_NONE);
    eglSwapInterval(EGL_NO_DISPLAY, kSwapInterval);
    initialised = true;

    logInfo("vitaGL: GL_VERSION '{}', GL_RENDERER '{}', GLSL '{}'", glString(GL_VERSION), glString(GL_RENDERER),
            glString(GL_SHADING_LANGUAGE_VERSION));
    return true;
}

void Platform::shutdown() {
    // vitaGL has no shutdown call (docs/VITA.md "Decisions and findings"); the process exit releases GXM.
    initialised = false;
}

void Platform::present(bool commonDialogShown) {
    // SDL never calls sceCommonDialogUpdate itself, so the app must present with has_commondialog = GL_TRUE
    // while the IME is up, or the dialog never appears or the app hangs (docs/VITA.md).
    vglSwapBuffers(commonDialogShown ? GL_TRUE : GL_FALSE);
}

void Platform::drawableSize(int& width, int& height) {
    // Always the screen size, headless or not: vitaGL owns one display, not a per-Renderer window.
    width = kScreenWidth;
    height = kScreenHeight;
}

const char* Platform::name() { return "vitaGL"; }

const char* Platform::preamble(bool fragmentShader) {
    return fragmentShader ? "#version 100\nprecision mediump float;\n" : "#version 100\n";
}

bool Platform::npotMipmaps() { return false; }

int Platform::maxTextureSize() { return 4096; }

}  // namespace mb::render::gl

#endif  // MB3_RENDERER_VITAGL
