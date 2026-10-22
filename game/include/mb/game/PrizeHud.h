#pragma once

#include "mb/game/Gameplay.h"
#include "mb/game/Hud.h"

#include <span>
#include <string_view>
#include <vector>

namespace mb {

// What the prizes put on the HUD (docs/GAMEPLAY.md sections 6 and 16): the icons of the effects in force with their numbers, and
// the caption of the prize caught last.

// The icon stack, oldest first: each effect that has an icon (PrizeInfo::icon), with the seconds it has left (counted up, so the
// number falls by one each second and reads 1 until the effect ends) or the shots it has left. The icon's ring is green for a
// good prize and red for a bad one, as the icon files draw it; the number's green is the same for both (V2 112.2 [M]).
std::vector<PowerUp> powerUpsOf(std::span<const ActiveEffect> effects);

// A caption shows at once when a prize is caught, strong for about 1 s and fading out by about 2.2 s [M] (section 6).
inline constexpr double kCaptionStrongSeconds = 1.0;
inline constexpr double kCaptionSeconds = 2.2;
// How strong a caption is `age` seconds after the catch, 1 to 0.
float captionAlpha(double age);

// The caption of the prize caught last. A catch replaces the one on show, but the gift of a Surprise (a catch worth no points) does
// not: the footage shows "Surprise" and nothing else [M].
class PrizeCaption {
public:
    // `seconds` after the last call, with the events since.
    void update(std::span<const GameEvent> events, double seconds);

    // The string table's key for the caption, empty when none is on show, and how strong it is.
    std::string_view key() const { return age_ < kCaptionSeconds ? key_ : std::string_view(); }
    float alpha() const { return captionAlpha(age_); }

private:
    std::string_view key_;
    double age_ = kCaptionSeconds;
};

}  // namespace mb
