#include "mb/game/UiDraw.h"

#include <algorithm>
#include <cmath>

namespace mb {

namespace {

Color withAlpha(Color c, float alpha) {
    c.a *= alpha;
    return c;
}

struct Context {
    UiAssets& assets;
    std::vector<Sprite>& out;
    float scale;  // target pixels per reference pixel
    float fade;
    double scrollSeconds;
    const UiWidget* caret;
};

// Where `rect` of the widget is drawn right now: grown on hover, and moved while pressed if asked to.
Rect shownRect(const UiWidget& w, Rect r, float scale, bool pressedOffset) {
    if (w.hovered && w.enabled && w.mouseOverOffset > 0) {
        const float grow = w.mouseOverOffset * scale;
        r = {r.x - grow, r.y - grow, r.w + 2 * grow, r.h + 2 * grow};
    }
    if (pressedOffset && w.pressed) {
        r.x += w.pressOffset.x * scale;
        r.y += w.pressOffset.y * scale;
    }
    return r;
}

// How far a scrolling image has slid after `seconds`, as a fraction of the picture, wrapped to 0..1 so that a long
// run keeps its precision.
float slideOf(float hundredthsPerSecond, double seconds) {
    const double turns = hundredthsPerSecond * 0.01 * seconds;
    return static_cast<float>(turns - std::floor(turns));
}

// One frame of the widget's image into `rect`, preceded by its drop shadow.
void drawImage(Context& c, const UiWidget& w, const Rect& rect, const Rect& shadowBase, float alpha) {
    const UiAssets::Image* image = c.assets.image(w.image.name);
    if (!image) {
        return;
    }
    const FrameUv uv = sheetFrameUv(w.image.columns, w.image.rows, w.frame());
    Sprite sprite;
    sprite.x = rect.x;
    sprite.y = rect.y;
    sprite.width = rect.w;
    sprite.height = rect.h;
    sprite.texture = image->texture;
    sprite.uv0 = uv.uv0;
    sprite.uv1 = uv.uv1;
    sprite.rotation = radians(w.rotation);
    if (w.kind == WidgetKind::Progress) {
        const float shown = w.fraction();
        sprite.width *= shown;
        sprite.uv1.x = uv.uv0.x + (uv.uv1.x - uv.uv0.x) * shown;
    } else if (w.image.columns == 1 && w.image.rows == 1) {
        // The sampler wraps coordinates past 1, which repeats the picture.
        if (w.image.tiled) {
            // At its own size.
            sprite.uv1 = {rect.w / (static_cast<float>(image->width) * c.scale),
                          rect.h / (static_cast<float>(image->height) * c.scale)};
        } else {
            sprite.uv1 = {w.image.tile.x, w.image.tile.y};
        }
        const Vec2 slide{slideOf(w.image.scroll.x, c.scrollSeconds), slideOf(w.image.scroll.y, c.scrollSeconds)};
        sprite.uv0 = slide;
        sprite.uv1 = {sprite.uv1.x + slide.x, sprite.uv1.y + slide.y};
    }
    if (sprite.width <= 0) {
        return;
    }

    if ((w.shadowOffset.x != 0 || w.shadowOffset.y != 0) && w.shadowColor.a > 0) {
        Sprite shadow = sprite;
        shadow.x = shadowBase.x + w.shadowOffset.x * c.scale;
        shadow.y = shadowBase.y + w.shadowOffset.y * c.scale;
        shadow.tint = withAlpha(w.shadowColor, alpha);
        c.out.push_back(shadow);
    }
    sprite.tint = withAlpha(w.color, alpha);
    c.out.push_back(sprite);
}

void drawText(Context& c, const UiWidget& w, const Rect& box, float alpha) {
    // A list's text is the format its child label shows the selected item with, not something to draw.
    const bool caret = &w == c.caret;
    if ((w.text.empty() && !caret) || w.kind == WidgetKind::List) {
        return;
    }
    const UiAssets::FontChoice choice = c.assets.font(w.fontFace, w.fontSize);
    if (!choice.font) {
        return;
    }
    TextParams params;
    params.scale = choice.scale * c.scale;
    params.color = withAlpha(w.fontColor, alpha);
    params.shadowOffset = {w.fontShadow.x * c.scale, w.fontShadow.y * c.scale};
    params.shadowColor = withAlpha(w.shadowColor, alpha);
    params.align = HAlign::Center;

    const float gap = w.textOffset * c.scale;
    const Vec2 center = box.center();
    Vec2 anchor{center.x, center.y};
    Vec2 size;
    if (w.textPlacement == TextPlacement::CenterMultiline) {
        params.maxWidth = box.w;
        anchor.y = box.y;
    } else {
        size = choice.font->measure(w.text, params);
        switch (w.textPlacement) {
            case TextPlacement::Right:
                params.align = HAlign::Left;
                anchor = {box.right() + gap, center.y - size.y * 0.5f};
                break;
            case TextPlacement::Above:
                anchor.y = box.y - gap - size.y;
                break;
            case TextPlacement::Under:
            case TextPlacement::Bottom:
                anchor.y = box.bottom() + gap;
                break;
            case TextPlacement::Center:
            case TextPlacement::CenterMultiline:
                anchor.y = center.y - size.y * 0.5f;
                break;
        }
    }
    choice.font->draw(c.out, w.text, anchor, params);
    if (caret) {
        // A bar after the last character, as tall as the lettering.
        const float width = std::max(2.0f * c.scale, 1.0f);
        const float left = params.align == HAlign::Center ? anchor.x + size.x * 0.5f : anchor.x + size.x;
        const float height = static_cast<float>(choice.font->lineHeight()) * params.scale * 0.8f;
        Sprite bar;
        bar.x = std::round(left + c.scale);
        bar.y = std::round(anchor.y + (size.y - height) * 0.5f);
        bar.width = width;
        bar.height = std::round(height);
        bar.tint = params.color;
        c.out.push_back(bar);
    }
}

void drawWidget(Context& c, const UiWidget& w, bool imageToo) {
    const float alpha = w.noFading ? 1.0f : c.fade;
    const Rect rest = shownRect(w, w.rect, c.scale, false);
    const Rect box = shownRect(w, w.rect, c.scale, true);
    if (imageToo && !w.image.empty()) {
        drawImage(c, w, box, rest, alpha);
    }
    drawText(c, w, box, alpha);
}

}  // namespace

void drawForm(const UiForm& form, UiAssets& assets, std::vector<Sprite>& out, const UiDrawOptions& options) {
    Context c{assets, out, form.screen().scale(), std::clamp(options.fade, 0.0f, 1.0f), options.scrollSeconds, options.caret};
    const std::vector<UiWidget>& widgets = form.widgets();

    std::vector<std::size_t> knobs;  // sliders whose knob waits for their children
    const auto drawKnob = [&](const UiWidget& slider) {
        if (!slider.image.empty()) {
            const Rect knob = slider.knobRect();
            drawImage(c, slider, shownRect(slider, knob, c.scale, true), shownRect(slider, knob, c.scale, false),
                      slider.noFading ? 1.0f : c.fade);
        }
    };
    for (std::size_t i = 0; i < widgets.size();) {
        while (!knobs.empty() && static_cast<std::size_t>(widgets[knobs.back()].end) <= i) {
            drawKnob(widgets[knobs.back()]);
            knobs.pop_back();
        }
        const UiWidget& w = widgets[i];
        if (!w.visible) {
            i = static_cast<std::size_t>(w.end);  // hides everything inside too
            continue;
        }
        if (w.kind == WidgetKind::Slider) {
            knobs.push_back(i);
            drawWidget(c, w, false);
        } else {
            drawWidget(c, w, true);
        }
        ++i;
    }
    while (!knobs.empty()) {
        drawKnob(widgets[knobs.back()]);
        knobs.pop_back();
    }
}

}  // namespace mb
