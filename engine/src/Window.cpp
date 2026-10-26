#include "mb/engine/Window.h"

#include "mb/engine/Log.h"

#include <SDL3/SDL.h>

#ifdef MB3_PLATFORM_VITA
#include "mb/engine/ConsoleInput.h"

#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/system_param.h>
#endif

#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace mb {

namespace {

#ifdef MB3_PLATFORM_VITA
// Cross confirms unless the system's own setting says Circle; read once at start-up, as the hello does.
bool circleConfirmsOnVita() {
    SceAppUtilInitParam init{};
    SceAppUtilBootParam boot{};
    sceAppUtilInit(&init, &boot);
    int value = SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS;
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, &value);
    return value == SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE;
}

// The front panel's touch device id: SDL registers it before the rear pad, and its name says which is which.
bool findFrontTouchId(std::uint64_t& id) {
    int count = 0;
    SDL_TouchID* ids = SDL_GetTouchDevices(&count);
    // Read before SDL_free below: this is the fallback used only if no device name contains "Front".
    const SDL_TouchID first = count > 0 ? ids[0] : 0;
    bool found = false;
    for (int i = 0; i < count && !found; ++i) {
        const char* name = SDL_GetTouchDeviceName(ids[i]);
        if (name && std::string_view(name).find("Front") != std::string_view::npos) {
            id = ids[i];
            found = true;
        }
    }
    SDL_free(ids);
    if (!found && count > 0) {
        id = first;  // SDL registers the front panel first; fall back to it if the name ever changes
        found = true;
    }
    return found;
}
#endif

// The shortcuts this window handles itself.
bool isFullscreenShortcut(const SDL_KeyboardEvent& key) {
    switch (key.key) {
        case SDLK_F11:
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            return (key.mod & SDL_KMOD_ALT) != 0;
        default:
            return false;
    }
}

// On macOS, Ctrl+Cmd+F is the key equivalent of the "Toggle Full Screen" menu item SDL installs, and the
// system acts on it. SDL reports the key press as well, so toggling here too would switch twice. It is
// kept from the game, which would otherwise see a plain F.
bool isMenuFullscreenShortcut(const SDL_KeyboardEvent& key) {
#ifdef __APPLE__
    return key.key == SDLK_F && (key.mod & SDL_KMOD_CTRL) != 0 && (key.mod & SDL_KMOD_GUI) != 0;
#else
    (void)key;
    return false;
#endif
}

}  // namespace

Window::Window(const std::string& title, int width, int height, bool startFullscreen) {
#ifdef MB3_PLATFORM_VITA
    // Touch is read as touch, never synthesised into a mouse click (which would launch the ball wherever a
    // finger lands), the rear pad is off (palms there must do nothing), and the IME is offered even though
    // there is no physical keyboard (docs/VITA.md "SDL3 on Vita").
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_VITA_ENABLE_BACK_TOUCH, "0");
    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "1");
#endif
    Uint32 initFlags = SDL_INIT_VIDEO | SDL_INIT_GAMEPAD;
#ifdef MB3_PLATFORM_VITA
    initFlags |= SDL_INIT_JOYSTICK;
#endif
    if (!SDL_Init(initFlags)) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }
    // A window bigger than the screen is never wanted (a size remembered from another display, say).
    SDL_Rect usable{};
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) && usable.w > 0 && usable.h > 0) {
        width = std::min(width, usable.w);
        height = std::min(height, usable.h);
    }
    width = std::max(width, kMinWindowWidth);
    height = std::max(height, kMinWindowHeight);
    window_ = SDL_CreateWindow(title.c_str(), width, height,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
#if defined(MB3_RENDERER_GL)
                                   SDL_WINDOW_OPENGL |  // the GL back end draws through an SDL GL context
#endif
                                   (startFullscreen ? SDL_WINDOW_FULLSCREEN : 0));
    if (!window_) {
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }
    SDL_SetWindowMinimumSize(window_, kMinWindowWidth, kMinWindowHeight);
    windowedWidth_ = width;
    windowedHeight_ = height;
    fullscreenRequested_ = startFullscreen;
    int pixelsW = 0, pixelsH = 0;
    SDL_GetWindowSizeInPixels(window_, &pixelsW, &pixelsH);
    logInfo("Window: {}x{} points, {}x{} pixels (display scale {:.1f}){}", width, height, pixelsW, pixelsH,
            SDL_GetWindowDisplayScale(window_), startFullscreen ? ", fullscreen" : "");

#ifdef MB3_PLATFORM_VITA
    const bool circleConfirms = circleConfirmsOnVita();
    logInfo("Window: enter button is {}", circleConfirms ? "Circle" : "Cross");
    console_ = std::make_unique<ConsoleInput>(ConsoleInputSettings{.circleConfirms = circleConfirms});
    console_->setTargetSize(pixelsW, pixelsH);

    int joystickCount = 0;
    if (SDL_JoystickID* ids = SDL_GetJoysticks(&joystickCount)) {
        if (joystickCount > 0) {
            joystick_ = SDL_OpenJoystick(ids[0]);
            logInfo("Window: joystick 0 '{}', a gamepad mapping {}",
                    joystick_ ? SDL_GetJoystickName(joystick_) : "?", SDL_IsGamepad(ids[0]) ? "exists" : "does not exist");
        } else {
            logInfo("Window: no joystick");
        }
        SDL_free(ids);
    } else {
        logInfo("Window: no joystick");
    }
    if (findFrontTouchId(frontTouchId_)) {
        frontTouchKnown_ = true;
        logInfo("Window: front touch device id {}", frontTouchId_);
    } else {
        logInfo("Window: no touch device found");
    }
#endif
}

Window::~Window() {
#ifdef MB3_PLATFORM_VITA
    if (joystick_) {
        SDL_CloseJoystick(joystick_);
    }
#endif
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
    // SDL reports the pointer in window points; the renderer's pixels differ on a high-density display.
    float pixelsPerPointX = 1, pixelsPerPointY = 1;
    int points = 0, pointsHigh = 0, pixels = 0, pixelsHigh = 0;
    if (SDL_GetWindowSize(window_, &points, &pointsHigh) && SDL_GetWindowSizeInPixels(window_, &pixels, &pixelsHigh) &&
        points > 0 && pointsHigh > 0) {
        pixelsPerPointX = static_cast<float>(pixels) / static_cast<float>(points);
        pixelsPerPointY = static_cast<float>(pixelsHigh) / static_cast<float>(pointsHigh);
    }
    const auto samplePointer = [&](float x, float y) {
        pointerX_ = x * pixelsPerPointX;
        pointerY_ = y * pixelsPerPointY;
        input.pointer.push_back({pointerX_, pointerY_, leftHeld_});
    };

    bool running = true;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE && escapeQuits_) {
                    running = false;
                } else if (!event.key.repeat) {
                    if (isFullscreenShortcut(event.key)) {
                        toggleFullscreen();
                    } else if (!isMenuFullscreenShortcut(event.key)) {
                        input.keysPressed.push_back(event.key.key);
                    }
                }
                break;
            case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
                logInfo("Window: fullscreen");
                break;
            case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
                fullscreenRequested_ = false;  // however it ended, for instance from the macOS menu
                logInfo("Window: windowed");
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                // Fullscreen reports the display's size, which is not one to go back to. SDL's Cocoa back end
                // sends it after the window is flagged fullscreen, and the windowed size back after the flag
                // is cleared; elsewhere it can come before the flag is set.
                if (!fullscreen() && !fullscreenRequested_ && event.window.data1 > 0 && event.window.data2 > 0) {
                    windowedWidth_ = event.window.data1;
                    windowedHeight_ = event.window.data2;
                }
                break;
            case SDL_EVENT_TEXT_INPUT:
                input.text += event.text.text;
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
                if (event.button.button == SDL_BUTTON_LEFT) {
                    samplePointer(event.button.x, event.button.y);
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
                samplePointer(event.motion.x, event.motion.y);
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL:
                input.wheel += event.wheel.y;
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                // A release outside the window may never arrive; don't leave a drag or a press stuck on.
                if (leftHeld_) {
                    input.pointer.push_back({pointerX_, pointerY_, false});
                }
                leftHeld_ = rightHeld_ = middleHeld_ = false;
                break;
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                input.pointer.push_back({-1, -1, leftHeld_});  // off the window: nothing is hovered
                break;
#ifdef MB3_PLATFORM_VITA
            case SDL_EVENT_JOYSTICK_ADDED:
                // A PSTV's pad can drop and re-pair (sleep, low battery) with a new instance id; SDL only
                // delivers button/axis events for an opened joystick, so without this the game would take no
                // more pad input until restarted. Only replace it if nothing is open already.
                if (!joystick_) {
                    joystick_ = SDL_OpenJoystick(event.jdevice.which);
                    logInfo("Window: joystick {} '{}' connected", event.jdevice.which,
                            joystick_ ? SDL_GetJoystickName(joystick_) : "?");
                }
                break;
            case SDL_EVENT_JOYSTICK_REMOVED:
                if (joystick_ && SDL_GetJoystickID(joystick_) == event.jdevice.which) {
                    SDL_CloseJoystick(joystick_);
                    joystick_ = nullptr;
                    if (console_) {
                        // Nothing stays held once the pad is gone: release confirm and every d-pad direction,
                        // as a real buttonUp would. ConsoleInput ignores an up with no matching down, so this
                        // is harmless for whichever of these were not actually held.
                        const int confirm = console_->settings().circleConfirms
                                                 ? static_cast<int>(ConsoleButton::Circle)
                                                 : static_cast<int>(ConsoleButton::Cross);
                        console_->buttonUp(confirm);
                        console_->buttonUp(static_cast<int>(ConsoleButton::DpadLeft));
                        console_->buttonUp(static_cast<int>(ConsoleButton::DpadRight));
                        console_->buttonUp(static_cast<int>(ConsoleButton::DpadUp));
                        console_->buttonUp(static_cast<int>(ConsoleButton::DpadDown));
                    }
                    logInfo("Window: joystick {} disconnected", event.jdevice.which);
                }
                break;
            case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
            case SDL_EVENT_JOYSTICK_BUTTON_UP:
                if (console_) {
                    if (event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN) {
                        // sceImeDialog is one-shot: once it closes (confirmed or cancelled) nothing SDL does
                        // reopens it, and FrontEnd starts text input only when wantsText changes, so without
                        // this a cancel (or a second look at the name) would be stuck with no keyboard at all.
                        // Reopening and forwarding the press to ConsoleInput are mutually exclusive: forwarding
                        // it too would send Return (ConsoleInput::buttonDown, text input on) and submit the
                        // field's current text in the very press that is meant to reopen the dialog instead.
                        const int confirm = console_->settings().circleConfirms ? static_cast<int>(ConsoleButton::Circle)
                                                                                 : static_cast<int>(ConsoleButton::Cross);
                        const bool reopen =
                            textInputOn_ && event.jbutton.button == confirm && !SDL_ScreenKeyboardShown(window_);
                        if (reopen) {
                            SDL_StopTextInput(window_);
                            SDL_StartTextInput(window_);
                            logInfo("Window: confirm pressed with no IME shown while text input is on; reopening it");
                        } else {
                            console_->buttonDown(event.jbutton.button);
                        }
                    } else {
                        // The matching up for a press that reopened the IME is simply ignored: ConsoleInput
                        // never saw the down, so its "up without down" rule (ConsoleInput.cpp) drops it.
                        console_->buttonUp(event.jbutton.button);
                    }
                }
                break;
            case SDL_EVENT_JOYSTICK_AXIS_MOTION:
                if (console_ && (event.jaxis.axis == 0 || event.jaxis.axis == 1)) {
                    // -32768..32767 to -1..1, each axis remembered so the other's last value is not lost.
                    const float value = std::clamp(event.jaxis.value / 32767.0f, -1.0f, 1.0f);
                    if (event.jaxis.axis == 0) {
                        stickAxisX_ = value;
                    } else {
                        stickAxisY_ = value;
                    }
                    console_->stick(stickAxisX_, stickAxisY_);
                }
                break;
            case SDL_EVENT_FINGER_DOWN:
            case SDL_EVENT_FINGER_MOTION:
            case SDL_EVENT_FINGER_UP:
                if (console_ && frontTouchKnown_ && event.tfinger.touchID == frontTouchId_) {
                    const float x = event.tfinger.x * static_cast<float>(pixels);
                    const float y = event.tfinger.y * static_cast<float>(pixelsHigh);
                    const double seconds = static_cast<double>(event.tfinger.timestamp) / 1e9;  // ns to s
                    if (event.type == SDL_EVENT_FINGER_DOWN) {
                        console_->fingerDown(event.tfinger.fingerID, x, y, seconds);
                    } else if (event.type == SDL_EVENT_FINGER_MOTION) {
                        console_->fingerMotion(event.tfinger.fingerID, x, y, seconds);
                    } else {
                        console_->fingerUp(event.tfinger.fingerID, x, y, seconds);
                    }
                }
                break;
#endif
            default:
                break;
        }
    }
    input.leftButton = leftHeld_;
    input.rightButton = rightHeld_;
    input.middleButton = middleHeld_;

#ifdef MB3_PLATFORM_VITA
    // A console woken from sleep while a level was playing: push it through the pause rule like any other
    // Escape (the 0.1 s dt clamp below absorbs the clock jump the sleep caused).
    SceAppMgrSystemEvent appEvent{};
    if (sceAppMgrReceiveSystemEvent(&appEvent) >= 0 && appEvent.systemEvent == SCE_APPMGR_SYSTEMEVENT_ON_RESUME && cursorHidden_) {
        input.keysPressed.push_back(SDLK_ESCAPE);
        logInfo("Window: resumed while a level was playing; pausing it");
    }
    if (console_) {
        const auto now = std::chrono::steady_clock::now();
        const double dt = lastPoll_.time_since_epoch().count() == 0
                               ? 0.0
                               : std::min(std::chrono::duration<double>(now - lastPoll_).count(), 0.1);
        lastPoll_ = now;
        console_->frame(dt, input);
    }
#else
    float x = 0, y = 0;
    SDL_GetMouseState(&x, &y);
    input.mouseX = x * pixelsPerPointX;
    input.mouseY = y * pixelsPerPointY;
#endif
    return running;
}

void Window::setTextInput(bool on) {
    if (on ? !SDL_StartTextInput(window_) : !SDL_StopTextInput(window_)) {
        logWarn("cannot {} text input: {}", on ? "start" : "stop", SDL_GetError());
    }
#ifdef MB3_PLATFORM_VITA
    textInputOn_ = on;
    if (console_) {
        console_->setTextInput(on);
    }
#endif
}

void Window::setTitle(const std::string& title) {
    SDL_SetWindowTitle(window_, title.c_str());
}

void Window::setCursorHidden(bool hidden) {
    if (hidden ? !SDL_HideCursor() : !SDL_ShowCursor()) {
        logWarn("cannot {} the cursor: {}", hidden ? "hide" : "show", SDL_GetError());
    }
    SDL_SetWindowMouseGrab(window_, hidden);
#ifdef MB3_PLATFORM_VITA
    cursorHidden_ = hidden;
    if (console_) {
        console_->setPlaying(hidden);  // the pointer is the bat exactly when the cursor is hidden
    }
#endif
}

void Window::keepAwake() {
#ifdef MB3_PLATFORM_VITA
    // SDL ticks the power timer in its own event pump, which is the only thing keeping the console from
    // sleeping during a long, blocking level load. Desktop has nothing to gain from this (and must not
    // start processing OS events mid-load where it did not before), so the call is Vita-only.
    SDL_PumpEvents();
#endif
}

void Window::setSize(int width, int height) {
    SDL_SetWindowSize(window_, width, height);
}

bool Window::displaySize(int& width, int& height) const {
    SDL_Rect bounds{};
    if (!SDL_GetDisplayBounds(SDL_GetDisplayForWindow(window_), &bounds) || bounds.w <= 0 || bounds.h <= 0) {
        return false;
    }
    width = bounds.w;
    height = bounds.h;
    return true;
}

bool Window::fullscreen() const {
    return (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
}

void Window::setFullscreen(bool on) {
    fullscreenRequested_ = on;
    if (!SDL_SetWindowFullscreen(window_, on)) {
        logWarn("cannot switch {} fullscreen: {}", on ? "to" : "from", SDL_GetError());
        fullscreenRequested_ = false;
    }
}

void Window::windowedSize(int& width, int& height) const {
    width = windowedWidth_;
    height = windowedHeight_;
}

}  // namespace mb
