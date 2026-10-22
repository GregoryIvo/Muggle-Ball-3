#pragma once

#include "mb/game/Gameplay.h"
#include "mb/game/SoundLibrary.h"

namespace mb {

// What a game session sounds like: each GameEvent as the sound the original plays for it, through the SoundLibrary
// (which holds back a sound that is repeated inside its replay delay), panned by where on the board it happened.
//
//     PlaySounds sounds(*audio.sounds());
//     for (const GameEvent& event : session.takeEvents()) { sounds.play(event); }
//
// A brick that is hit and one that is destroyed make their material's `hit` and `dead` sounds; the ball's sounds are
// sounds.xml's ballHitBat (the bat, and the launch, which is the bat's hit), ballHitBoard (the walls) and ballHitBall.
// The data names ballHitBat and bat_dead for no material, so the events they go with are inferred (docs/OBJECTS.md
// section 3): bat_dead as the bat flies off after the last ball is lost, endlevel as the level is won (it is also the
// sound of the prize that does the same). A prize that is caught makes its material's `dead` sound (a few name a sound with no
// file, which is silent), the Magnet's catch ballMagnited, the Yo-Yo's string turning the ball back ballRubbered and the
// Explosive Ball's blast explosion_ball.
class PlaySounds {
public:
    explicit PlaySounds(SoundLibrary& sounds) : sounds_(sounds) {}

    void play(const GameEvent& event);

private:
    SoundLibrary& sounds_;
};

}  // namespace mb
