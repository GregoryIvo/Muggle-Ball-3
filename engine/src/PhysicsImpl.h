#pragma once

// Internals of PhysicsWorld, shared by Physics.cpp (bodies, stepping, queries), PhysicsShapes.cpp
// (descriptions to Jolt shapes and body settings) and PhysicsJoints.cpp. Nothing here is public.

#include "mb/engine/Log.h"
#include "mb/engine/Physics.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystem.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/GroupFilter.h>
#include <Jolt/Physics/Collision/PhysicsMaterial.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/SoftBody/SoftBodyContactListener.h>
#include <Jolt/Physics/SoftBody/SoftBodyManifold.h>
#include <Jolt/Physics/SoftBody/SoftBodySharedSettings.h>

#include <array>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mb {

// Events nobody collected; a game that never calls takeContacts() must not eat the memory.
constexpr std::size_t kMaxQueuedContacts = 1u << 16;

// Allocator, type registry and factory are process-wide in Jolt; every world holds one of these
// and the last one to go tears them down.
class JoltRuntime {
public:
    JoltRuntime();
    ~JoltRuntime();
    JoltRuntime(const JoltRuntime&) = delete;
    JoltRuntime& operator=(const JoltRuntime&) = delete;
};

// A body is either moving or not; static bodies live in the cheaper broad phase tree. A cloth is a layer of its own, in the moving tree:
// it sees every body, and no body sees it (the filter below is deliberately not symmetric). Jolt lets a body that moves continuously
// cast against the faces of a soft body, and a face with a particle that is held (a pinned one has no mass) takes the whole momentum of
// what hits it into the others: the particles go to 10^7 in one step. So the cloth is the one that looks for bodies, particle by
// particle, and nothing looks for it; a particle is what the original's sails collided with (spheres of 0.5 on a grid).
namespace layers {
constexpr JPH::ObjectLayer nonMoving = 0;
constexpr JPH::ObjectLayer moving = 1;
constexpr JPH::ObjectLayer cloth = 2;
constexpr JPH::uint broadPhaseCount = 2;  // the cloth is in the moving one
}  // namespace layers

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return layers::broadPhaseCount; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(layer == layers::nonMoving ? layers::nonMoving : layers::moving));
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return static_cast<JPH::BroadPhaseLayer::Type>(layer) == layers::moving ? "moving" : "non-moving";
    }
#endif
};

class ObjectVsBroadPhaseLayers final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer object, JPH::BroadPhaseLayer broadPhase) const override {
        return object != layers::nonMoving || static_cast<JPH::BroadPhaseLayer::Type>(broadPhase) == layers::moving;
    }
};

class ObjectLayerPairs final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        if (a == layers::cloth) {
            return b != layers::cloth;
        }
        return b != layers::cloth && (a == layers::moving || b == layers::moving);
    }
};

// BodyDesc::group and collidesWith: each body's group id is its kind and its sub group id the kinds it touches.
class KindFilter final : public JPH::GroupFilter {
public:
    bool CanCollide(const JPH::CollisionGroup& a, const JPH::CollisionGroup& b) const override {
        return (a.GetGroupID() & b.GetSubGroupID()) != 0 && (b.GetGroupID() & a.GetSubGroupID()) != 0;
    }
};

// The carrier of mb::PhysicsMaterial inside Jolt shapes; the contact combiners read it back.
class Material final : public JPH::PhysicsMaterial {
public:
    explicit Material(const mb::PhysicsMaterial& values) : values(values) {}
    mb::PhysicsMaterial values;
};

// Our rigid pose as Jolt wants it. Jolt rotates column vectors, we rotate row vectors, so its
// rotation matrix is the transpose of our upper-left 3x3 and the numbers (axes, handedness) are
// otherwise passed through untouched.
struct JoltPose {
    JPH::RVec3 position = JPH::RVec3::sZero();
    JPH::Quat rotation = JPH::Quat::sIdentity();
};

inline JPH::Vec3 toJolt(Vec3 v) { return JPH::Vec3(v.x, v.y, v.z); }
inline Vec3 fromJolt(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }

bool isFinite(Vec3 v);
bool toJolt(const Mat4& pose, JoltPose& out);  // false if not finite or not a rotation plus translation
Mat4 fromJolt(JPH::RVec3Arg position, JPH::QuatArg rotation);
JPH::Mat44 rotationToJolt(const Mat4& pose);  // just the rotation of a rigid Mat4

struct BodyRecord {
    JPH::BodyID id;
    std::uint64_t userData = 0;
    std::vector<JointId> joints;
    float sleepThreshold = 0;  // what this body put into Impl::sleepThresholds; 0 = nothing
    bool locked = false;       // see fullyLocked(): static for good
};

struct JointRecord {
    JPH::Ref<JPH::TwoBodyConstraint> constraint;
    BodyId bodyA = 0, bodyB = 0;
    float breakForce = 0, breakTorque = 0;
    bool breakable = false;
    bool noCollide = false;  // registered in Impl::noCollide
};

struct KinematicTarget {
    JPH::RVec3 position;
    JPH::Quat rotation;
};

// A cloth's own state beside its Jolt soft body (addCloth()).
struct ClothRecord {
    struct Pin {
        Vec3 position;
        Vec3 velocity;  // what took the particle to this place, for the moment it is let go
    };
    int width = 0, height = 0;
    std::vector<std::array<std::uint32_t, 3>> faces;
    std::vector<float> invMass;  // by particle, as the cloth was made: a pinned particle has none
    // The springs' rest lengths: what they are to be when the slack is all there, and how far they have grown towards it.
    JPH::Ref<JPH::SoftBodySharedSettings> shared;
    std::vector<float> restLength;  // by spring of `shared`
    float slack = 0, slackSeconds = 0, age = 0;
    bool slackGrown = false;  // all of it is there
    std::map<int, Pin> pins;     // by particle
    Vec3 wind{};
    std::unordered_set<BodyId> ignored;  // bodies it goes through
    std::set<BodyId> touching;           // what it touched at the end of the last step
    std::vector<JPH::Vec3> sound;        // the particles at the end of the last step that left them all within bounds
    int resets = 0;                      // how many times the solver sent them off, see PhysicsWorld::Impl::guardCloths()
    bool frozen = false;                 // and held where it is for good
};

// What a cloth's particles met in a step, as Jolt's worker threads saw it (see PhysicsWorld::Impl::OnSoftBodyContactAdded).
struct ClothTouch {
    BodyId cloth = 0, other = 0;
    Vec3 point, normal;
    float speed = 0;
};

// True when every translation and rotation axis is locked: such a body is created static.
bool fullyLocked(const BodyDesc& desc);

// Builds the Jolt description of a body (shape, mass, motion). On failure returns false and says why.
bool makeBodySettings(const BodyDesc& desc, JPH::BodyCreationSettings& out, std::string& error);

struct PhysicsWorld::Impl final : JPH::ContactListener, JPH::BodyActivationListener, JPH::SoftBodyContactListener {
    Impl(Vec3 gravity, const PhysicsSettings& requested);
    ~Impl() override;

    // Jolt keeps references to these, so they are declared before and outlive the system.
    JoltRuntime runtime;
    PhysicsSettings settings;
    BroadPhaseLayers broadPhaseLayers;
    ObjectVsBroadPhaseLayers objectVsBroadPhase;
    ObjectLayerPairs objectPairs;
    std::unique_ptr<JPH::TempAllocator> tempAllocator;
    std::unique_ptr<JPH::JobSystem> jobSystem;
    JPH::PhysicsSystem system;
    JPH::BodyInterface& bodies;  // the unlocked interface: everything here runs on one thread, outside step()
    JPH::Ref<KindFilter> kinds = new KindFilter;  // on every body, see BodyDesc::group

    std::unordered_map<BodyId, BodyRecord> records;
    std::unordered_map<JointId, JointRecord> joints;
    BodyId nextBody = 1;
    JointId nextJoint = 1;

    double accumulator = 0;
    std::size_t addedSinceOptimize = 0;
    std::uint64_t droppedSteps = 0;  // whole fixed steps thrown away so far; see PhysicsWorld::step

    std::unordered_map<BodyId, KinematicTarget> kinematicTargets;
    std::vector<BodyId> kinematicMoving;  // moved by the last fixed step; stopped unless moved again

    std::multiset<float> sleepThresholds;  // see updateSleepThreshold

    // Pairs of bodies whose joints ask them not to collide, with how many joints say so.
    std::unordered_map<std::uint64_t, int> noCollide;

    // Filled by Jolt's worker threads while stepping.
    std::mutex queueMutex;
    std::vector<ContactEvent> contacts;
    std::vector<BodyId> activated;  // bodies that woke up and have joints; see flushActivations
    bool contactsDropped = false;

    std::vector<JointId> brokenJoints;

    std::unordered_map<BodyId, ClothRecord> cloths;
    std::vector<ClothTouch> clothTouches;  // filled by worker threads, under queueMutex

    mutable std::unordered_set<BodyId> warnedBodies;
    mutable std::unordered_set<JointId> warnedJoints;
    bool warnedUpdateError = false;

    // Id lookups; an unknown id is logged (once per id) and gives nullptr.
    const BodyRecord* find(BodyId id, const char* what) const;
    BodyRecord* find(BodyId id, const char* what) {
        return const_cast<BodyRecord*>(static_cast<const Impl*>(this)->find(id, what));
    }
    JPH::Body* joltBody(const BodyRecord& record) const;

    void updateSleepThreshold();
    void fixedStep();
    void applyKinematicTargets();
    void checkBreaks();
    void flushActivations();
    void destroyJoint(JointId id);  // takes the constraint out of the system and every list
    void addNoCollide(BodyId a, BodyId b);
    void removeNoCollide(BodyId a, BodyId b);
    void invalidateContacts(BodyId a, BodyId b);
    bool movingBodyNear(const JPH::Body& cloth, const ClothRecord& record) const;
    void guardCloths();          // after it: a cloth that has gone to pieces is put back
    void driveCloths();          // before a fixed step: the pins and the wind
    void reportClothTouches();   // after it: the touches that began become contact events

    // JPH::ContactListener, called from worker threads.
    JPH::ValidateResult OnContactValidate(const JPH::Body& a, const JPH::Body& b, JPH::RVec3Arg baseOffset,
                                          const JPH::CollideShapeResult& result) override;
    void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold,
                        JPH::ContactSettings& settings) override;

    // JPH::SoftBodyContactListener, called from worker threads.
    JPH::SoftBodyValidateResult OnSoftBodyContactValidate(const JPH::Body& cloth, const JPH::Body& other,
                                                          JPH::SoftBodyContactSettings& settings) override;
    void OnSoftBodyContactAdded(const JPH::Body& cloth, const JPH::SoftBodyManifold& manifold) override;

    // JPH::BodyActivationListener
    void OnBodyActivated(const JPH::BodyID& id, JPH::uint64 userData) override;
    void OnBodyDeactivated(const JPH::BodyID&, JPH::uint64) override {}
};

}  // namespace mb
