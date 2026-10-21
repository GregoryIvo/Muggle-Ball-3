// Fullscreen shortcuts and the remembered windowed size, on SDL's dummy video driver (no display needed).

#include "mb/engine/Window.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <memory>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void pushKey(SDL_Keycode key, SDL_Keymod mod = SDL_KMOD_NONE, bool repeat = false) {
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = key;
    e.key.mod = mod;
    e.key.down = true;
    e.key.repeat = repeat;
    SDL_PushEvent(&e);
}

void pushResize(int w, int h) {
    SDL_Event e{};
    e.type = SDL_EVENT_WINDOW_RESIZED;
    e.window.data1 = w;
    e.window.data2 = h;
    SDL_PushEvent(&e);
}

struct Size {
    int w = 0, h = 0;
};

Size windowed(const mb::Window& window) {
    Size s;
    window.windowedSize(s.w, s.h);
    return s;
}

void testShortcuts() {
    mb::Window window("test", 640, 480);
    mb::InputEvents input;
    check(!window.fullscreen(), "starts windowed");

    struct Shortcut {
        const char* what;
        SDL_Keycode key;
        SDL_Keymod mod;
    };
    const Shortcut shortcuts[] = {
        {"F11", SDLK_F11, SDL_KMOD_NONE},
        {"Alt+Enter", SDLK_RETURN, SDL_KMOD_LALT},
        {"Alt+keypad Enter", SDLK_KP_ENTER, SDL_KMOD_RALT},
    };
    for (const Shortcut& s : shortcuts) {
        pushKey(s.key, s.mod);
        check(window.pollEvents(input), s.what);
        check(window.fullscreen(), s.what);
        check(input.keysPressed.empty(), "a shortcut is not passed on as a key press");
        pushKey(s.key, s.mod);
        window.pollEvents(input);
        check(!window.fullscreen(), "the same shortcut leaves fullscreen");
    }

    pushKey(SDLK_RETURN);
    pushKey(SDLK_F, SDL_KMOD_LCTRL);
    pushKey(SDLK_F, SDL_KMOD_LGUI);
    pushKey(SDLK_A);
    window.pollEvents(input);
    check(!window.fullscreen() && input.keysPressed.size() == 4, "Enter, Ctrl+F, Cmd+F and A are ordinary keys");

    // Ctrl+Cmd+F: on macOS the system's menu item toggles, so the window must not (it would switch twice).
    pushKey(SDLK_F, static_cast<SDL_Keymod>(SDL_KMOD_LCTRL | SDL_KMOD_LGUI));
    window.pollEvents(input);
    check(!window.fullscreen(), "Ctrl+Cmd+F is not acted on by the window");
#ifdef __APPLE__
    check(input.keysPressed.empty(), "and is not passed on as an F");
#else
    check(input.keysPressed.size() == 1, "and is an ordinary key where there is no such menu");
#endif

    pushKey(SDLK_F11, SDL_KMOD_NONE, true);
    window.pollEvents(input);
    check(!window.fullscreen(), "a held key does not repeat the toggle");

    pushKey(SDLK_F11);
    pushKey(SDLK_F11);
    window.pollEvents(input);
    check(!window.fullscreen(), "two presses in one poll cancel out");
}

void testWindowedSize() {
    mb::Window window("test", 640, 480);
    Size s = windowed(window);
    check(s.w == 640 && s.h == 480, "the creation size is the windowed size");

    mb::InputEvents input;
    pushResize(800, 600);
    window.pollEvents(input);
    s = windowed(window);
    check(s.w == 800 && s.h == 600, "a resize is remembered");

    window.setFullscreen(true);
    SDL_SyncWindow(window.handle());  // the switch may take effect later, e.g. after an animation
    check(window.fullscreen(), "setFullscreen(true)");
    pushResize(1920, 1080);  // the display's size, reported while fullscreen
    window.pollEvents(input);
    s = windowed(window);
    check(s.w == 800 && s.h == 600, "the fullscreen size is not remembered");

    window.setFullscreen(false);
    SDL_SyncWindow(window.handle());
    check(!window.fullscreen(), "setFullscreen(false)");
    pushResize(700, 500);
    pushResize(0, 0);
    pushResize(-5, 300);
    window.pollEvents(input);
    s = windowed(window);
    check(s.w == 700 && s.h == 500, "the size after leaving fullscreen is remembered, empty sizes are not");
}

void testResizeBeforeTheSwitchHasFinished() {
    mb::InputEvents input;
    {
        // The display-sized resize can arrive before the window is flagged fullscreen.
        mb::Window window("test", 640, 480, true);
        pushResize(1512, 845);
        window.pollEvents(input);
        const Size s = windowed(window);
        check(s.w == 640 && s.h == 480, "a resize while starting fullscreen is not the windowed size");
    }
    {
        // Leaving by some other means than setFullscreen (the macOS menu) must let resizes through again.
        mb::Window window("test", 640, 480);
        window.setFullscreen(true);
        SDL_SyncWindow(window.handle());
        SDL_SetWindowFullscreen(window.handle(), false);
        SDL_SyncWindow(window.handle());
        window.pollEvents(input);
        pushResize(700, 500);
        window.pollEvents(input);
        const Size s = windowed(window);
        check(!window.fullscreen() && s.w == 700 && s.h == 500, "resizes count again after fullscreen ended on its own");
    }
}

void testCreation() {
    {
        mb::Window window("test", 640, 480, true);
        SDL_SyncWindow(window.handle());
        const Size s = windowed(window);
        check(window.fullscreen() && s.w == 640 && s.h == 480, "starting fullscreen keeps the windowed size to return to");
    }
    {
        mb::Window window("test", 10, 10);
        const Size s = windowed(window);
        check(s.w == mb::kMinWindowWidth && s.h == mb::kMinWindowHeight, "a tiny size is raised to the minimum");
        int w = 0, h = 0;
        SDL_GetWindowSize(window.handle(), &w, &h);
        check(w == mb::kMinWindowWidth && h == mb::kMinWindowHeight, "and so is the window");
    }
    {
        mb::Window window("test", 100000, 100000);
        SDL_Rect usable{};
        const bool known = SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable);
        const Size s = windowed(window);
        check(known && s.w <= usable.w && s.h <= usable.h, "a window is never bigger than the display");
    }
}

}  // namespace

int main() {
    // Environment variables rather than hints: every Window ends with SDL_Quit, which forgets the hints.
    SDL_setenv_unsafe("SDL_VIDEO_DRIVER", "dummy", 1);
    SDL_setenv_unsafe("SDL_AUDIO_DRIVER", "dummy", 1);
    try {
        testShortcuts();
        testWindowedSize();
        testResizeBeforeTheSwitchHasFinished();
        testCreation();
    } catch (const std::exception& e) {
        std::printf("SKIP: no window on the dummy video driver (%s)\n", e.what());
        return 77;
    }
    if (failures == 0) {
        std::puts("all window tests passed");
    }
    return failures == 0 ? 0 : 1;
}
