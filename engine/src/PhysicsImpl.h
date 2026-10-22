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

#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mb {

// Allocator, type registry and factory are process-wide in Jolt; every world holds one of these
// and the last one to go tears them down.
class JoltRuntime {
public:
    JoltRuntime();
    ~JoltRuntime();
    JoltRuntime(const JoltRuntime&) = delete;
    JoltRuntime& operator=(const JoltRuntime&) = delete;
};

// A body is either moving or not; static bodies live in the cheaper broad phase tree.
namespace layers {
constexpr JPH::ObjectLayer nonMoving = 0;
constexpr JPH::ObjectLayer moving = 1;
constexpr JPH::uint count = 2;
}  // namespace layers

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return layers::count; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(layer));
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
        return object == layers::moving || static_cast<JPH::BroadPhaseLayer::Type>(broadPhase) == layers::moving;
    }
};

class ObjectLayerPairs final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a == layers::moving || b == layers::moving;
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

// True when every translation and rotation axis is locked: such a body is created static.
bool fullyLocked(const BodyDesc& desc);

// Builds the Jolt description of a body (shape, mass, motion). On failure returns false and says why.
bool makeBodySettings(const BodyDesc& desc, JPH::BodyCreationSettings& out, std::string& error);

struct PhysicsWorld::Impl final : JPH::ContactListener, JPH::BodyActivationListener {
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

    // JPH::ContactListener, called from worker threads.
    JPH::ValidateResult OnContactValidate(const JPH::Body& a, const JPH::Body& b, JPH::RVec3Arg baseOffset,
                                          const JPH::CollideShapeResult& result) override;
    void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold,
                        JPH::ContactSettings& settings) override;

    // JPH::BodyActivationListener
    void OnBodyActivated(const JPH::BodyID& id, JPH::uint64 userData) override;
    void OnBodyDeactivated(const JPH::BodyID&, JPH::uint64) override {}
};

}  // namespace mb
