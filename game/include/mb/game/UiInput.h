#pragma once

#include "mb/engine/Math.h"

#include <string>
#include <vector>

namespace mb {

// The keys the menus use; everything else is `Other`, which still counts as "any key" on the pause screen.
enum class UiKey { Escape, Enter, Backspace, Other };

// One frame of input for the menus, in target pixels (the units of Renderer::drawableSize, origin top-left). The
// app builds it from the window's InputEvents; tests write it by hand.
struct UiInput {
    // Every pointer event since the last frame, oldest first. Replaying them one by one keeps a click that begins
    // and ends within one frame as a press and a release. Empty when the pointer did nothing.
    struct Pointer {
        Vec2 position;
        bool down = false;  // the left button, just after the event
    };
    std::vector<Pointer> pointer;
    std::vector<UiKey> keys;  // in order
    std::string text;         // UTF-8 typed since the last frame
};

}  // namespace mb
