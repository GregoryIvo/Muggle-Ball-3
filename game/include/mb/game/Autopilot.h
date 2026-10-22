#pragma once

#include "mb/game/Gameplay.h"

#include <functional>
#include <vector>

namespace mb {

// A player that needs no hands: launches at once and keeps the bat where the ball that comes down first will meet it, and fires its
// weapon (a click every frame; the session lets one shot through in each shot interval) when something it can hurt is straight ahead
// of the bat.
// It varies where on the bat the ball is met from hit to hit, or every ball would go straight back up its own column.
// For playing a level headless (tests, the smoke run, scripted visual checks); it is no model of a good player (aiming
// at the nearest brick instead lost more balls and cleared fewer bricks).
class Autopilot {
public:
    // The input for the next frame.
    GameInput decide(const GameSession& session) const;
};

struct PlayResult {
    std::vector<GameEvent> events;  // everything that happened, in order
    double seconds = 0;             // how long it played
};

// Plays a session with the Autopilot in frames of 1/60 s until `seconds` have passed, the level is complete or the
// game is over. `afterFrame`, if given, is called after each frame (to check the state, or to record it).
PlayResult autoplay(GameSession& session, double seconds,
                    const std::function<void(const GameSession&)>& afterFrame = {});

}  // namespace mb
