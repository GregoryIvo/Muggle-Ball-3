#pragma once

#include <string>

struct SDL_Window;

namespace mb {

// Owns SDL initialisation and the main window. The game runs at the original
// 4:3 1024x768 layout and scales to whatever size the window has.
class Window {
public:
    Window(const std::string& title, int width, int height);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Pumps OS events; returns false once the user asked to quit.
    bool pollEvents();

    SDL_Window* handle() const { return window_; }

private:
    SDL_Window* window_ = nullptr;
};

}  // namespace mb
