#pragma once

#include "mb/engine/Renderer.h"
#include "mb/game/UiAssets.h"
#include "mb/game/UiForm.h"

#include <vector>

namespace mb {

struct UiDrawOptions {
    // Opacity of the form, 0 to 1: the menu code raises it over UiForm::fadeTimeMs() when a form appears. Widgets
    // with `noFading` (the logo) are always fully opaque.
    float fade = 1;
    // Seconds the scrolling images (the sky of the background form, see SpriteSheet::scroll) have run for.
    double scrollSeconds = 0;
    // A text field that has the keyboard: a caret is drawn after its text. Null for none.
    const UiWidget* caret = nullptr;
};

// Appends the sprites that draw a form on the target it was last laid out for (UiForm::layout), in drawing order:
// widgets in document order, each with its drop shadow, image and text, then its children. A slider's knob is the
// exception: it goes on top of the track, a child widget, so it comes after the slider's children.
//
// What each widget shows:
//   - the image frame for its state (UiWidget::frame()), tinted by `color`, turned by `rotation` about its centre;
//     a drop shadow, the same frame tinted by `shadowColor` and moved by `shadowOffset`, when it has one;
//   - while hovered it grows by `mouseOverOffset` on every side; while pressed the image and text move by
//     `pressOffset`;
//   - its text where its placement puts it, in the font and colour it inherited, with a drop shadow when its
//     fontShadow is not zero (in `shadowColor`);
//   - a progress bar shows the part of its image to the left of `fraction()`;
//   - an image with a `tile` count or a `scroll` speed shows that many repeats of itself, slid by the speed times
//     the time (only for a picture that is not a sprite sheet).
// Images and fonts that cannot be found are skipped (UiAssets logs them once).
void drawForm(const UiForm& form, UiAssets& assets, std::vector<Sprite>& out, const UiDrawOptions& options = {});

}  // namespace mb
