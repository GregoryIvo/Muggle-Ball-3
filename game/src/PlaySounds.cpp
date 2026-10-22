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
        case EventType::BatLeft:
            sounds_.play("bat_dead", 1, pan);
            break;
        case EventType::LevelComplete:
            sounds_.play("endlevel");
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
