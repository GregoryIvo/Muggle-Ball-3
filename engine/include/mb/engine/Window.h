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

// The smallest window the user can drag to, and the smallest size worth remembering.
inline constexpr int kMinWindowWidth = 320;
inline constexpr int kMinWindowHeight = 240;

// Owns SDL initialisation and the main window. The game runs at the original
// 4:3 1024x768 layout and scales to whatever size the window has.
class Window {
public:
    // `width` x `height` is the windowed size in points (not pixels: a high-DPI display shows more pixels),
    // limited to the primary display's usable area. With `startFullscreen` the window starts fullscreen
    // and goes back to that size when it leaves it.
    Window(const std::string& title, int width, int height, bool startFullscreen = false);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Pumps OS events; returns false once the user asked to quit (window close or Escape).
    // F11 and Alt+Enter toggle fullscreen here and never reach `keysPressed`. On macOS, Ctrl+Cmd+F does
    // too, but through the "Toggle Full Screen" menu item SDL installs; it does not reach `keysPressed` either.
    bool pollEvents();
    bool pollEvents(InputEvents& input);

    void setTitle(const std::string& title);

    // Desktop fullscreen: the window covers the display at the display's own size, so the 3D view keeps
    // its vertical field of view and shows more at the sides on a wider screen. On macOS this is a
    // fullscreen Space, which takes a moment, so fullscreen() changes only once it has begun.
    bool fullscreen() const;
    void setFullscreen(bool on);
    void toggleFullscreen() { setFullscreen(!fullscreen()); }

    // The window's size in points as it was last not fullscreen: what to restore, and what to remember
    // for the next run.
    void windowedSize(int& width, int& height) const;

    SDL_Window* handle() const { return window_; }

private:
    SDL_Window* window_ = nullptr;
    int windowedWidth_ = 0, windowedHeight_ = 0;
    // We asked for fullscreen and have not seen it end. The window is flagged fullscreen only once the
    // switch has finished, and the resize to the display's size can come before that.
    bool fullscreenRequested_ = false;
    // Button state as of the last event seen, kept across polls.
    bool leftHeld_ = false, rightHeld_ = false, middleHeld_ = false;
};

}  // namespace mb
