#pragma once

#include "mb/engine/Model.h"
#include "mb/engine/Renderer.h"
#include "mb/game/Gameplay.h"
#include "mb/game/LevelScene.h"
#include "mb/game/ModelCache.h"
#include "mb/game/ParticleSystem.h"
#include "mb/game/WeaponEffects.h"

#include <array>
#include <span>
#include <vector>

namespace mb {

// What the weapons and the strikes look like in play (docs/GAMEPLAY.md section 18.12): the bullets, the cannonball, the rocket and the
// meteors in the air, each turning in its model's own animation, with a shadow on the water; and, through WeaponEffects, the particle
// effects they make. The barrels on the bat are BatBarrels'. It reads the session and the events and keeps no rules.
class WeaponScene {
public:
    // `particles` is the scene's one system, which must outlive this.
    WeaponScene(ModelCache& cache, const MaterialLibrary& materials, const TextureIndex& textures, ParticleSystem& particles,
                const WeaponRules& rules);

    // Brings the picture up to the session: what is in the air as of its last step carried on by `lead` seconds, and what the
    // events ask of the particle system.
    void update(const GameSession& session, std::span<const GameEvent> events, double lead);

    // What to draw on top of the level: as of the last update().
    std::span<const DrawItem> items() const { return items_; }

private:
    // What a projectile is drawn as: an actor of `props_` and the model it is a frame of, and that frame, the one the origin of the
    // draw is taken from.
    struct Look {
        ActorIndex actor = 0;
        const Model* model = nullptr;
        const ModelAnimation* spin = nullptr;  // "Anim-1": a turn about one axis, over and over; none: it does not turn
        int frame = -1;
        float shadow = 0;  // across, 0 for none
    };

    void add(const Look& look, Vec3 at, double age);

    LevelScene props_;  // a bullet (left and right), the cannonball, the rocket and the meteor, each at its model's own place
    WeaponEffects effects_;
    MeshHandle quad_ = 0;
    TextureHandle shadowTexture_ = 0;
    std::array<Look, 5> looks_{};  // Left-Bullet, Right-Bullet, then by ProjectileKind: Kernel, Rocket, Meteor (a Bolt has none)
    std::vector<DrawItem> items_;
};

}  // namespace mb
