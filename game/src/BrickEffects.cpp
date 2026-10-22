#include "mb/game/BrickEffects.h"

namespace mb {

std::span<const std::string_view> explosionEffects(int exploseType) {
    static constexpr std::string_view kBall[] = {"exploseball"};
    static constexpr std::string_view kSmall[] = {"explosesmall"};
    static constexpr std::string_view kMeteor[] = {"explosemeteor"};
    switch (exploseType) {
        case 3: return kBall;
        case 6: return kSmall;
        case 9: return kMeteor;
        default: return {};
    }
}

}  // namespace mb
