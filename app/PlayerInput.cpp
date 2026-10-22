#include "PlayerInput.h"

#include <SDL3/SDL_keycode.h>

#include <algorithm>

namespace mb_app {

mb::GameInput PlayerInput::read(const mb::InputEvents& input, int width, int height, const mb::Camera& camera) {
    mb::GameInput game;
    game.batTargetX = mb::batTargetX(input.mouseX, input.mouseY, static_cast<float>(width), static_cast<float>(height), camera);
    if (!primed_) {
        pointerDown_ = input.leftButton;
        primed_ = true;
    }
    // The samples are every move and button change in order, so a click that begins and ends within a frame still shows.
    for (const mb::PointerSample& sample : input.pointer) {
        game.launch = game.launch || (sample.leftDown && !pointerDown_);
        pointerDown_ = sample.leftDown;
    }
    pointerDown_ = input.leftButton;
    game.launch = game.launch || std::ranges::find(input.keysPressed, static_cast<std::uint32_t>(SDLK_SPACE)) != input.keysPressed.end();
    return game;
}

}  // namespace mb_app
