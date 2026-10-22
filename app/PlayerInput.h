#pragma once

#include "mb/engine/Window.h"
#include "mb/game/GameCamera.h"
#include "mb/game/Gameplay.h"

namespace mb_app {

// How a person plays (in app/ because the keys are SDL's): the cursor is the bat (the point of the board under it, as the game camera sees it), and the left button
// going down, or Space, launches the ball. One per level being played; it remembers the button between frames.
class PlayerInput {
public:
    // What the window polled in a frame, for a target of `width` x `height` pixels (the units of InputEvents::mouseX/Y).
    // A button that was already down when the level began or the pause ended is not a click: only a press seen
    // afterwards launches. The target is empty where the cursor is above the horizon (the bat stays where it was).
    mb::GameInput read(const mb::InputEvents& input, int width, int height, const mb::Camera& camera);

private:
    bool primed_ = false;
    bool pointerDown_ = false;
};

}  // namespace mb_app
