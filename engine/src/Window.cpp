#include "mb/engine/Window.h"

#include "mb/engine/Log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <stdexcept>

namespace mb {

namespace {

// The shortcuts this window handles itself.
bool isFullscreenShortcut(const SDL_KeyboardEvent& key) {
    switch (key.key) {
        case SDLK_F11:
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            return (key.mod & SDL_KMOD_ALT) != 0;
        default:
            return false;
    }
}

// On macOS, Ctrl+Cmd+F is the key equivalent of the "Toggle Full Screen" menu item SDL installs, and the
// system acts on it. SDL reports the key press as well, so toggling here too would switch twice. It is
// kept from the game, which would otherwise see a plain F.
bool isMenuFullscreenShortcut(const SDL_KeyboardEvent& key) {
#ifdef __APPLE__
    return key.key == SDLK_F && (key.mod & SDL_KMOD_CTRL) != 0 && (key.mod & SDL_KMOD_GUI) != 0;
#else
    (void)key;
    return false;
#endif
}

}  // namespace

Window::Window(const std::string& title, int width, int height, bool startFullscreen) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }
    // A window bigger than the screen is never wanted (a size remembered from another display, say).
    SDL_Rect usable{};
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) && usable.w > 0 && usable.h > 0) {
        width = std::min(width, usable.w);
        height = std::min(height, usable.h);
    }
    width = std::max(width, kMinWindowWidth);
    height = std::max(height, kMinWindowHeight);
    window_ = SDL_CreateWindow(title.c_str(), width, height,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                   (startFullscreen ? SDL_WINDOW_FULLSCREEN : 0));
    if (!window_) {
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }
    SDL_SetWindowMinimumSize(window_, kMinWindowWidth, kMinWindowHeight);
    windowedWidth_ = width;
    windowedHeight_ = height;
    fullscreenRequested_ = startFullscreen;
    int pixelsW = 0, pixelsH = 0;
    SDL_GetWindowSizeInPixels(window_, &pixelsW, &pixelsH);
    logInfo("Window: {}x{} points, {}x{} pixels (display scale {:.1f}){}", width, height, pixelsW, pixelsH,
            SDL_GetWindowDisplayScale(window_), startFullscreen ? ", fullscreen" : "");
}

Window::~Window() {
    if (window_) {
        SDL_DestroyWindow(window_);
    }
    SDL_Quit();
}

bool Window::pollEvents() {
    InputEvents ignored;
    return pollEvents(ignored);
}

bool Window::pollEvents(InputEvents& input) {
    input = {};
    // SDL reports the pointer in window points; the renderer's pixels differ on a high-density display.
    float pixelsPerPointX = 1, pixelsPerPointY = 1;
    int points = 0, pointsHigh = 0, pixels = 0, pixelsHigh = 0;
    if (SDL_GetWindowSize(window_, &points, &pointsHigh) && SDL_GetWindowSizeInPixels(window_, &pixels, &pixelsHigh) &&
        points > 0 && pointsHigh > 0) {
        pixelsPerPointX = static_cast<float>(pixels) / static_cast<float>(points);
        pixelsPerPointY = static_cast<float>(pixelsHigh) / static_cast<float>(pointsHigh);
    }
    const auto samplePointer = [&](float x, float y) {
        pointerX_ = x * pixelsPerPointX;
        pointerY_ = y * pixelsPerPointY;
        input.pointer.push_back({pointerX_, pointerY_, leftHeld_});
    };

    bool running = true;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE && escapeQuits_) {
                    running = false;
                } else if (!event.key.repeat) {
                    if (isFullscreenShortcut(event.key)) {
                        toggleFullscreen();
                    } else if (!isMenuFullscreenShortcut(event.key)) {
                        input.keysPressed.push_back(event.key.key);
                    }
                }
                break;
            case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
                logInfo("Window: fullscreen");
                break;
            case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
                fullscreenRequested_ = false;  // however it ended, for instance from the macOS menu
                logInfo("Window: windowed");
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                // Fullscreen reports the display's size, which is not one to go back to. SDL's Cocoa back end
                // sends it after the window is flagged fullscreen, and the windowed size back after the flag
                // is cleared; elsewhere it can come before the flag is set.
                if (!fullscreen() && !fullscreenRequested_ && event.window.data1 > 0 && event.window.data2 > 0) {
                    windowedWidth_ = event.window.data1;
                    windowedHeight_ = event.window.data2;
                }
                break;
            case SDL_EVENT_TEXT_INPUT:
                input.text += event.text.text;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                switch (event.button.button) {
                    case SDL_BUTTON_LEFT: leftHeld_ = down; break;
                    case SDL_BUTTON_RIGHT: rightHeld_ = down; break;
                    case SDL_BUTTON_MIDDLE: middleHeld_ = down; break;
                    default: break;
                }
                if (event.button.button == SDL_BUTTON_LEFT) {
                    samplePointer(event.button.x, event.button.y);
                }
                break;
            }
            case SDL_EVENT_MOUSE_MOTION: {
                const float dx = event.motion.xrel, dy = event.motion.yrel;
                input.mouseDX += dx;
                input.mouseDY += dy;
                if (leftHeld_) {
                    input.leftDragDX += dx;
                    input.leftDragDY += dy;
                }
                if (rightHeld_) {
                    input.rightDragDX += dx;
                    input.rightDragDY += dy;
                }
                if (middleHeld_) {
                    input.middleDragDX += dx;
                    input.middleDragDY += dy;
                }
                samplePointer(event.motion.x, event.motion.y);
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL:
                input.wheel += event.wheel.y;
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                // A release outside the window may never arrive; don't leave a drag or a press stuck on.
                if (leftHeld_) {
                    input.pointer.push_back({pointerX_, pointerY_, false});
                }
                leftHeld_ = rightHeld_ = middleHeld_ = false;
                break;
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                input.pointer.push_back({-1, -1, leftHeld_});  // off the window: nothing is hovered
                break;
            default:
                break;
        }
    }
    input.leftButton = leftHeld_;
    input.rightButton = rightHeld_;
    input.middleButton = middleHeld_;

    float x = 0, y = 0;
    SDL_GetMouseState(&x, &y);
    input.mouseX = x * pixelsPerPointX;
    input.mouseY = y * pixelsPerPointY;
    return running;
}

void Window::setTextInput(bool on) {
    if (on ? !SDL_StartTextInput(window_) : !SDL_StopTextInput(window_)) {
        logWarn("cannot {} text input: {}", on ? "start" : "stop", SDL_GetError());
    }
}

void Window::setTitle(const std::string& title) {
    SDL_SetWindowTitle(window_, title.c_str());
}

void Window::setCursorHidden(bool hidden) {
    if (hidden ? !SDL_HideCursor() : !SDL_ShowCursor()) {
        logWarn("cannot {} the cursor: {}", hidden ? "hide" : "show", SDL_GetError());
    }
    SDL_SetWindowMouseGrab(window_, hidden);
}

void Window::setSize(int width, int height) {
    SDL_SetWindowSize(window_, width, height);
}

bool Window::displaySize(int& width, int& height) const {
    SDL_Rect bounds{};
    if (!SDL_GetDisplayBounds(SDL_GetDisplayForWindow(window_), &bounds) || bounds.w <= 0 || bounds.h <= 0) {
        return false;
    }
    width = bounds.w;
    height = bounds.h;
    return true;
}

bool Window::fullscreen() const {
    return (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
}

void Window::setFullscreen(bool on) {
    fullscreenRequested_ = on;
    if (!SDL_SetWindowFullscreen(window_, on)) {
        logWarn("cannot switch {} fullscreen: {}", on ? "to" : "from", SDL_GetError());
        fullscreenRequested_ = false;
    }
}

void Window::windowedSize(int& width, int& height) const {
    width = windowedWidth_;
    height = windowedHeight_;
}

}  // namespace mb
