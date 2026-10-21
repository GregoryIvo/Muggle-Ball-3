#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;

namespace mb {

// One pointer event as SDL reported it: where the pointer was, in window pixels ((-1, -1) once it has left the
// window), and whether the left button was down just after it. The 2D UI replays these in order, so a click that
// starts and ends within one poll still shows up as a press and a release, each at its own position.
struct PointerSample {
    float x = 0, y = 0;
    bool leftDown = false;
};

// Input gathered by one pollEvents() call.
struct InputEvents {
    std::vector<std::uint32_t> keysPressed;  // SDL_Keycode values, in order, key repeats left out
    std::string text;                        // UTF-8 typed since the last poll; only while setTextInput(true)
    std::vector<PointerSample> pointer;      // every mouse move and left-button change, oldest first
    float mouseDX = 0, mouseDY = 0;          // all pointer motion, in window points
    // Where the pointer is now, in window pixels (the units of Renderer::drawableSize, origin top-left): the
    // position the 2D UI hit-tests against. Not reset by a poll; it is whatever SDL reports for this window.
    float mouseX = 0, mouseY = 0;
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

    // Pumps OS events; returns false once the user asked to quit (window close or, unless setEscapeQuits(false), Escape).
    bool pollEvents();
    bool pollEvents(InputEvents& input);

    // Escape closes the window by default. The menus turn that off to use the key themselves: it then arrives in
    // InputEvents::keysPressed like any other.
    void setEscapeQuits(bool quits) { escapeQuits_ = quits; }
    // Typed characters arrive in InputEvents::text only while this is on (it also shows the system's input method).
    void setTextInput(bool on);

    void setTitle(const std::string& title);
    // The window's size in points (the unit of the constructor's size), or its fullscreen state. Fullscreen covers
    // the desktop without changing the display mode.
    void setSize(int width, int height);
    void setFullscreen(bool fullscreen);
    // The size in points of the display the window is on; false if SDL cannot tell.
    bool displaySize(int& width, int& height) const;

    SDL_Window* handle() const { return window_; }

private:
    SDL_Window* window_ = nullptr;
    bool escapeQuits_ = true;
    // Button state as of the last event seen, kept across polls.
    bool leftHeld_ = false, rightHeld_ = false, middleHeld_ = false;
    float pointerX_ = 0, pointerY_ = 0;  // the last pointer position seen in an event, window pixels
};

}  // namespace mb
