#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;

namespace mb {

// Owns SDL initialisation and the main window. The game runs at the original
// 4:3 1024x768 layout and scales to whatever size the window has.
// Input gathered by one pollEvents() call.
struct InputEvents {
    std::vector<std::uint32_t> keysPressed;  // SDL_Keycode values, in order
    float mouseDX = 0, mouseDY = 0;          // pointer motion, in window points
    bool leftButton = false, rightButton = false, middleButton = false;  // held at the end of the poll
    float wheel = 0;                         // vertical scroll, positive away from the user
};

class Window {
public:
    Window(const std::string& title, int width, int height);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Pumps OS events; returns false once the user asked to quit (window close or Escape).
    bool pollEvents();
    bool pollEvents(InputEvents& input);

    void setTitle(const std::string& title);

    SDL_Window* handle() const { return window_; }

private:
    SDL_Window* window_ = nullptr;
};

}  // namespace mb
