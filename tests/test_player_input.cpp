// The mouse and Space as a player's input: where the cursor puts the bat, and what is a click. No window, GPU or data.

#include "PlayerInput.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>

using namespace mb;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

constexpr int kWidth = 1024, kHeight = 768;

InputEvents at(float x, float y, bool down = false) {
    InputEvents in;
    in.mouseX = x;
    in.mouseY = y;
    in.leftButton = down;
    return in;
}

void testCursor() {
    const Camera camera;
    mb_app::PlayerInput player;
    const std::optional<float> middle = player.read(at(kWidth / 2.0f, 600), kWidth, kHeight, camera).batTargetX;
    const std::optional<float> right = player.read(at(kWidth * 0.75f, 600), kWidth, kHeight, camera).batTargetX;
    const std::optional<float> left = player.read(at(kWidth * 0.25f, 600), kWidth, kHeight, camera).batTargetX;
    check(middle && std::fabs(*middle) < 0.01f, "cursor: the middle of the window is the middle of the board");
    check(right && left && *right > 5 && *left < -5 && std::fabs(*right + *left) < 0.01f, "cursor: right and left of the middle put the bat there, evenly");
    check(*player.read(at(kWidth * 0.75f, 600), kWidth, kHeight, camera).batTargetX == *batTargetX(kWidth * 0.75f, 600, kWidth, kHeight, camera),
          "cursor: it is what batTargetX says for the window's size");
    Camera skyward;  // a camera that sees the sky: the game's looks down on the board all over the window
    skyward.lookAt({0, 10, -50}, {0, 15, 0}, 33);
    check(!player.read(at(500, 5), kWidth, kHeight, skyward).batTargetX.has_value() &&
              player.read(at(500, 750), kWidth, kHeight, skyward).batTargetX.has_value(),
          "cursor: where it points at the sky there is no target (the bat stays), and below the horizon there is");
    check(!player.read(at(500, 500), 0, 0, camera).batTargetX.has_value(), "cursor: a window with no size gives none");
}

void testLaunch() {
    const Camera camera;
    mb_app::PlayerInput player;
    check(!player.read(at(500, 500), kWidth, kHeight, camera).launch, "launch: nothing is pressed");

    InputEvents press = at(500, 500, true);
    press.pointer = {{500, 500, false}, {500, 500, true}};
    check(player.read(press, kWidth, kHeight, camera).launch, "launch: the button going down launches");
    check(!player.read(at(500, 500, true), kWidth, kHeight, camera).launch, "launch: and holding it does not, again");
    InputEvents drag = at(520, 500, true);
    drag.pointer = {{510, 500, true}, {520, 500, true}};
    check(!player.read(drag, kWidth, kHeight, camera).launch, "launch: nor does moving with it down");
    InputEvents release = at(520, 500, false);
    release.pointer = {{520, 500, false}};
    check(!player.read(release, kWidth, kHeight, camera).launch, "launch: nor does letting go");

    InputEvents quick = at(500, 500, false);
    quick.pointer = {{500, 500, true}, {500, 500, false}};
    check(player.read(quick, kWidth, kHeight, camera).launch, "launch: a click that begins and ends within one frame still counts");

    InputEvents space;
    space.keysPressed = {static_cast<std::uint32_t>(SDLK_A), static_cast<std::uint32_t>(SDLK_SPACE)};
    check(player.read(space, kWidth, kHeight, camera).launch, "launch: Space launches");
    InputEvents other;
    other.keysPressed = {static_cast<std::uint32_t>(SDLK_RETURN)};
    check(!player.read(other, kWidth, kHeight, camera).launch, "launch: another key does not");
}

void testButtonHeldAtTheStart() {
    const Camera camera;
    mb_app::PlayerInput player;
    // The button that chose the level, or the one that ended the pause (PlaySession::resume() starts the player's input over,
    // as a new level does), is still down when play begins: moving the mouse with it down is not a click.
    InputEvents moving = at(500, 500, true);
    moving.pointer = {{500, 500, true}, {505, 500, true}};
    check(!player.read(moving, kWidth, kHeight, camera).launch, "held: a button already down at the start launches nothing");
    InputEvents release = at(505, 500, false);
    release.pointer = {{505, 500, false}};
    player.read(release, kWidth, kHeight, camera);
    InputEvents again = at(505, 500, true);
    again.pointer = {{505, 500, true}};
    check(player.read(again, kWidth, kHeight, camera).launch, "held: the next press does");
}

}  // namespace

int main() {
    testCursor();
    testLaunch();
    testButtonHeldAtTheStart();
    if (failures == 0) {
        std::puts("all player input tests passed");
    }
    return failures == 0 ? 0 : 1;
}
