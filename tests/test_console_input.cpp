// The Vita's buttons, stick and touch screen turned into InputEvents (docs/VITA_PLAN.md 5.8), driven straight
// through ConsoleInput with no window, SDL events or device needed: it is platform-neutral on purpose so these
// rules are covered on the host. One test per rule in engine/include/mb/engine/ConsoleInput.h.

#include "mb/engine/ConsoleInput.h"

#include <SDL3/SDL_keycode.h>

#include <cmath>
#include <cstdio>
#include <cstdint>

using namespace mb;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Runs one frame with no events beyond what the caller already fed in, and returns what it produced.
InputEvents frameOf(ConsoleInput& console, double dt = 1.0 / 60.0) {
    InputEvents input;
    console.frame(dt, input);
    return input;
}

bool hasKey(const InputEvents& input, std::uint32_t key) {
    for (const std::uint32_t k : input.keysPressed) {
        if (k == key) {
            return true;
        }
    }
    return false;
}

void testConfirmButtonPerSettings() {
    // Cross confirms unless the system says Circle.
    ConsoleInput cross({.circleConfirms = false});
    cross.buttonDown(static_cast<int>(ConsoleButton::Cross));
    InputEvents a = frameOf(cross);
    check(a.pointer.size() == 1 && a.pointer[0].leftDown, "confirm (Cross): a press at the cursor");
    cross.buttonDown(static_cast<int>(ConsoleButton::Circle));  // the other button does nothing for confirm
    InputEvents b = frameOf(cross);
    check(b.pointer.empty(), "confirm (Cross): Circle is not confirm when the setting says Cross");

    ConsoleInput circle({.circleConfirms = true});
    circle.buttonDown(static_cast<int>(ConsoleButton::Circle));
    InputEvents c = frameOf(circle);
    check(c.pointer.size() == 1 && c.pointer[0].leftDown, "confirm (Circle): a press at the cursor when the setting says Circle");
}

void testBackAndStartEscapeOncePerPress() {
    ConsoleInput console({.circleConfirms = false});  // back is Circle
    console.buttonDown(static_cast<int>(ConsoleButton::Circle));
    InputEvents a = frameOf(console);
    check(hasKey(a, SDLK_ESCAPE) && a.keysPressed.size() == 1, "back: one Escape on the press");
    InputEvents b = frameOf(console);  // still held, no new event
    check(b.keysPressed.empty(), "back: holding it does not repeat Escape every frame");

    console.buttonDown(static_cast<int>(ConsoleButton::Start));
    InputEvents c = frameOf(console);
    check(hasKey(c, SDLK_ESCAPE), "Start: also Escape, on its own press");
}

void testConfirmWhileTextInputGivesReturnNoPress() {
    ConsoleInput console;
    console.setTextInput(true);
    console.buttonDown(static_cast<int>(ConsoleButton::Cross));
    InputEvents a = frameOf(console);
    check(hasKey(a, SDLK_RETURN) && a.pointer.empty(), "confirm while text input: Return, and no pointer press");
    console.buttonUp(static_cast<int>(ConsoleButton::Cross));
    InputEvents b = frameOf(console);
    check(b.pointer.empty(), "confirm while text input: releasing it presses nothing either");
}

void testConfirmPressReleaseWithinOneFrame() {
    // Both events arrive before frame() is ever called, as they would within one poll.
    ConsoleInput console;
    console.buttonDown(static_cast<int>(ConsoleButton::Cross));
    console.buttonUp(static_cast<int>(ConsoleButton::Cross));
    InputEvents a = frameOf(console);
    check(a.pointer.size() == 2 && a.pointer[0].leftDown && !a.pointer[1].leftDown,
          "confirm: a press then a release, in that order, in one frame");
}

void testStickMovesCursorScaledAndClamped() {
    ConsoleInput console;  // default cursor (480, 272), target 960x544, cursorSpeed 900, dead zone 0.2
    const ConsoleInputSettings& s = console.settings();
    console.stick(1.0f, 0.0f);  // full deflection
    InputEvents a = frameOf(console, 0.5);
    const float expected = 480.0f + s.cursorSpeed * 0.5f;
    check(a.pointer.size() == 1 && std::fabs(a.mouseX - expected) < 0.01f, "stick: full deflection covers cursorSpeed * dt");
    check(a.mouseX == console.cursorX(), "stick: InputEvents::mouseX is the cursor");

    // A half deflection (within the dead zone at 0.2) moves nothing.
    ConsoleInput still;
    still.stick(0.15f, 0.0f);
    InputEvents b = frameOf(still, 1.0);
    check(b.pointer.empty() && still.cursorX() == 480, "stick: inside the dead zone the cursor does not move");

    // Full deflection for a long time clamps at the edge, not past it.
    ConsoleInput clamp;
    clamp.stick(1.0f, 0.0f);
    InputEvents c = frameOf(clamp, 10.0);
    check(clamp.cursorX() == 960, "stick: the cursor stops at the target's edge");
    check(c.pointer.back().x == 960, "stick: the clamped position is what is reported");
}

void testDpadMoves() {
    ConsoleInput console;
    console.buttonDown(static_cast<int>(ConsoleButton::DpadRight));
    InputEvents a = frameOf(console, 1.0);
    check(console.cursorX() > 480, "d-pad: right moves the cursor right");
    console.buttonUp(static_cast<int>(ConsoleButton::DpadRight));
    console.buttonDown(static_cast<int>(ConsoleButton::DpadUp));
    const float beforeUp = console.cursorY();
    InputEvents b = frameOf(console, 1.0);
    check(console.cursorY() < beforeUp, "d-pad: up moves the cursor up (smaller y)");
}

void testMenuTouchPressDragRelease() {
    ConsoleInput console;  // playing() defaults to false: the menus
    console.fingerDown(1, 200, 150, 0.0);
    InputEvents a = frameOf(console);
    check(a.pointer.size() == 1 && a.pointer[0].leftDown && a.pointer[0].x == 200, "menu touch: landing presses at the finger");
    check(console.touchHeld(), "menu touch: held while the finger is down");

    console.fingerMotion(1, 260, 150, 0.1);
    InputEvents b = frameOf(console);
    check(b.pointer.size() == 1 && b.pointer[0].leftDown && b.pointer[0].x == 260, "menu touch: motion drags, still held");

    console.fingerUp(1, 260, 150, 0.2);
    InputEvents c = frameOf(console);
    check(c.pointer.size() == 1 && !c.pointer[0].leftDown, "menu touch: lifting releases");
    check(!console.touchHeld(), "menu touch: not held once released");
}

void testPlayModeDragNeverPresses() {
    ConsoleInput console;
    console.setPlaying(true);
    console.fingerDown(1, 300, 300, 0.0);
    console.fingerMotion(1, 400, 300, 0.05);   // 100 px: well past tapPixels
    console.fingerUp(1, 400, 300, 0.1);
    InputEvents a = frameOf(console);
    bool anyPress = false;
    for (const PointerSample& sample : a.pointer) {
        anyPress = anyPress || sample.leftDown;
    }
    check(!anyPress, "play: a drag never presses, down, motion or up");
    check(console.cursorX() == 400, "play: the cursor still follows the finger");
}

void testPlayModeTapPressesAndReleasesAtTheLandingPoint() {
    ConsoleInput console;
    console.setPlaying(true);
    console.fingerDown(1, 500, 250, 0.0);  // moves the cursor there; not a press
    console.fingerUp(1, 503, 250, 0.1);    // 3 px, under tapPixels (12); 0.1 s, under tapSeconds (0.2)
    InputEvents a = frameOf(console);
    check(a.pointer.size() == 3, "play: the landing move, then the tap's press and release");
    check(!a.pointer[0].leftDown, "play: the landing itself does not press");
    check(a.pointer[1].leftDown && !a.pointer[2].leftDown, "play: a tap presses then releases");
    check(a.pointer[1].x == 503 && a.pointer[2].x == 503, "play: at the point it lifted at");
}

void testSlowOrLongTouchDoesNotPress() {
    ConsoleInput moved;
    moved.setPlaying(true);
    moved.fingerDown(1, 500, 250, 0.0);
    moved.fingerUp(1, 520, 250, 0.1);  // 20 px: at or past tapPixels (12)
    InputEvents a = frameOf(moved);
    for (const PointerSample& sample : a.pointer) {
        check(!sample.leftDown, "play: moved too far to be a tap");
    }

    ConsoleInput slow;
    slow.setPlaying(true);
    slow.fingerDown(1, 500, 250, 0.0);
    slow.fingerUp(1, 500, 250, 0.5);  // held 0.5 s: past tapSeconds (0.2)
    InputEvents b = frameOf(slow);
    for (const PointerSample& sample : b.pointer) {
        check(!sample.leftDown, "play: held too long to be a tap");
    }
}

void testSecondFingerIgnored() {
    ConsoleInput console;
    console.fingerDown(1, 100, 100, 0.0);
    console.fingerDown(2, 900, 500, 0.0);  // a second finger lands while the first is still down
    InputEvents a = frameOf(console);
    check(a.pointer.size() == 1 && console.cursorX() == 100, "second finger: ignored while the first is down");
    console.fingerMotion(2, 920, 520, 0.1);
    InputEvents b = frameOf(console);
    check(b.pointer.empty() && console.cursorX() == 100, "second finger: its motion is ignored too");
    console.fingerUp(1, 100, 100, 0.2);
    frameOf(console);
    console.fingerDown(2, 900, 500, 0.3);  // now free to be tracked
    InputEvents c = frameOf(console);
    check(c.pointer.size() == 1 && console.cursorX() == 900, "second finger: tracked once the first lifts");
}

void testLeftButtonReflectsHeldState() {
    ConsoleInput console;
    console.buttonDown(static_cast<int>(ConsoleButton::Cross));
    check(frameOf(console).leftButton, "leftButton: true while confirm is held");
    console.buttonUp(static_cast<int>(ConsoleButton::Cross));
    check(!frameOf(console).leftButton, "leftButton: false once it is let go");

    console.fingerDown(1, 10, 10, 0.0);  // the menus: a landing finger holds it too
    check(frameOf(console).leftButton, "leftButton: true while a menu touch is held");
    console.fingerUp(1, 10, 10, 0.1);
    check(!frameOf(console).leftButton, "leftButton: false once the finger lifts");
}

void testMouseDxAndLeftDragDxAccumulate() {
    ConsoleInput console;
    console.buttonDown(static_cast<int>(ConsoleButton::Cross));  // held, so the move below is a drag
    frameOf(console);  // consume the press's own frame (no motion yet)
    console.stick(1.0f, 0.0f);
    InputEvents a = frameOf(console, 0.2);
    const float moved = a.mouseDX;
    check(moved > 0, "mouseDX: accumulates like the mouse would report it");
    check(a.leftDragDX == moved, "leftDragDX: the whole move counts while the button is held");

    ConsoleInput notHeld;
    notHeld.stick(1.0f, 0.0f);
    InputEvents b = frameOf(notHeld, 0.2);
    check(b.mouseDX > 0 && b.leftDragDX == 0, "leftDragDX: stays zero when nothing is held, even as mouseDX moves");
}

void testDoubleButtonDownWithoutUp() {
    ConsoleInput console;
    console.buttonDown(static_cast<int>(ConsoleButton::Cross));
    console.buttonDown(static_cast<int>(ConsoleButton::Cross));  // a duplicate, no up in between
    InputEvents a = frameOf(console);
    check(a.pointer.size() == 1, "double down: only one press, not two");
    console.buttonUp(static_cast<int>(ConsoleButton::Cross));
    InputEvents b = frameOf(console);
    check(b.pointer.size() == 1 && !b.pointer[0].leftDown, "double down: a plain release still follows");
}

void testFingerIdMismatch() {
    ConsoleInput console;
    console.fingerDown(1, 100, 100, 0.0);
    frameOf(console);
    console.fingerMotion(2, 200, 200, 0.1);  // the wrong id
    InputEvents a = frameOf(console);
    check(a.pointer.empty() && console.cursorX() == 100, "finger id mismatch: motion from an untracked id is ignored");
    console.fingerUp(2, 200, 200, 0.2);  // the wrong id again
    InputEvents b = frameOf(console);
    check(b.pointer.empty() && console.touchHeld(), "finger id mismatch: a release from an untracked id is ignored too");
    console.fingerUp(1, 100, 100, 0.3);  // the real one
    InputEvents c = frameOf(console);
    check(c.pointer.size() == 1 && !c.pointer[0].leftDown, "finger id mismatch: the tracked finger still releases normally");
}

void testSetPlayingFlipWhileFingerDown() {
    ConsoleInput console;  // menus: the finger down is a press
    console.fingerDown(1, 50, 50, 0.0);
    frameOf(console);
    console.setPlaying(true);  // a level starts while the finger is still on the screen
    InputEvents a = frameOf(console);
    check(a.pointer.size() == 1 && !a.pointer[0].leftDown, "setPlaying flip: the held press is released cleanly");
    check(!console.touchHeld(), "setPlaying flip: not held afterwards");
    // The same finger id, now untracked, does nothing odd if it moves or lifts.
    console.fingerMotion(1, 60, 60, 0.1);
    console.fingerUp(1, 60, 60, 0.2);
    InputEvents b = frameOf(console);
    check(b.pointer.empty(), "setPlaying flip: the stale finger id is inert afterwards");
}

void testSetTextInputFlipWhileConfirmHeld() {
    ConsoleInput console;
    console.buttonDown(static_cast<int>(ConsoleButton::Cross));  // presses, text input off
    InputEvents a = frameOf(console);
    check(a.pointer.size() == 1 && a.pointer[0].leftDown, "text input flip: the initial press happened");
    console.setTextInput(true);  // a field takes the keyboard while confirm is still held
    InputEvents b = frameOf(console);
    check(b.pointer.size() == 1 && !b.pointer[0].leftDown, "text input flip: the stuck press is released");
    console.buttonUp(static_cast<int>(ConsoleButton::Cross));
    InputEvents c = frameOf(console);
    check(c.pointer.empty(), "text input flip: letting go afterwards presses or releases nothing more");
}

}  // namespace

int main() {
    testConfirmButtonPerSettings();
    testBackAndStartEscapeOncePerPress();
    testConfirmWhileTextInputGivesReturnNoPress();
    testConfirmPressReleaseWithinOneFrame();
    testStickMovesCursorScaledAndClamped();
    testDpadMoves();
    testMenuTouchPressDragRelease();
    testPlayModeDragNeverPresses();
    testPlayModeTapPressesAndReleasesAtTheLandingPoint();
    testSlowOrLongTouchDoesNotPress();
    testSecondFingerIgnored();
    testLeftButtonReflectsHeldState();
    testMouseDxAndLeftDragDxAccumulate();
    testDoubleButtonDownWithoutUp();
    testFingerIdMismatch();
    testSetPlayingFlipWhileFingerDown();
    testSetTextInputFlipWhileConfirmHeld();
    if (failures == 0) {
        std::puts("all console input tests passed");
    }
    return failures == 0 ? 0 : 1;
}
