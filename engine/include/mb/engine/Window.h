#pragma once

#include <cstdint>
#include <string>
#include <vector>

#ifdef MB3_PLATFORM_VITA
#include <chrono>
#include <memory>
#endif

struct SDL_Window;
#ifdef MB3_PLATFORM_VITA
struct SDL_Joystick;
#endif

namespace mb {

#ifdef MB3_PLATFORM_VITA
class ConsoleInput;
#endif

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

    // Pumps OS events; returns false once the user asked to quit (window close or, unless setEscapeQuits(false), Escape).
    // F11 and Alt+Enter toggle fullscreen here and never reach `keysPressed`. On macOS, Ctrl+Cmd+F does
    // too, but through the "Toggle Full Screen" menu item SDL installs; it does not reach `keysPressed` either.
    bool pollEvents();
    bool pollEvents(InputEvents& input);

    // Escape closes the window by default. The menus turn that off to use the key themselves: it then arrives in
    // InputEvents::keysPressed like any other.
    void setEscapeQuits(bool quits) { escapeQuits_ = quits; }
    // Typed characters arrive in InputEvents::text only while this is on (it also shows the system's input method).
    void setTextInput(bool on);

    void setTitle(const std::string& title);
    // While a level is played the pointer is the bat: the system cursor is hidden and kept inside the window, so that a
    // flick past the edge does not lose the bat. False shows and frees it again (the menus, the pause screen).
    void setCursorHidden(bool hidden);
    // The window's size in points (the unit of the constructor's size).
    void setSize(int width, int height);
    // The size in points of the display the window is on; false if SDL cannot tell.
    bool displaySize(int& width, int& height) const;

    // On the Vita, pumps the window system's events without reading them, for a long load: SDL ticks the
    // power timer in its own event pump, which is what keeps the console from sleeping while nothing else
    // is. A no-op everywhere else, where loading does not risk a sleep and there is nothing to gain from it;
    // safe to call unconditionally either way.
    void keepAwake();

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
    bool escapeQuits_ = true;
    int windowedWidth_ = 0, windowedHeight_ = 0;
    // We asked for fullscreen and have not seen it end. The window is flagged fullscreen only once the
    // switch has finished, and the resize to the display's size can come before that.
    bool fullscreenRequested_ = false;
    // Button state as of the last event seen, kept across polls.
    bool leftHeld_ = false, rightHeld_ = false, middleHeld_ = false;
    float pointerX_ = 0, pointerY_ = 0;  // the last pointer position seen in an event, window pixels

#ifdef MB3_PLATFORM_VITA
    // The buttons, stick and touch screen, turned into InputEvents (ConsoleInput.h, docs/VITA_PLAN.md 5.8).
    std::unique_ptr<ConsoleInput> console_;
    SDL_Joystick* joystick_ = nullptr;
    std::uint64_t frontTouchId_ = 0;  // the front panel's SDL touch device id; 0 is never a real one
    bool frontTouchKnown_ = false;
    bool textInputOn_ = false;  // remembered so a confirm press can reopen a cancelled, one-shot IME
    bool cursorHidden_ = false;  // as last set by setCursorHidden(): a level is playing while this is true
    float stickAxisX_ = 0, stickAxisY_ = 0;  // the left stick's last reported axes, each updated independently
    std::chrono::steady_clock::time_point lastPoll_{};  // for frame()'s dt; unset until the first poll
#endif
};

}  // namespace mb
