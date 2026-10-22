#include "mb/game/Cloth.h"

#include "mb/game/PhysicsScene.h"

#include <algorithm>
#include <limits>

namespace mb {

namespace {

// What a sail does not touch: the parts of the ship it hangs from, found through the joints that join them. A ship has a few
// dozen; one that grew past this would be the whole level joined together (docs/GAMEPLAY.md section 20.5 [I]).
constexpr std::size_t kMostShipParts = 256;

}  // namespace

ClothSet::ClothSet(const PhysicsPlan& plan, PhysicsWorld& world, std::vector<BodyId>& bodies, const ClothSettings& settings)
    : plan_(plan), world_(world), clothOf_(plan.actors.size(), -1), pinsOfHolder_(plan.actors.size()) {
    // The ship of each sail: what its holders are joined to, other than the world and what never moves.
    std::vector<std::vector<ActorIndex>> joinedTo(plan.actors.size());
    for (const JointPlan& joint : plan.joints) {
        if (!joint.toCloth && joint.actorA != kNoActor && joint.actorB != kNoActor) {
            joinedTo[joint.actorA].push_back(joint.actorB);
            joinedTo[joint.actorB].push_back(joint.actorA);
        }
    }

    state_.resize(plan.cloths.size());
    for (std::size_t i = 0; i < plan.cloths.size(); ++i) {
        const ClothPlan& cloth = plan.cloths[i];
        for (const ActorIndex actor : cloth.actors) {
            clothOf_[actor] = static_cast<int>(i);
        }
        ClothDesc desc;
        desc.width = cloth.width;
        desc.height = cloth.height;
        desc.positions = cloth.rest;
        desc.particleMass = cloth.particleMass;
        desc.particleRadius = cloth.particleRadius;
        desc.slack = settings.slack;
        desc.slackSeconds = settings.slackSeconds;
        desc.stretchCompliance = settings.stretchCompliance;
        desc.shearCompliance = settings.shearCompliance;
        desc.bendCompliance = settings.bendCompliance;
        desc.damping = settings.damping;
        desc.friction = cloth.friction;
        desc.restitution = cloth.restitution;
        desc.iterations = settings.iterations;
        desc.userData = cloth.lead;
        State& state = state_[i];
        state.body = world.addCloth(desc);
        if (state.body == 0) {
            continue;
        }
        bodies[cloth.lead] = state.body;
        world.setClothWind(state.body, cloth.wind * settings.windShare);

        state.held.assign(cloth.pins.size(), true);
        state.holding = cloth.pins.size();
        for (std::size_t p = 0; p < cloth.pins.size(); ++p) {
            const ClothPin& pin = cloth.pins[p];
            const Vec3 at = pin.holder == kNoActor ? pin.local : transformPoint(pin.local, plan.actors[pin.holder].pose);
            world.pinClothParticle(state.body, pin.particle, at);
            if (pin.holder != kNoActor) {
                pinsOfHolder_[pin.holder].push_back({i, p});
            }
        }

        std::vector<ActorIndex> stack;
        std::vector<bool> seen(plan.actors.size(), false);
        std::size_t parts = 0;
        for (const ClothPin& pin : cloth.pins) {
            if (pin.holder != kNoActor) {
                stack.push_back(pin.holder);
            }
        }
        while (!stack.empty() && parts < kMostShipParts) {
            const ActorIndex actor = stack.back();
            stack.pop_back();
            if (seen[actor]) {
                continue;
            }
            seen[actor] = true;
            if (bodies[actor] == 0 || plan.actors[actor].motion == MotionType::Static) {
                continue;
            }
            ++parts;
            world.clothIgnores(state.body, bodies[actor]);
            stack.insert(stack.end(), joinedTo[actor].begin(), joinedTo[actor].end());
        }
    }
}

const ClothPlan& ClothSet::plan(std::size_t cloth) const {
    return plan_.cloths[cloth];
}

std::vector<Vec3> ClothSet::positions(std::size_t cloth) const {
    return alive(cloth) ? world_.clothPositions(state_[cloth].body) : std::vector<Vec3>{};
}

Bounds ClothSet::bounds(std::size_t cloth) const {
    const std::vector<Vec3> now = positions(cloth);
    Bounds box;
    for (const Vec3 p : now.empty() ? plan_.cloths[cloth].rest : now) {
        box.add(p, plan_.cloths[cloth].particleRadius);
    }
    return box;
}

Vec3 ClothSet::centre(std::size_t cloth) const {
    const std::vector<Vec3> now = positions(cloth);
    const std::vector<Vec3>& points = now.empty() ? plan_.cloths[cloth].rest : now;
    Vec3 sum{};
    for (const Vec3 p : points) {
        sum = sum + p;
    }
    return sum / static_cast<float>(points.size());
}

float ClothSet::distanceTo(std::size_t cloth, Vec3 point) const {
    float nearest = std::numeric_limits<float>::infinity();
    for (const Vec3 p : positions(cloth)) {
        nearest = std::min(nearest, length(p - point) - plan_.cloths[cloth].particleRadius);
    }
    return std::max(nearest, 0.0f);
}

void ClothSet::carryPins(const PhysicsScene& scene) {
    for (std::size_t i = 0; i < state_.size(); ++i) {
        const State& state = state_[i];
        if (state.body == 0 || state.holding == 0) {
            continue;
        }
        const std::vector<ClothPin>& pins = plan_.cloths[i].pins;
        for (std::size_t p = 0; p < pins.size(); ++p) {
            if (state.held[p]) {
                const ClothPin& pin = pins[p];
                const Vec3 at = pin.holder == kNoActor ? pin.local : transformPoint(pin.local, scene.actorPose(pin.holder));
                world_.pinClothParticle(state.body, pin.particle, at);
            }
        }
    }
}

void ClothSet::release(std::size_t cloth, std::size_t pin) {
    State& state = state_[cloth];
    if (state.body == 0 || !state.held[pin]) {
        return;
    }
    state.held[pin] = false;
    --state.holding;
    world_.releaseClothParticle(state.body, plan_.cloths[cloth].pins[pin].particle);
    if (state.holding == 0) {
        world_.clothIgnoresNothing(state.body);  // nothing hangs it from the ship now: it can lie on what is left of it
    }
}

void ClothSet::holderGone(ActorIndex holder) {
    for (const PinRef& ref : pinsOfHolder_[holder]) {
        release(ref.cloth, ref.pin);
    }
}

void ClothSet::holderHit(ActorIndex holder, float force) {
    for (const PinRef& ref : pinsOfHolder_[holder]) {
        if (force > plan_.cloths[ref.cloth].pins[ref.pin].breakForce) {
            release(ref.cloth, ref.pin);
        }
    }
}

void ClothSet::remove(std::size_t cloth) {
    State& state = state_[cloth];
    if (state.body != 0) {
        world_.removeBody(state.body);
        state.body = 0;
        state.holding = 0;
    }
}

}  // namespace mb
