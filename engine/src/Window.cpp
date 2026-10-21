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
            case SDL_EVENT_MOUSE_MOTION:
                input.mouseDX += event.motion.xrel;
                input.mouseDY += event.motion.yrel;
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                input.wheel += event.wheel.y;
                break;
            default:
                break;
        }
    }
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(nullptr, nullptr);
    input.leftButton = (buttons & SDL_BUTTON_LMASK) != 0;
    input.rightButton = (buttons & SDL_BUTTON_RMASK) != 0;
    input.middleButton = (buttons & SDL_BUTTON_MMASK) != 0;
    return running;
}

void Window::setTitle(const std::string& title) {
    SDL_SetWindowTitle(window_, title.c_str());
}

}  // namespace mb
