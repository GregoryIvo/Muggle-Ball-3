#include "mb/game/WeaponEffects.h"

#include <algorithm>
#include <string_view>

namespace mb {

namespace {

// ---- What each event makes (docs/GAMEPLAY.md 18.12). The particle files (Data/Particles [D]) say how each looks; the names are the
// jobs the original's code gave them, which the data does not name: the Laser particle is the laser's beam and the Lightning
// particle the Thunderbolt's [D: OBJECTS.md section 8]; the meteor names TrailMeteor [D: `particles1`] and the rocket takes the blue
// trail, the firework crate's rocket having the orange one (19.6) [I]; explosemeteor lies flat on the water [D: its `direction`].
constexpr std::string_view kBeam = "Laser";
constexpr std::string_view kBolt = "Lightning";
constexpr std::string_view kRocketTrail = "Trail/TrailBlue";
constexpr std::string_view kMeteorTrail = "TrailMeteor";
constexpr std::string_view kKernelBurst = "exploseyadro";   // as the cannon of the level's: a cannonball's burst (17.3) [D, I]
constexpr std::string_view kRocketBurst = "exploseyadro";   // "a white-orange star 4-5 units wide" [M] is this file's flash [I]
// The file's flash is 10 units across, rays and all; the footage's star is about 4 units across (a white core of 2) for the cannonball
// (V2 97.28 to 97.39) and 4 to 5 for the rocket (V2 112.84 to 112.91): the picture is that much of it [M].
constexpr float kKernelBurstScale = 0.4f;
constexpr float kRocketBurstScale = 0.45f;
constexpr std::string_view kMeteorRing = "explosemeteor";   // the white ring on the water [D]

// The beam's front runs out to the tip in WeaponRules::laserGrow (0.1 s): the Laser file's own 150 units a second would take 0.2 s for
// 30 units and 0.4 s for the 60 of its range, and the footage has about 35 percent of a beam at 62.78 and all of it by 62.88 [M, V2].
// It is at full strength for this long after the shot and then fades out by the time the rules drop it (WeaponRules::laserLife,
// 0.45 s): in the footage it is full at 0.26 s, dim at 0.4 s and gone by 0.54 [M, V1 58.88 to 59.3, the shot at about 58.76].
constexpr float kBeamHold = 0.25f;
// A bolt of lightning is shown about 0.1 s [M: each of the five of V2 408.0 to 408.7], at full strength, and drops out in a few frames.
constexpr float kBoltHold = 0.1f;
constexpr float kBoltFade = 0.05f;
// The ring lies on the water, wherever the meteor came down (on a brick it still shows on the water under it): the floor is at y 0 in
// every level, and the file lifts a flat flash a little above what it lies on. It swells to its 10 units at the file's 50 units a second
// (0.2 s) and fades as it does, about 0.4 s in all [M, V2 859.5 to 865]; the file's `fade_time_msec` alone, 0.2 s, would end it as
// soon as it had swollen, so its life is asked for.
constexpr float kFloor = 0.0f;
constexpr float kRingSeconds = 0.4f;
// A rocket's trail comes out of its tail, which is this far behind the middle of its capsule (the model runs from -0.79 to +1.0) [D].
constexpr float kRocketTail = 0.8f;
// The footage's rocket is a small red body with a short glow behind it and no streak (V2 112.62 to 112.75): the trail's 2.5 units are
// drawn at this share of their size [M, I].
constexpr float kRocketTrailScale = 0.35f;

}  // namespace

WeaponEffects::WeaponEffects(ParticleSystem& particles, const WeaponRules& rules) : particles_(particles), rules_(rules) {}

void WeaponEffects::update(std::span<const GameEvent> events, std::span<const Projectile> projectiles, double lead) {
    for (const GameEvent& event : events) {
        switch (event.type) {
            case EventType::LaserBeam:
                particles_.beam(kBeam, event.position, event.end,
                                {.seconds = kBeamHold,
                                 .fade = std::max(static_cast<float>(rules_.laserLife) - kBeamHold, 0.0f),
                                 .grow = static_cast<float>(rules_.laserGrow)});
                break;
            case EventType::BoltStruck:
                // A target above the top edge of the screen has no lightning to see.
                if (event.end.y >= event.position.y) {
                    particles_.beam(kBolt, event.end, event.position, {.seconds = kBoltHold, .fade = kBoltFade});
                }
                break;
            case EventType::MeteorImpact:
                particles_.spawn(kMeteorRing, {event.position.x, kFloor, event.position.z}, {.seconds = kRingSeconds});
                break;
            case EventType::ProjectileExploded:
                if (event.prize == PrizeKind::Cannon) {
                    particles_.spawn(kKernelBurst, event.position, {.scale = kKernelBurstScale});
                } else if (event.prize == PrizeKind::Bazooka) {
                    particles_.spawn(kRocketBurst, event.position, {.scale = kRocketBurstScale});
                }
                break;
            default:
                break;
        }
    }
    follow(projectiles, lead);
}

// Each rocket and meteor in the air leaves a trail; the trail stops when its projectile is gone, and what it left fades out.
void WeaponEffects::follow(std::span<const Projectile> projectiles, double lead) {
    for (const Projectile& projectile : projectiles) {
        if (projectile.kind != ProjectileKind::Rocket && projectile.kind != ProjectileKind::Meteor) {
            continue;
        }
        Vec3 at = projectile.position + projectile.velocity * static_cast<float>(lead);
        if (projectile.kind == ProjectileKind::Rocket) {
            at = at - normalize(projectile.velocity) * kRocketTail;
        }
        EffectId& trail = trails_[projectile.id];
        if (!particles_.alive(trail)) {
            trail = projectile.kind == ProjectileKind::Rocket ? particles_.attach(kRocketTrail, at, {.scale = kRocketTrailScale})
                                                               : particles_.attach(kMeteorTrail, at);
        }
        particles_.moveTo(trail, at);
    }
    std::erase_if(trails_, [&](const auto& entry) {
        const bool flying = std::ranges::any_of(projectiles, [&](const Projectile& projectile) { return projectile.id == entry.first; });
        if (!flying) {
            particles_.stop(entry.second);
        }
        return !flying;
    });
}

}  // namespace mb
