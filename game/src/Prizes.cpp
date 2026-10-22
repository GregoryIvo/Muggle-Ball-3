#include "mb/game/Prizes.h"

#include <algorithm>
#include <cctype>

namespace mb {

namespace {

// The table. Weights are the second study's counts of caught prizes by score jump (levels 2 to 83, about 1,320 of the 1,355
// catches), a value shared by two prizes split by what the footage shows of each; the Next Level stars, about 70 of the 109 jumps
// of +120, are no part of it (PrizeRules::starChanceLast). 110, 120 and 150 are also the value of bricks, so the counts of
// Increase Bat Size, Explosive Ball and Normal Ball may be too high. [I] for all of it; docs/GAMEPLAY.md section 15 has the table
// and its basis.
//   777  Bonus 146 / Life 7: the hearts rose from 2 to 6 in three hours, with two lost
//   120  Slowdown 22 / Normal Ball 17, of the 39 that are not stars
//   280  Increase Ball Speed 83 / Night 5: 83 of the 88 showed no darkening
//   180  Big Ball 40 / Decrease Bat Size 26; 190 Multiball 40 / Earthquake 25
//   Meteorite Rain is worth 4 points, which a score jump hides: 30 is a guess.
constexpr PrizeInfo kInfo[kPrizeKindCount] = {
    {PrizeKind::Bonus, "bonus", "Bonus", "", false, 0, 0, 146},
    {PrizeKind::Life, "life", "Life", "", false, 0, 0, 7},
    {PrizeKind::Skull, "scull", "Skull", "", true, 0, 0, 2},
    {PrizeKind::IncBat, "incbat", "IncBat", "", false, 0, 0, 135},
    {PrizeKind::DecBat, "decbat", "DecBat", "", true, 0, 0, 26},
    {PrizeKind::SpeedUp, "speedup", "SpeedUp", "", true, 0, 0, 83},
    {PrizeKind::SlowDown, "slowdown", "SlowDown", "", false, 0, 0, 22},
    {PrizeKind::BigBall, "bigball", "BigBall", "big", false, 20, 0, 40},
    {PrizeKind::SmallBall, "smallball", "Weak", "weak", true, 20, 0, 6},
    {PrizeKind::PowerBall, "powerfull", "PowerBall", "Powerful", false, 20, 0, 77},
    {PrizeKind::NormalBall, "commonball", "NormalBall", "", true, 0, 0, 17},
    {PrizeKind::ExplosiveBall, "explosiveball", "ExplosiveBall", "", false, 0, 1, 81},
    {PrizeKind::Drunken, "drunken", "Drunken Ball", "drunken", true, 30, 0, 8},
    {PrizeKind::Multiball, "tripple", "Multiball", "", false, 0, 0, 40},
    {PrizeKind::Magnet, "magnit", "Magnet", "Magnit", false, 30, 0, 58},
    // Rubber.png exists, but the HUD showed no icon for the Yo-Yo (V2 126-133); its 7.5 s is one case.
    {PrizeKind::YoYo, "pingpong", "YoYo", "", false, 7.5, 0, 11},
    {PrizeKind::Wall, "wall", "Wall", "wall", false, 30, 0, 39},
    {PrizeKind::Night, "night", "Night", "Night", true, 10, 0, 5},
    {PrizeKind::Gun, "gun", "Gun", "Gun", false, 0, 12, 64},
    {PrizeKind::Cannon, "cannon", "Cannon", "Cannon", false, 0, 6, 54},
    {PrizeKind::Bazooka, "bazuka", "Bazooka", "Bazuka", false, 0, 7, 45},
    {PrizeKind::Laser, "lazer", "Laser", "Lazer", false, 0, 5, 39},
    {PrizeKind::Thunderbolt, "flash", "Thunderbolt", "", false, 0, 0, 81},
    {PrizeKind::Meteor, "meteor", "MeteoriteRain", "", false, 0, 0, 30},
    {PrizeKind::Quake, "quake", "EarthQuake", "", false, 3, 0, 25},
    {PrizeKind::Wind, "wind", "Wind", "", false, 5, 0, 87},
    {PrizeKind::Surprise, "random", "Surprise", "", false, 0, 0, 51},
    {PrizeKind::NextLevel, "endlevel", "NextLevel", "", false, 0, 0, 0},
};

bool sameName(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
}

}  // namespace

const PrizeInfo& prizeInfo(PrizeKind kind) {
    return kInfo[static_cast<std::size_t>(kind)];
}

std::optional<PrizeKind> prizeByStem(std::string_view stem) {
    for (const PrizeInfo& info : kInfo) {
        if (sameName(info.stem, stem)) {
            return info.kind;
        }
    }
    return std::nullopt;
}

std::optional<WeaponType> weaponOf(PrizeKind kind) {
    switch (kind) {
        case PrizeKind::Gun: return WeaponType::Gun;
        case PrizeKind::Cannon: return WeaponType::Cannon;
        case PrizeKind::Bazooka: return WeaponType::Bazooka;
        case PrizeKind::Laser: return WeaponType::Laser;
        default: return std::nullopt;
    }
}

PrizeKind prizeOf(WeaponType type) {
    switch (type) {
        case WeaponType::Gun: return PrizeKind::Gun;
        case WeaponType::Cannon: return PrizeKind::Cannon;
        case WeaponType::Bazooka: return PrizeKind::Bazooka;
        case WeaponType::Laser: return PrizeKind::Laser;
    }
    return PrizeKind::Gun;
}

std::uint64_t PrizeRandom::next() {
    state_ += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = state_;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

float dropChance(const PrizeRules& rules, int points) {
    for (const PrizeRules::Tier& tier : rules.tiers) {
        if (points < tier.below) {
            return tier.chance;
        }
    }
    return rules.tiers.back().chance;
}

float starChance(const PrizeRules& rules, int targetsLeft, int targetsAtStart) {
    if (targetsAtStart <= 0) {
        return 0;
    }
    const int window =
        std::max(rules.starWindowMin, static_cast<int>(rules.starWindowShare * static_cast<float>(targetsAtStart)));
    if (targetsLeft >= window) {
        return 0;
    }
    return rules.starChanceLast * (1.0f - static_cast<float>(std::max(targetsLeft, 0)) / static_cast<float>(window));
}

PrizeKind pickPrize(double u, PrizePool pool) {
    const auto allowed = [pool](const PrizeInfo& info) {
        if (info.weight <= 0) {
            return false;
        }
        return pool == PrizePool::All || (!info.bad && (pool == PrizePool::GoodOnly || info.kind != PrizeKind::Surprise));
    };
    int total = 0;
    for (const PrizeInfo& info : kInfo) {
        total += allowed(info) ? info.weight : 0;
    }
    int pick = std::min(static_cast<int>(u * total), total - 1);
    for (const PrizeInfo& info : kInfo) {
        if (!allowed(info)) {
            continue;
        }
        if (pick < info.weight) {
            return info.kind;
        }
        pick -= info.weight;
    }
    return PrizeKind::Bonus;
}

}  // namespace mb
