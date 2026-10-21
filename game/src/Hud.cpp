#include "mb/game/Hud.h"

#include "mb/game/UiLayout.h"

#include <algorithm>

namespace mb {

namespace {

// Everything below is in the menus' reference pixels (1024x768), measured on the original's 800x600 screenshots
// (reference/original-level1-pirates.jpg and original-saloon.jpg, scaled by 1.28).
constexpr float kHeartSize = 40;    // Icons/Heart.png is 44x40, drawn square
constexpr float kHeartLeft = 11;
constexpr float kHeartTop = 9;
constexpr float kHeartPitch = 44;   // from one heart to the next, downwards
constexpr int kMaxHearts = 12;
constexpr int kTextSize = 22;       // the font size that makes "POINTS : 1529" the width it has in the screenshots
constexpr float kTextMargin = 15;
constexpr float kTextBottom = 18;   // the middle of the text's line, up from the bottom edge
constexpr float kIconSize = 48;     // Icons/*.png are 76x76
constexpr float kIconPitch = 54;
constexpr float kIconMargin = 16;
constexpr float kIconTop = 12;

// `format` with its first "%d" (the string table writes numbers C-style) replaced by the number.
std::string withNumber(const std::string& format, int number) {
    std::string text = format;
    const std::size_t at = text.find("%d");
    return at == std::string::npos ? text : text.replace(at, 2, std::to_string(number));
}

std::string line(const UiAssets& assets, const char* key, int number) {
    const std::string* format = assets.strings().find(key);
    return withNumber(format ? *format : std::string(key) + " : %d", number);
}

void drawImage(UiAssets& assets, const char* name, Rect rect, std::vector<Sprite>& out) {
    if (const UiAssets::Image* image = assets.image(name)) {
        Sprite sprite;
        sprite.x = rect.x;
        sprite.y = rect.y;
        sprite.width = rect.w;
        sprite.height = rect.h;
        sprite.texture = image->texture;
        out.push_back(sprite);
    }
}

}  // namespace

void drawHud(UiAssets& assets, const GameHud& hud, int width, int height, std::vector<Sprite>& out) {
    const float s = UiLayout::fit(width, height).scale();
    const auto w = static_cast<float>(width), h = static_cast<float>(height);

    for (int i = 0; i < std::min(hud.lives, kMaxHearts); ++i) {
        drawImage(assets, "Icons/Heart.png",
                  {kHeartLeft * s, (kHeartTop + kHeartPitch * static_cast<float>(i)) * s, kHeartSize * s, kHeartSize * s}, out);
    }

    for (std::size_t i = 0; i < hud.powerUps.size(); ++i) {
        drawImage(assets, ("Icons/" + hud.powerUps[i]).c_str(),
                  {w - (kIconMargin + kIconSize) * s, (kIconTop + kIconPitch * static_cast<float>(i)) * s, kIconSize * s, kIconSize * s}, out);
    }

    const UiAssets::FontChoice choice = assets.font(assets.strings().resolve("FontName"), kTextSize);
    if (!choice.font) {
        return;
    }
    TextParams params;
    params.scale = choice.scale * s;
    const float lineHeight = static_cast<float>(choice.font->lineHeight()) * params.scale;
    const float top = h - kTextBottom * s - lineHeight * 0.5f;
    params.align = HAlign::Left;
    choice.font->draw(out, line(assets, "Scores", hud.score), {kTextMargin * s, top}, params);
    params.align = HAlign::Right;
    choice.font->draw(out, line(assets, "Level", hud.level), {w - kTextMargin * s, top}, params);
}

void drawPause(UiAssets& assets, int width, int height, std::vector<Sprite>& out) {
    const float s = UiLayout::fit(width, height).scale();
    const auto w = static_cast<float>(width), h = static_cast<float>(height);

    Sprite dim;  // texture 0 draws the tint as a plain colour
    dim.width = w;
    dim.height = h;
    dim.tint = {0, 0, 0, 0.45f};
    out.push_back(dim);

    const std::string face = assets.strings().resolve("FontName");
    TextParams params;
    params.align = HAlign::Center;
    params.shadowColor = {0, 0, 0, 0.56f};
    const auto centred = [&](const char* key, int size, Color color, float y) {
        const UiAssets::FontChoice choice = assets.font(face, size);
        if (!choice.font) {
            return;
        }
        params.scale = choice.scale * s;
        params.color = color;
        params.shadowOffset = {2 * s, 2 * s};
        choice.font->draw(out, assets.strings().resolve(key), {w * 0.5f, y * s + (h - 768 * s) * 0.5f}, params);
    };
    centred("Pause", 40, {1, 1, 0, 1}, 280);
    centred("PressAnyKey", 18, {1, 1, 1, 1}, 372);
    centred("PressEscape", 18, {1, 1, 1, 1}, 408);
}

}  // namespace mb
