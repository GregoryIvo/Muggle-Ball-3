#include "mb/game/PrizeHud.h"

#include <algorithm>
#include <cmath>

namespace mb {

std::vector<PowerUp> powerUpsOf(std::span<const ActiveEffect> effects) {
    std::vector<PowerUp> icons;
    for (const ActiveEffect& effect : effects) {
        const PrizeInfo& info = prizeInfo(effect.kind);
        if (!info.icon.empty()) {
            icons.push_back({std::string(info.icon), weaponOf(effect.kind) ? effect.shots : static_cast<int>(std::ceil(effect.timeLeft))});
        }
    }
    return icons;
}

float captionAlpha(double age) {
    if (age < 0 || age >= kCaptionSeconds) {
        return 0;
    }
    return age <= kCaptionStrongSeconds ? 1.0f
                                        : static_cast<float>(1 - (age - kCaptionStrongSeconds) / (kCaptionSeconds - kCaptionStrongSeconds));
}

void PrizeCaption::update(std::span<const GameEvent> events, double seconds) {
    age_ += seconds;
    for (const GameEvent& event : events) {
        if (event.type == EventType::PrizeCaught && event.prize && event.points > 0) {
            key_ = prizeInfo(*event.prize).caption;
            age_ = 0;
        }
    }
}

}  // namespace mb
