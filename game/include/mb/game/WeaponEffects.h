#pragma once

#include "mb/game/Gameplay.h"
#include "mb/game/ParticleSystem.h"
#include "mb/game/Weapons.h"

#include <span>
#include <unordered_map>

namespace mb {

// What the weapons and the strikes do in the particle system (docs/GAMEPLAY.md section 18.12): the beam of the Laser, the
// lightning of a Thunderbolt, the trails of a rocket and a meteor as they fly, the burst of a cannonball and a rocket, the ring a
// meteor makes on the water. No GPU and no window: it only asks for effects (the one ParticleSystem PlayScene owns, which draws
// them with the rest) from what the session reports. The models of what flies are WeaponScene's.
class WeaponEffects {
public:
    // `particles` must outlive it. `rules` are the session's (how long a beam lives).
    WeaponEffects(ParticleSystem& particles, const WeaponRules& rules);

    // Asks for what the events call for, and carries each trail on with its projectile (the session's projectiles()), `lead`
    // seconds past the session's last step.
    void update(std::span<const GameEvent> events, std::span<const Projectile> projectiles, double lead);

private:
    void follow(std::span<const Projectile> projectiles, double lead);

    ParticleSystem& particles_;
    WeaponRules rules_;
    std::unordered_map<int, EffectId> trails_;  // by Projectile::id
};

}  // namespace mb
