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
    bool running = true;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) {
                    running = false;
                } else if (!event.key.repeat) {
                    input.keysPressed.push_back(event.key.key);
                }
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
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL:
                input.wheel += event.wheel.y;
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                // A release outside the window may never arrive; don't leave a drag stuck on.
                leftHeld_ = rightHeld_ = middleHeld_ = false;
                break;
            default:
                break;
        }
    }
    input.leftButton = leftHeld_;
    input.rightButton = rightHeld_;
    input.middleButton = middleHeld_;
    return running;
}

void Window::setTitle(const std::string& title) {
    SDL_SetWindowTitle(window_, title.c_str());
}

}  // namespace mb
