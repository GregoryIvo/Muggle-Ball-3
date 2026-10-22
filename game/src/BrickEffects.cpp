#include "mb/game/BrickEffects.h"

#include <algorithm>
#include <cctype>

namespace mb {

namespace {

constexpr std::string_view kIsland = "ostrov.jpg";  // the sand of the pirate levels' islands (75 objects in 8 levels)

bool isIsland(const GameMaterial& material) {
    return std::ranges::equal(material.name, kIsland,
                              [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; });
}

}  // namespace

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

std::span<const std::string_view> explosionEffects(const GameMaterial* material) {
    return material == nullptr || isIsland(*material) ? std::span<const std::string_view>{} : explosionEffects(material->exploseType);
}

}  // namespace mb
