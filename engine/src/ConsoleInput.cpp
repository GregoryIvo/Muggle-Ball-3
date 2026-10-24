#include "mb/engine/ConsoleInput.h"

#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <cmath>

namespace mb {

namespace {

// Which positional button confirms and which goes back, per the system's enter-button setting.
int confirmButton(const ConsoleInputSettings& settings) {
    return static_cast<int>(settings.circleConfirms ? ConsoleButton::Circle : ConsoleButton::Cross);
}

int backButton(const ConsoleInputSettings& settings) {
    return static_cast<int>(settings.circleConfirms ? ConsoleButton::Cross : ConsoleButton::Circle);
}

// A coordinate should always be finite (SDL's normalised touch is 0..1, and the stick/d-pad only ever add a
// finite speed times a finite dt), but std::clamp passes NaN straight through and a NaN cursor position never
// recovers on its own: fall back to the last good position instead of ever accepting one.
float finiteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

}  // namespace

ConsoleInput::ConsoleInput(ConsoleInputSettings settings) : settings_(settings) {}

void ConsoleInput::setTargetSize(int width, int height) {
    width_ = width;
    height_ = height;
    cursorX_ = std::clamp(cursorX_, 0.0f, static_cast<float>(width_));
    cursorY_ = std::clamp(cursorY_, 0.0f, static_cast<float>(height_));
}

void ConsoleInput::setPlaying(bool playing) {
    if (playing == playing_) {
        return;
    }
    playing_ = playing;
    // A finger left down across the switch would otherwise carry the menu's press state into play (or the
    // other way round), which is surprising either way: let go of it cleanly instead.
    if (finger_) {
        if (touchHeld_) {
            release(pending_, cursorX_, cursorY_);
            touchHeld_ = false;
        }
        finger_.reset();
        fingerMoved_ = false;
        fingerPressed_ = false;
    }
}

void ConsoleInput::setTextInput(bool on) {
    if (on == textInput_) {
        return;
    }
    // While confirm was held with text input off, a press was sent for it; if a text field takes the keyboard
    // over now, that press must let go instead of staying stuck down (the IME's own Return follows on its own).
    const bool pointerWasDownForConfirm = confirmHeld_ && !textInput_;
    textInput_ = on;
    if (on && pointerWasDownForConfirm) {
        release(pending_, cursorX_, cursorY_);
    }
}

void ConsoleInput::buttonDown(int button) {
    if (button == confirmButton(settings_)) {
        if (confirmHeld_) {
            return;  // already down with no up in between: a duplicate event, not a second press
        }
        confirmHeld_ = true;
        if (textInput_) {
            pending_.keysPressed.push_back(SDLK_RETURN);
        } else {
            press(pending_, cursorX_, cursorY_);
        }
        return;
    }
    if (button == backButton(settings_) || button == static_cast<int>(ConsoleButton::Start)) {
        pending_.keysPressed.push_back(SDLK_ESCAPE);
        return;
    }
    switch (static_cast<ConsoleButton>(button)) {
        case ConsoleButton::DpadLeft: dpadLeft_ = true; break;
        case ConsoleButton::DpadRight: dpadRight_ = true; break;
        case ConsoleButton::DpadUp: dpadUp_ = true; break;
        case ConsoleButton::DpadDown: dpadDown_ = true; break;
        default: break;
    }
}

void ConsoleInput::buttonUp(int button) {
    if (button == confirmButton(settings_)) {
        if (!confirmHeld_) {
            return;  // an up with no matching down: ignore rather than release something never pressed
        }
        confirmHeld_ = false;
        if (!textInput_) {
            release(pending_, cursorX_, cursorY_);
        }
        return;
    }
    switch (static_cast<ConsoleButton>(button)) {
        case ConsoleButton::DpadLeft: dpadLeft_ = false; break;
        case ConsoleButton::DpadRight: dpadRight_ = false; break;
        case ConsoleButton::DpadUp: dpadUp_ = false; break;
        case ConsoleButton::DpadDown: dpadDown_ = false; break;
        default: break;  // back and Start have no held state: Escape was already sent on the press edge
    }
}

void ConsoleInput::stick(float x, float y) {
    stickX_ = std::clamp(x, -1.0f, 1.0f);
    stickY_ = std::clamp(y, -1.0f, 1.0f);
}

void ConsoleInput::fingerDown(std::int64_t finger, float x, float y, double timeSeconds) {
    if (finger_) {
        return;  // only the first finger down matters; the rest are ignored until it lifts
    }
    finger_ = finger;
    fingerStartX_ = x;
    fingerStartY_ = y;
    fingerStartTime_ = timeSeconds;
    fingerMoved_ = false;
    if (playing_) {
        moveCursorTo(pending_, x, y);  // in play, landing only moves the cursor; it never presses
    } else {
        touchHeld_ = true;
        fingerPressed_ = true;
        press(pending_, x, y);  // in the menus, landing is a click, at the finger
    }
}

void ConsoleInput::fingerMotion(std::int64_t finger, float x, float y, double timeSeconds) {
    (void)timeSeconds;
    if (!finger_ || *finger_ != finger) {
        return;  // not the finger being tracked (or none is)
    }
    const float dx = x - fingerStartX_, dy = y - fingerStartY_;
    if (std::sqrt(dx * dx + dy * dy) >= settings_.tapPixels) {
        fingerMoved_ = true;  // moved too far from where it landed to ever be a tap, however it ends
    }
    moveCursorTo(pending_, x, y);
}

void ConsoleInput::fingerUp(std::int64_t finger, float x, float y, double timeSeconds) {
    if (!finger_ || *finger_ != finger) {
        return;
    }
    const float dx = x - fingerStartX_, dy = y - fingerStartY_;
    const bool movedTooFar = fingerMoved_ || std::sqrt(dx * dx + dy * dy) >= settings_.tapPixels;
    const bool heldTooLong = (timeSeconds - fingerStartTime_) > settings_.tapSeconds;
    if (playing_) {
        if (!movedTooFar && !heldTooLong) {
            // A short tap launches: a press and a release at the same point, in the same frame.
            press(pending_, x, y);
            release(pending_, x, y);
        } else {
            moveCursorTo(pending_, x, y);  // a drag or a slow lift just leaves the cursor where it is
        }
    } else {
        release(pending_, x, y);
        touchHeld_ = false;
        fingerPressed_ = false;
    }
    finger_.reset();
    fingerMoved_ = false;
}

void ConsoleInput::frame(double dtSeconds, InputEvents& input) {
    // Window.cpp clamps dt to a finite, non-negative value before calling this, but a non-finite dt would
    // otherwise poison the cursor with NaN permanently (every clamp below passes NaN straight through, and
    // nothing after this ever recovers): guard it here too, since this class is host-tested on its own.
    const float dt = std::isfinite(dtSeconds) ? static_cast<float>(dtSeconds) : 0.0f;
    // The dead zone is cut from the deflection and the rest scaled so full deflection reaches cursorSpeed;
    // the d-pad always drives at full speed. Both are added, so holding both at once is simply faster.
    const auto axisSpeed = [&](float value) {
        const float magnitude = std::fabs(value);
        if (magnitude <= settings_.stickDeadZone) {
            return 0.0f;
        }
        const float scaled = (magnitude - settings_.stickDeadZone) / (1.0f - settings_.stickDeadZone);
        return std::copysign(scaled, value) * settings_.cursorSpeed;
    };
    const float vx = axisSpeed(stickX_) + (dpadRight_ ? settings_.cursorSpeed : 0.0f) - (dpadLeft_ ? settings_.cursorSpeed : 0.0f);
    const float vy = axisSpeed(stickY_) + (dpadDown_ ? settings_.cursorSpeed : 0.0f) - (dpadUp_ ? settings_.cursorSpeed : 0.0f);
    const float targetX = std::clamp(cursorX_ + vx * dt, 0.0f, static_cast<float>(width_));
    const float targetY = std::clamp(cursorY_ + vy * dt, 0.0f, static_cast<float>(height_));
    if (targetX != cursorX_ || targetY != cursorY_) {
        moveCursorTo(pending_, targetX, targetY);
    }

    // Hand over what this frame produced; fields InputEvents has that ConsoleInput does not own (text, wheel,
    // the right and middle buttons) are left as Window.cpp (or whatever else filled `input`) set them.
    input.keysPressed.insert(input.keysPressed.end(), pending_.keysPressed.begin(), pending_.keysPressed.end());
    input.pointer.insert(input.pointer.end(), pending_.pointer.begin(), pending_.pointer.end());
    input.mouseDX += pending_.mouseDX;
    input.mouseDY += pending_.mouseDY;
    input.leftDragDX += pending_.leftDragDX;
    input.leftDragDY += pending_.leftDragDY;
    input.mouseX = cursorX_;
    input.mouseY = cursorY_;
    input.leftButton = input.leftButton || confirmHeld_ || touchHeld_;

    // Reused, not reallocated: clear() keeps every vector's capacity, unlike assigning a fresh InputEvents{}.
    pending_.keysPressed.clear();
    pending_.pointer.clear();
    pending_.mouseDX = pending_.mouseDY = 0;
    pending_.leftDragDX = pending_.leftDragDY = 0;
}

void ConsoleInput::press(InputEvents& input, float x, float y) {
    cursorX_ = std::clamp(finiteOr(x, cursorX_), 0.0f, static_cast<float>(width_));
    cursorY_ = std::clamp(finiteOr(y, cursorY_), 0.0f, static_cast<float>(height_));
    input.pointer.push_back({cursorX_, cursorY_, true});
}

void ConsoleInput::release(InputEvents& input, float x, float y) {
    cursorX_ = std::clamp(finiteOr(x, cursorX_), 0.0f, static_cast<float>(width_));
    cursorY_ = std::clamp(finiteOr(y, cursorY_), 0.0f, static_cast<float>(height_));
    input.pointer.push_back({cursorX_, cursorY_, false});
}

// A plain move (the stick, the d-pad, a touch that is not pressing): carries the current button state along,
// like Window.cpp's own mouse-motion samples, and feeds mouseDX/DY and leftDragDX/DY the same way a real mouse
// would. press()/release() deliberately do not: Window.cpp's button events do not touch them either, only its
// motion events do, so a touch that teleports the cursor on landing is not counted as a drag.
void ConsoleInput::moveCursorTo(InputEvents& input, float x, float y) {
    const float clampedX = std::clamp(finiteOr(x, cursorX_), 0.0f, static_cast<float>(width_));
    const float clampedY = std::clamp(finiteOr(y, cursorY_), 0.0f, static_cast<float>(height_));
    const float dx = clampedX - cursorX_;
    const float dy = clampedY - cursorY_;
    cursorX_ = clampedX;
    cursorY_ = clampedY;
    const bool held = confirmHeld_ || touchHeld_;
    input.pointer.push_back({cursorX_, cursorY_, held});
    input.mouseDX += dx;
    input.mouseDY += dy;
    if (held) {
        input.leftDragDX += dx;
        input.leftDragDY += dy;
    }
}

}  // namespace mb
