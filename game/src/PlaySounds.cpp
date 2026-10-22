#include "mb/game/PlaySounds.h"

#include <algorithm>

namespace mb {

namespace {

// Half the board's width: the pan of a sound at a side wall.
constexpr float kPanWidth = 20.0f;

float panOf(const GameEvent& event) {
    return std::clamp(event.position.x / kPanWidth, -1.0f, 1.0f);
}

}  // namespace

void PlaySounds::play(const GameEvent& event) {
    const float pan = panOf(event);
    switch (event.type) {
        case EventType::BallLaunched:
        case EventType::BatHit:
            sounds_.play("ballHitBat", 1, pan);
            break;
        case EventType::BoardHit:
            sounds_.play("ballHitBoard", 1, pan);
            break;
        case EventType::BallsHit:
            sounds_.play("ballHitBall", 1, pan);
            break;
        case EventType::BrickHit:
            if (event.material) {
                sounds_.play(*event.material, MaterialSound::Hit, 1, pan);
            }
            break;
        case EventType::BrickDestroyed:
            if (event.material) {
                sounds_.play(*event.material, MaterialSound::Dead, 1, pan);
            }
            break;
        case EventType::ActorFell:
            if (event.material && event.points > 0) {  // a piece that was not worth points is not a brick
                sounds_.play(*event.material, MaterialSound::Dead, 1, pan);
            }
            break;
        case EventType::CannonFired:
            sounds_.play("brick_cannon_shoot", 1, pan);
            break;
        case EventType::RocketBurst:
            sounds_.play("firerocket_dead", 1, pan);
            break;
        case EventType::BallEntered:
        case EventType::BallEmerged:
            sounds_.play("teleport", 1, pan);
            break;
        case EventType::PrizeCaught:
            // The star's sound is the level's, which LevelComplete plays in the same step.
            if (event.material && event.prize != PrizeKind::NextLevel) {
                sounds_.play(*event.material, MaterialSound::Dead, 1, pan);
            }
            break;
        case EventType::BallStuck:
            sounds_.play("ballMagnited", 1, pan);
            break;
        case EventType::BallRubbered:
            sounds_.play("ballRubbered", 1, pan);
            break;
        case EventType::Blast:
            sounds_.play("explosion_ball", 1, pan);
            break;
        case EventType::PrizeDropped:  // the data names no sound for a prize that appears, falls, fizzles or runs out
        case EventType::PrizeMissed:
        case EventType::EffectStarted:
        case EventType::EffectEnded:
        case EventType::StrikeGranted:  // (the strikes' own sounds come with them)
            break;
        case EventType::BatLeft:
            sounds_.play("bat_dead", 1, pan);
            break;
        case EventType::LevelComplete:
            sounds_.play("endlevel");
            break;
        case EventType::ShotFired:  // (the weapons' sounds, the `period` and `dead` of the projectile's material, are the next stage's)
        case EventType::ProjectileHit:
        case EventType::ProjectileExploded:
        case EventType::ProjectileGone:
        case EventType::LaserBeam:
        case EventType::BoltStruck:
        case EventType::MeteorFalling:
        case EventType::MeteorImpact:
            break;
        case EventType::DynamiteBlast:  // (the charge makes its own sound as it is destroyed)
        case EventType::ShotHit:
        case EventType::RocketLaunched:
        case EventType::BallLost:
        case EventType::BallFreed:
        case EventType::LifeLost:
        case EventType::BatEntered:
        case EventType::GameOver:
            break;
    }
}

}  // namespace mb
