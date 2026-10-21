#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;

namespace mb {

// Input gathered by one pollEvents() call.
struct InputEvents {
    std::vector<std::uint32_t> keysPressed;  // SDL_Keycode values, in order
    float mouseDX = 0, mouseDY = 0;          // all pointer motion, in window points
    // The part of that motion made while a button was held. Each motion event is attributed to the
    // buttons held at that moment, so motion just before a press or just after a release does not
    // count, and a drag that spans several polls keeps counting. Two held buttons both count it.
    float leftDragDX = 0, leftDragDY = 0;
    float rightDragDX = 0, rightDragDY = 0;
    float middleDragDX = 0, middleDragDY = 0;
    bool leftButton = false, rightButton = false, middleButton = false;  // held at the end of the poll
    float wheel = 0;                         // vertical scroll, positive away from the user
};

// Owns SDL initialisation and the main window. The game runs at the original
// 4:3 1024x768 layout and scales to whatever size the window has.
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
    // Button state as of the last event seen, kept across polls.
    bool leftHeld_ = false, rightHeld_ = false, middleHeld_ = false;
};

}  // namespace mb
