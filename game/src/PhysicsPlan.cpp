// Level -> bodies and joints, see planPhysics() in PhysicsScene.h.

#include "mb/game/PhysicsScene.h"

#include "mb/engine/Log.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <unordered_map>

namespace mb {

namespace {

constexpr std::uint32_t kAllFrozen = Body::kFrozenPosX | Body::kFrozenPosY | Body::kFrozenPosZ | Body::kFrozenRotX |
                                     Body::kFrozenRotY | Body::kFrozenRotZ;

// NxJointFlag-style flags of the level's revolute and spherical joints.
constexpr std::uint32_t kRevoluteLimit = 1, kRevoluteMotor = 2, kRevoluteSpring = 4;
constexpr std::uint32_t kSphericalTwistLimit = 1, kSphericalSwingLimit = 2;

PhysicsMaterial materialOf(const MaterialLibrary& materials, const GameObject* owner) {
    const GameMaterial* game = owner && !owner->material.empty() ? materials.find(owner->material) : nullptr;
    return game ? physicsMaterial(*game) : PhysicsMaterial{};
}

std::optional<PhysicsShape> physicsShape(const Shape& shape, const PhysicsMaterial& material) {
    PhysicsShape out;
    out.localPose = rigidTransform(shape.localPose);
    out.material = material;
    switch (shape.type) {
        case ShapeType::Box:
            out.type = PhysicsShapeType::Box;
            out.halfExtents = shape.halfExtents;
            return out;
        case ShapeType::Sphere:
            out.type = PhysicsShapeType::Sphere;
            out.radius = shape.radius;
            return out;
        case ShapeType::Capsule:
            out.type = PhysicsShapeType::Capsule;
            out.radius = shape.radius;
            out.halfHeight = shape.height * 0.5f;
            return out;
        case ShapeType::Convex:
            if (shape.hullPoints.empty()) {
                return std::nullopt;  // a level file from before version 3 has no hull points
            }
            out.type = PhysicsShapeType::ConvexHull;
            out.points = shape.hullPoints;
            return out;
        case ShapeType::Plane:
        case ShapeType::Mesh:
            return std::nullopt;
    }
    return std::nullopt;
}

MotionType motionOf(const Actor& actor, const GameObject* owner) {
    if (!actor.body || (actor.body->flags & kAllFrozen) == kAllFrozen) {
        return MotionType::Static;
    }
    if (actor.body->kinematic() || (owner && owner->type == ObjectType::Cloth)) {
        return MotionType::Kinematic;
    }
    return MotionType::Dynamic;
}

void planBody(const ActorRef& ref, const MaterialLibrary& materials, const PhysicsOptions& options,
              PhysicsPlan& plan) {
    const Actor& actor = *ref.actor;
    ActorPlan out;
    out.type = ref.owner ? ref.owner->type : ObjectType::Unknown;
    out.pose = rigidTransform(actor.pose);
    out.motion = motionOf(actor, ref.owner);

    const PhysicsMaterial material = materialOf(materials, ref.owner);
    BodyDesc& body = out.body;
    for (const Shape& shape : actor.shapes) {
        const bool trigger = (shape.shapeFlags & kTriggerShapeFlags) != 0;
        if (shape.group == kBatZoneGroup || trigger) {
            if (shape.type == ShapeType::Box) {
                plan.zones.push_back({ref.index, shape.group, trigger, rigidTransform(shape.localPose),
                                      shape.halfExtents});
            }
        }
        if (!collides(shape)) {
            continue;
        }
        if (std::optional<PhysicsShape> converted = physicsShape(shape, material)) {
            body.shapes.push_back(std::move(*converted));
        } else {
            ++plan.unsupportedShapes;
        }
    }
    out.simulated = !body.shapes.empty();
    if (!out.simulated) {
        plan.actors.push_back(std::move(out));
        return;
    }

    body.pose = out.pose;
    body.motion = out.motion;
    body.density = actor.density;
    body.userData = ref.index;
    if (const std::optional<Body>& saved = actor.body) {
        body.mass = saved->mass;
        body.inertia = saved->massSpaceInertia;
        body.massLocalPose = rigidTransform(saved->massLocalPose);
        body.linearDamping = saved->linearDamping;
        body.angularDamping = saved->angularDamping;
        body.maxAngularVelocity = saved->maxAngularVelocity;
        body.gravity = (saved->flags & Body::kDisableGravity) == 0;
        body.lockTranslation[0] = (saved->flags & Body::kFrozenPosX) != 0;
        body.lockTranslation[1] = (saved->flags & Body::kFrozenPosY) != 0;
        body.lockTranslation[2] = (saved->flags & Body::kFrozenPosZ) != 0;
        body.lockRotation[0] = (saved->flags & Body::kFrozenRotX) != 0;
        body.lockRotation[1] = (saved->flags & Body::kFrozenRotY) != 0;
        body.lockRotation[2] = (saved->flags & Body::kFrozenRotZ) != 0;
        body.sleepLinearVelocity = saved->sleepLinearVelocity;
        body.sleepAngularVelocity = saved->sleepAngularVelocity;
        if (out.motion != MotionType::Static) {
            switch (options.start) {
                case StartState::Asleep:
                    body.startAsleep = true;
                    break;
                case StartState::SavedVelocities:
                    body.linearVelocity = saved->linearVelocity;
                    body.angularVelocity = saved->angularVelocity;
                    break;
                case StartState::Still:
                    break;
            }
        }
    }
    plan.actors.push_back(std::move(out));
}

// Level object id (and cloth segment id) -> actor index, see "Joint actor references" in LEVEL_FORMAT.md.
std::unordered_map<std::uint32_t, ActorIndex> actorsById(std::span<const ActorRef> actors) {
    std::unordered_map<std::uint32_t, ActorIndex> byId;
    // try_emplace keeps the first one: an object id wins over a segment id, an earlier object over a later one.
    for (const ActorRef& ref : actors) {
        if (ref.actor->object) {
            byId.try_emplace(ref.actor->object->id, ref.index);
        }
    }
    for (const ActorRef& ref : actors) {
        if (ref.actor->object) {
            const std::size_t segments = ref.actor->object->clothSegments.size();
            for (std::size_t k = 0; k < segments; ++k) {
                byId.try_emplace(ref.actor->object->id + 1 + static_cast<std::uint32_t>(k),
                                 ref.index + 1 + static_cast<ActorIndex>(k));
            }
        }
    }
    return byId;
}

// Makes the second body's frame the first one's, as the saved poses have them.
void rebuildSecondFrame(const JointPlan& joint, const PhysicsPlan& plan, JointDesc& desc) {
    const Mat4 first = joint.actorA == kNoActor ? Mat4::identity() : plan.actors[joint.actorA].pose;
    const Mat4 toSecond = inverse(plan.actors[joint.actorB].pose);
    const Vec3 anchor = transformPoint(desc.localAnchor[0], first);
    const Vec3 axis = transformVector(normalize(desc.localAxis[0]), first);
    const Vec3 normal = transformVector(normalize(desc.localNormal[0]), first);
    desc.localAnchor[1] = transformPoint(anchor, toSecond);
    desc.localAxis[1] = normalize(transformVector(axis, toSecond));
    desc.localNormal[1] = normalize(transformVector(normal, toSecond));
}

void planJoints(const Level& level, std::span<const ActorRef> actors, const PhysicsOptions& options,
                PhysicsPlan& plan) {
    const std::unordered_map<std::uint32_t, ActorIndex> byId = actorsById(actors);
    const auto resolve = [&](std::uint32_t reference, ActorIndex& out) {
        if (reference == 0) {
            out = kNoActor;
            return true;
        }
        const auto it = byId.find(reference);
        out = it == byId.end() ? kNoActor : it->second;
        return it != byId.end();
    };

    for (const Joint& saved : level.joints) {
        JointPlan joint;
        if (!resolve(saved.actorA, joint.actorA) || !resolve(saved.actorB, joint.actorB) ||
            (joint.actorA == kNoActor && joint.actorB == kNoActor)) {
            ++plan.unresolvedJoints;
            continue;
        }
        if (saved.type != NxJointType::Fixed && saved.type != NxJointType::Revolute &&
            saved.type != NxJointType::Spherical) {
            ++plan.unsupportedJoints;
            continue;
        }
        const auto hasBody = [&](ActorIndex actor) { return actor == kNoActor || plan.actors[actor].simulated; };
        if (!hasBody(joint.actorA) || !hasBody(joint.actorB)) {
            ++plan.jointsWithoutBody;
            continue;
        }

        JointDesc& desc = joint.desc;
        for (int end = 0; end < 2; ++end) {
            desc.localAnchor[end] = saved.localAnchor[end];
            desc.localAxis[end] = saved.localAxis[end];
            desc.localNormal[end] = saved.localNormal[end];
        }
        desc.collideConnected = false;  // PhysX's default; the saved flag word carries no reliable bit
        constexpr float kUnbreakable = std::numeric_limits<float>::max();
        constexpr float kInfinity = std::numeric_limits<float>::infinity();
        desc.breakForce = saved.maxForce >= kUnbreakable ? kInfinity : saved.maxForce;
        desc.breakTorque = saved.maxTorque >= kUnbreakable ? kInfinity : saved.maxTorque;

        const auto limit = [](const JointLimitDesc& l) { return JointLimit{l.value, l.restitution}; };
        switch (saved.type) {
            case NxJointType::Revolute:
                desc.type = JointType::Hinge;
                desc.limitEnabled = (saved.typeFlags & kRevoluteLimit) != 0;
                desc.limitLow = limit(saved.limitLow);
                desc.limitHigh = limit(saved.limitHigh);
                desc.motorEnabled = (saved.typeFlags & kRevoluteMotor) != 0;
                desc.motorVelocity = saved.motorVelTarget;
                desc.motorMaxForce = saved.motorMaxForce;
                desc.springEnabled = (saved.typeFlags & kRevoluteSpring) != 0;
                desc.spring = saved.spring.spring;
                desc.damper = saved.spring.damper;
                desc.springTarget = saved.spring.targetValue;
                break;
            case NxJointType::Spherical:
                desc.type = JointType::Ball;
                desc.limitEnabled = (saved.typeFlags & kSphericalTwistLimit) != 0;
                desc.limitLow = limit(saved.limitLow);
                desc.limitHigh = limit(saved.limitHigh);
                desc.swingLimitEnabled = (saved.typeFlags & kSphericalSwingLimit) != 0;
                desc.swingLimit = limit(saved.swingLimit);
                break;
            default:
                desc.type = JointType::Fixed;
                break;
        }
        const auto isCloth = [&](ActorIndex actor) {
            return actor != kNoActor && plan.actors[actor].type == ObjectType::Cloth;
        };
        joint.toCloth = isCloth(joint.actorA) || isCloth(joint.actorB);
        if (options.rebuildJointFrames && joint.actorB != kNoActor) {
            rebuildSecondFrame(joint, plan, desc);
        }
        plan.joints.push_back(std::move(joint));
    }
}

}  // namespace

bool collides(const Shape& shape) {
    return shape.group != kMarkerGroup && shape.group != kBatZoneGroup && (shape.shapeFlags & kTriggerShapeFlags) == 0;
}

PhysicsMaterial physicsMaterial(const GameMaterial& material) {
    const auto mode = [](int value) { return static_cast<CombineMode>(std::clamp(value, 0, 3)); };
    PhysicsMaterial out;
    out.staticFriction = material.staticFriction;
    out.dynamicFriction = material.dynamicFriction;
    out.restitution = material.restitution;
    out.frictionCombine = mode(material.frictionCombineMode);
    out.restitutionCombine = mode(material.restitutionCombineMode);
    return out;
}

PhysicsPlan planPhysics(const Level& level, const MaterialLibrary& materials, const PhysicsOptions& options) {
    PhysicsPlan plan;
    plan.gravity = level.gravity;
    const std::vector<ActorRef> actors = flattenActors(level);
    plan.actors.reserve(actors.size());
    for (const ActorRef& ref : actors) {
        planBody(ref, materials, options, plan);
    }
    planJoints(level, actors, options, plan);
    return plan;
}

}  // namespace mb
