#pragma once

#include "mb/engine/Math.h"
#include "mb/game/Level.h"
#include "mb/game/MaterialLibrary.h"

#include <cstddef>
#include <limits>
#include <span>
#include <vector>

namespace mb {

struct PhysicsPlan;

// How a cloth is simulated (docs/GAMEPLAY.md section 20, "The numbers"). The data gives the grid, the sail's saved shape, the wind, the
// size, weight and friction of a particle and what holds the corners; the rest is chosen [I] to look as the footage's sails do.
struct ClothSettings {
    // The share of the data's wind (10 units a second squared, square to the sail) that a sail takes. The whole of it swings a sail that
    // hangs by its top corners out 45 degrees and bellies it out by a quarter of its width; the footage's sails hang nearly upright and
    // belly out a little (original-level1-pirates.jpg). At 0.4 they belly out by a fifth of their width [I].
    float windShare = 0.4f;
    // The springs of a sail are this much longer than the distance between their particles where the level saved them, and grow to it over
    // slackSeconds from none: pinned at its corners where it was saved, a sail cut with slack bellies out the way the wind goes, and one that
    // has to take its slack all at once snaps into the bow. A sail cut to the saved distances lies flat as a board whatever the wind [I].
    float slack = 0.05f;
    float slackSeconds = 1.5f;
    // XPBD compliances (ClothDesc): along the sail's grid nearly none (cloth does not stretch), across its cells a little more, and across
    // its folds enough to let it drape [I].
    float stretchCompliance = 1e-4f;
    float shearCompliance = 1e-3f;
    float bendCompliance = 0.05f;
    float damping = 0.8f;  // of the particles' speed, per second: a sail settles within a few seconds [I]
    int iterations = 5;    // solver passes a step [I]
};

// A corner of a sail held to a mast, a boat or a plot: the joint of the level that tied the particle's sphere to it. The
// sail follows the holder, and lets go when the holder is destroyed or a hit breaks the joint (PhysicsScene).
struct ClothPin {
    int particle = 0;
    ActorIndex holder = kNoActor;  // the actor it is tied to; kNoActor for the world
    Vec3 local;                    // where it is tied, in the holder's frame as the level saved it (the world's: world space)
    float breakForce = std::numeric_limits<float>::infinity();  // the joint's maxForce
};

// One cloth object of a level as the physics has it. Particle k is at column k % width of row k / width; row 0 is the sail's lowest.
struct ClothPlan {
    ActorIndex lead = kNoActor;       // the cloth object's own actor: what the rules and the contacts know the sail by
    std::vector<ActorIndex> actors;   // by particle: the lead, then its segments in order
    int width = 0, height = 0;
    std::vector<Vec3> rest;           // by particle: where the level saved it, world space
    Vec3 wind{};                      // the data's wind, world space
    float particleRadius = 0;         // of a segment's sphere
    float particleMass = 0;           // of a segment's body
    float friction = 0.5f, restitution = 0;  // of the cloth's material (the PhysX one)
    std::vector<ClothPin> pins;
};

// Finds the cloths of the level and fills in `plan.cloths`: their grids, rest shapes, wind and pins, from the actors and joints already
// in the plan. The lead actor's body gets one sphere shape for each particle, so that whatever measures an actor measures the sail.
// A cloth whose segments do not make up its grid, or that has no sphere to give a particle its place, is counted in
// `plan.unsupportedCloths` and left to the old placeholder (its actors stay as they are).
void planCloths(std::span<const ActorRef> actors, const MaterialLibrary& materials, PhysicsPlan& plan);

}  // namespace mb
