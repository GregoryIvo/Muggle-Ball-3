#pragma once

#include "mb/engine/Math.h"

namespace mb {

// A rectangle in pixels: top-left corner and size, y running down (like Sprite).
struct Rect {
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;

    float right() const { return x + w; }
    float bottom() const { return y + h; }
    Vec2 center() const { return {x + w * 0.5f, y + h * 0.5f}; }
    // Left and top edges are inside, right and bottom edges are not.
    bool contains(Vec2 p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
};

// Where the 2D UI goes on a render target.
//
// The original menus are laid out on a 4:3 screen of 1024x768 "reference pixels", and every position and size
// in their data is a fraction of that screen. On a target of another shape the 4:3 area keeps its proportions
// and sits centred, with bars (the clear colour) at the sides or at top and bottom. The data counts y upwards
// from the bottom edge; pixels count downwards from the top, so `rect` and `point` flip it.
class UiLayout {
public:
    static constexpr float kReferenceWidth = 1024;
    static constexpr float kReferenceHeight = 768;

    // The largest 4:3 area that fits a target of this size in pixels, centred in it.
    static UiLayout fit(int targetWidth, int targetHeight);

    const Rect& screen() const { return screen_; }
    // Target pixels per reference pixel: sizes given in reference pixels (offsets, font sizes) are multiplied by it.
    float scale() const { return screen_.w / kReferenceWidth; }

    // A rectangle from the data's convention: `pos` is the bottom-left corner and `size` the extent, both as
    // fractions of the screen with y up.
    Rect rect(Vec2 pos, Vec2 size) const;
    // A point from the same convention (fractions of the screen, y up) to target pixels.
    Vec2 point(Vec2 pos) const;

private:
    Rect screen_{0, 0, kReferenceWidth, kReferenceHeight};
};

}  // namespace mb
