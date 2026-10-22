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
        case EventType::PrizeDropped:  // (the prizes' sounds are the presentation's: step 5, stage 2)
        case EventType::PrizeCaught:
        case EventType::PrizeMissed:
        case EventType::EffectStarted:
        case EventType::EffectEnded:
        case EventType::StrikeGranted:
        case EventType::BallStuck:
        case EventType::Blast:
            break;
        case EventType::BatLeft:
            sounds_.play("bat_dead", 1, pan);
            break;
        case EventType::LevelComplete:
            sounds_.play("endlevel");
            break;
        case EventType::BallLost:
        case EventType::BallFreed:
        case EventType::LifeLost:
        case EventType::BatEntered:
        case EventType::GameOver:
            break;
    }
}

}  // namespace mb
