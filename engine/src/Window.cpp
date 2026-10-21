#include "mb/engine/Window.h"

#include "mb/engine/Log.h"

#include <SDL3/SDL.h>

#include <stdexcept>

namespace mb {

Window::Window(const std::string& title, int width, int height) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }
    // A machine without an audio device can still play, just silently.
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        logWarn("audio unavailable: {}", SDL_GetError());
    }
    window_ = SDL_CreateWindow(title.c_str(), width, height,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_) {
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }
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
                    input.keysPressed.push_back(event.key.key);
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

void Window::setSize(int width, int height) {
    SDL_SetWindowSize(window_, width, height);
}

void Window::setFullscreen(bool fullscreen) {
    if (!SDL_SetWindowFullscreen(window_, fullscreen)) {
        logWarn("cannot switch fullscreen {}: {}", fullscreen ? "on" : "off", SDL_GetError());
    }
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

}  // namespace mb
