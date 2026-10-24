#pragma once

// A console's buttons, sticks and touch screen turned into what the game reads: the pointer (a virtual cursor that
// is the bat in play and the mouse in the menus), the left button (confirm) and a few keys (back, Enter). The
// rules are docs/VITA_PLAN.md section 5.8. This is platform-neutral on purpose, so the host's unit tests cover
// every rule (tests/test_console_input.cpp); Window.cpp on the Vita only feeds it SDL's joystick and touch events
// and copies its output into InputEvents. Nothing in game/ knows it exists.
//
// The rules:
//   - Confirm (Cross, or Circle when the system's enter-button setting says so) is the left mouse button at the
//     cursor, held for as long as the button is held: it clicks in menus, drags sliders, launches the ball (the
//     press edge) and resumes from the pause screen. While text input is on it sends Enter instead (the IME's
//     confirmation is a Return already, this is for the on-screen field).
//   - Back (the other of the two) is Escape: pause in play, back in menus, quit to the menu when paused. Start is
//     Escape too (confirm with the user in M5b whether Start while paused should resume instead).
//   - The left stick and the d-pad move the cursor: speed in pixels per second scaled by the stick's deflection,
//     the d-pad at full speed; the cursor stays inside the target.
//   - Front touch in the menus: a finger down is a press at the finger, its motion a drag, lifting it a release.
//   - Front touch in play (the cursor hidden, Window::setCursorHidden(true)): a finger moving sets the cursor only
//     and never presses, so a hand landing on the screen does not launch; a short tap (released within
//     kTapSeconds, moved less than kTapPixels) is a press and a release at the finger, which launches.
//   - Any key the game does not know is `Other`, which the pause screen takes as "carry on".

#include "mb/engine/Window.h"

#include <cstdint>
#include <optional>

namespace mb {

// SDL's positional buttons for the Vita's joystick (src/joystick/vita/SDL_sysjoystick.c in SDL 3.4.16; its built-in
// "PSVita Controller" mapping agrees): no hat, the d-pad is four buttons. L2, R2, L3, R3 exist on a PSTV's pad only.
enum class ConsoleButton : int {
    Triangle = 0, Circle = 1, Cross = 2, Square = 3, L1 = 4, R1 = 5,
    DpadDown = 6, DpadLeft = 7, DpadUp = 8, DpadRight = 9, Select = 10, Start = 11,
    L2 = 12, R2 = 13, L3 = 14, R3 = 15,
};

struct ConsoleInputSettings {
    bool circleConfirms = false;   // SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE; Cross otherwise
    float cursorSpeed = 900.0f;    // pixels per second at full stick deflection
    float stickDeadZone = 0.2f;    // of the axis range (-1..1)
    float tapSeconds = 0.2f;       // a touch released within this and kTapPixels is a tap
    float tapPixels = 12.0f;
};

class ConsoleInput {
public:
    explicit ConsoleInput(ConsoleInputSettings settings = {});

    // The target's size in pixels (the units of InputEvents::mouseX/Y); the cursor is kept inside it.
    void setTargetSize(int width, int height);
    // Whether a level is being played (Window::setCursorHidden(true)): touch then moves the bat without pressing.
    void setPlaying(bool playing);
    // Whether a text field is being edited (Window::setTextInput(true)): confirm sends Enter.
    void setTextInput(bool on);
    const ConsoleInputSettings& settings() const { return settings_; }

    // Events, in the order they arrived. `button` is the joystick's positional index (ConsoleButton).
    void buttonDown(int button);
    void buttonUp(int button);
    // The left stick: -1..1 each, SDL's axes 0 and 1 scaled from -32768..32767 by the caller.
    void stick(float x, float y);
    // The front touch screen, in target pixels (SDL's normalised 0..1 times the target size), `finger` SDL's id;
    // only the first finger down matters, the others are ignored until it lifts.
    void fingerDown(std::int64_t finger, float x, float y, double timeSeconds);
    void fingerMotion(std::int64_t finger, float x, float y, double timeSeconds);
    void fingerUp(std::int64_t finger, float x, float y, double timeSeconds);

    // Once per frame, after the events: moves the cursor by the stick and the d-pad for `dtSeconds`, and fills in
    // what the frame produced: `input.pointer` (every press, release and cursor move, oldest first, as
    // PointerSample), `input.mouseX/Y` (the cursor), `input.leftButton` (confirm or a finger held, as the frame
    // ends), `input.keysPressed` (SDLK_ESCAPE for back and for Start, SDLK_RETURN for confirm while text input is
    // on; launching is the press edge of the pointer, never a key), and `input.mouseDX/DY`, `leftDragDX/DY` from the
    // cursor's movement with the button state, as the mouse would report them. Fields it does not own are left as
    // they are.
    void frame(double dtSeconds, InputEvents& input);

    float cursorX() const { return cursorX_; }
    float cursorY() const { return cursorY_; }
    bool confirmHeld() const { return confirmHeld_; }
    // True while a short-tap launch or a menu touch is pressing: the pointer is down because of the touch screen.
    bool touchHeld() const { return touchHeld_; }

private:
    void press(InputEvents& input, float x, float y);
    void release(InputEvents& input, float x, float y);
    void moveCursorTo(InputEvents& input, float x, float y);

    ConsoleInputSettings settings_;
    int width_ = 960, height_ = 544;
    bool playing_ = false;
    bool textInput_ = false;
    float cursorX_ = 480, cursorY_ = 272;
    float stickX_ = 0, stickY_ = 0;
    bool dpadLeft_ = false, dpadRight_ = false, dpadUp_ = false, dpadDown_ = false;
    bool confirmHeld_ = false;
    bool touchHeld_ = false;
    std::optional<std::int64_t> finger_;  // the finger being tracked
    float fingerStartX_ = 0, fingerStartY_ = 0;
    double fingerStartTime_ = 0;
    bool fingerMoved_ = false;   // beyond tapPixels
    bool fingerPressed_ = false;  // in the menus: the finger pressed the button when it landed
    InputEvents pending_;         // what the events produced before frame() hands it over
};

}  // namespace mb
