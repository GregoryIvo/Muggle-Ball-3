#include "mb/game/UiLayout.h"

#include <algorithm>

namespace mb {

UiLayout UiLayout::fit(int targetWidth, int targetHeight) {
    UiLayout layout;
    const float width = static_cast<float>(std::max(targetWidth, 1));
    const float height = static_cast<float>(std::max(targetHeight, 1));
    const float scale = std::min(width / kReferenceWidth, height / kReferenceHeight);
    layout.screen_.w = kReferenceWidth * scale;
    layout.screen_.h = kReferenceHeight * scale;
    layout.screen_.x = (width - layout.screen_.w) * 0.5f;
    layout.screen_.y = (height - layout.screen_.h) * 0.5f;
    return layout;
}

Vec2 UiLayout::point(Vec2 pos) const {
    return {screen_.x + pos.x * screen_.w, screen_.y + (1.0f - pos.y) * screen_.h};
}

Rect UiLayout::rect(Vec2 pos, Vec2 size) const {
    const Vec2 bottomLeft = point(pos);
    const float w = size.x * screen_.w, h = size.y * screen_.h;
    return {bottomLeft.x, bottomLeft.y - h, w, h};
}

}  // namespace mb
