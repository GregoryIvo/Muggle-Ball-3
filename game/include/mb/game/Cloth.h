#pragma once

#include "mb/engine/Physics.h"
#include "mb/game/ClothPlan.h"

#include <cstddef>
#include <vector>

namespace mb {

class PhysicsScene;
struct Bounds;
struct PhysicsPlan;

// The sails of a PhysicsScene (docs/GAMEPLAY.md section 20): one cloth body in the physics world for each of the plan's cloths, held to the
// ships by the pins of the level's joints, blown by the data's wind, and touched by the ball and whatever flies. The scene calls it at
// the few places a sail meets the rest of the world; nothing else does.
class ClothSet {
public:
    // Makes the cloth bodies, which stand in for the lead actors' bodies (`bodies`, by actor: set at each lead, left 0 at the segments),
    // and ties their pins where the holders stand now.
    ClothSet(const PhysicsPlan& plan, PhysicsWorld& world, std::vector<BodyId>& bodies, const ClothSettings& settings);

    std::size_t size() const { return state_.size(); }
    const ClothPlan& plan(std::size_t cloth) const;
    // Which cloth an actor is a part of (its lead or one of its segments), or -1.
    int clothOf(ActorIndex actor) const { return actor < clothOf_.size() ? clothOf_[actor] : -1; }
    // Whether the cloth is still in the world: a destroyed sail is gone, see remove().
    bool alive(std::size_t cloth) const { return state_[cloth].body != 0; }
    BodyId body(std::size_t cloth) const { return state_[cloth].body; }
    // How many of its pins still hold.
    std::size_t pinsHolding(std::size_t cloth) const { return state_[cloth].holding; }

    // Where the particles are, by particle; empty for a cloth that is gone.
    std::vector<Vec3> positions(std::size_t cloth) const;
    // The box round the particles and their radius, and the middle of the particles; for a cloth that is gone, where it was saved.
    Bounds bounds(std::size_t cloth) const;
    Vec3 centre(std::size_t cloth) const;

    // Before each fixed step: takes every held particle to where its holder has it. A holder that has no body stays where it was saved.
    void carryPins(const PhysicsScene& scene);
    // The holder is gone (destroyed): the particles it held fall free.
    void holderGone(ActorIndex holder);
    // A hit of `force` on the holder (the rule that breaks joints, PhysicsScene::breakByHit): the pins whose joint is weaker let go.
    void holderHit(ActorIndex holder, float force);
    // The sail was destroyed: it leaves the world.
    void remove(std::size_t cloth);

private:
    struct State {
        BodyId body = 0;
        std::vector<bool> held;  // by pin of the plan: still holds
        std::size_t holding = 0;
    };
    struct PinRef {
        std::size_t cloth = 0, pin = 0;
    };

    void release(std::size_t cloth, std::size_t pin);

    const PhysicsPlan& plan_;
    PhysicsWorld& world_;
    std::vector<State> state_;
    std::vector<int> clothOf_;                        // by actor
    std::vector<std::vector<PinRef>> pinsOfHolder_;   // by actor
};

}  // namespace mb
