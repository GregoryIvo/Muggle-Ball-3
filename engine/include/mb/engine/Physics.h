#pragma once

#include "mb/engine/Math.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace mb {

// Rigid-body physics, standing in for the original's Ageia PhysX 2.x. Jolt does the
// work underneath; nothing in this header depends on it. Same space as everything
// else (Math.h): left-handed, +y up, row vectors. Poses are rigid (no scale).

using BodyId = std::uint32_t;   // 0 = none, or "the world" where a joint takes one
using JointId = std::uint32_t;  // 0 = none

// PhysX's NxCombineMode, same numbering. When two touching materials ask for different
// modes, the higher-numbered one wins, as in PhysX.
enum class CombineMode : std::uint8_t { Average = 0, Min = 1, Multiply = 2, Max = 3 };

struct PhysicsMaterial {
    float staticFriction = 0.5f;
    float dynamicFriction = 0.5f;
    float restitution = 0.0f;
    CombineMode frictionCombine = CombineMode::Average;
    CombineMode restitutionCombine = CombineMode::Average;
};

enum class PhysicsShapeType { Box, Sphere, Capsule, ConvexHull };

struct PhysicsShape {
    PhysicsShapeType type = PhysicsShapeType::Box;
    Mat4 localPose;            // rigid, relative to the body's frame
    Vec3 halfExtents{};        // Box
    float radius = 0;          // Sphere, Capsule
    // Capsule: half the cylinder length, along the shape's local +y. Under 5 cm Jolt's capsule is unreliable (it
    // sinks into a floor), so the shape is made a sphere of the same radius.
    float halfHeight = 0;
    std::vector<Vec3> points;  // ConvexHull, in the shape's local space
    PhysicsMaterial material;
};

enum class MotionType { Static, Kinematic, Dynamic };

struct BodyDesc {
    Mat4 pose;  // rigid world transform of the body's frame
    MotionType motion = MotionType::Dynamic;
    std::vector<PhysicsShape> shapes;  // at least one

    // Mass properties, as PhysX stores them. mass > 0 uses mass, inertia and massLocalPose
    // as given (a zero inertia is computed from the shapes for that mass); mass == 0
    // computes everything from the shapes and density.
    float mass = 0;
    float density = 1;
    Vec3 inertia{};       // diagonal, in the mass frame
    Mat4 massLocalPose;   // centre-of-mass frame relative to the body's frame

    Vec3 linearVelocity{};   // ignored (zero) for a body that starts asleep
    Vec3 angularVelocity{};
    float linearDamping = 0;
    float angularDamping = 0.05f;
    float maxAngularVelocity = 7;  // rad/s
    bool gravity = true;
    // NX_BF_FROZEN_POS_X/Y/Z and ROT_X/Y/Z. A body with all six set cannot move at all and is
    // created static, whatever `motion` says; setMotionType leaves it that way.
    bool lockTranslation[3] = {false, false, false};
    bool lockRotation[3] = {false, false, false};
    bool startAsleep = false;  // stays put until something touches it or wake() is called
    float sleepLinearVelocity = 0.15f;
    float sleepAngularVelocity = 0.14f;
    bool continuousCollision = false;  // for small fast bodies such as the ball
    std::uint64_t userData = 0;
};

enum class JointType { Fixed, Hinge, Ball };

struct JointLimit {
    float value = 0;        // radians
    float restitution = 0;  // bounce off the limit
};

// The PhysX 2.x joint model: each body has its own local frame for the joint (anchor,
// axis and a normal perpendicular to the axis). index 0 is bodyA, 1 is bodyB; for the
// world (BodyId 0) the frame is in world space.
struct JointDesc {
    JointType type = JointType::Fixed;
    BodyId bodyA = 0, bodyB = 0;
    Vec3 localAnchor[2]{};
    Vec3 localAxis[2]{{0, 0, 1}, {0, 0, 1}};
    Vec3 localNormal[2]{{1, 0, 0}, {1, 0, 0}};
    bool collideConnected = false;

    // The joint breaks (and is removed) when the force or torque it needed in a step
    // exceeds these. PhysX's maxForce and maxTorque.
    float breakForce = std::numeric_limits<float>::infinity();
    float breakTorque = std::numeric_limits<float>::infinity();

    // Hinge: the angle of bodyA relative to bodyB about the axis, measured from the normals (zero when they agree);
    // so a positive motor velocity turns A forwards and B backwards, as PhysX's does (the levels' windmills show it).
    // Ball: twist about the axis.
    bool limitEnabled = false;
    JointLimit limitLow, limitHigh;
    // Ball: cone half-angle around the axis.
    bool swingLimitEnabled = false;
    JointLimit swingLimit;

    // Hinge motor and spring.
    bool motorEnabled = false;
    float motorVelocity = 0;  // rad/s
    float motorMaxForce = 0;  // torque
    bool springEnabled = false;
    float spring = 0, damper = 0, springTarget = 0;
};

struct ContactEvent {
    BodyId a = 0, b = 0;
    std::uint64_t userDataA = 0, userDataB = 0;
    Vec3 point;           // world space
    Vec3 normal;          // world space, pointing from a to b
    float normalSpeed = 0;  // closing speed along the normal when the contact began
};

struct RayHit {
    BodyId body = 0;
    Vec3 point;
    Vec3 normal;
    float distance = 0;
};

struct PhysicsSettings {
    float fixedStep = 1.0f / 60.0f;  // step() advances in steps of exactly this size
    int maxStepsPerCall = 8;         // the remainder carries over; beyond this, time is dropped
    int collisionSteps = 1;          // Jolt sub-steps per fixed step
    // Jolt's solver iterations per sub-step. Its defaults (10 and 2) let a windmill's tower sink and topple within
    // a minute (a 5000 roof on 1800 stones, kept awake by the sails that turn on it) and joined models fly apart;
    // 100 hold it for minutes, at 0.15 ms a step in a level with a ball loose. One position iteration: 0 is not
    // usable (bodies never fall asleep and some sink into the floor), more only slows down the overlaps that
    // levels were saved with.
    int velocitySteps = 100;
    int positionSteps = 1;
    float bounceThreshold = 2.0f;    // PhysX's default: impacts slower than this (m/s) don't bounce
    int maxBodies = 32768;
    int maxBodyPairs = 65536;
    int maxContactConstraints = 32768;
    int threads = -1;  // worker threads; -1 = hardware concurrency - 1 but at most 3, 0 = run on the caller
};

class PhysicsWorld {
public:
    explicit PhysicsWorld(Vec3 gravity, const PhysicsSettings& settings = {});
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // Returns 0 (and logs why) if the description is unusable.
    BodyId addBody(const BodyDesc& desc);
    void removeBody(BodyId body);
    JointId addJoint(const JointDesc& desc);
    void removeJoint(JointId joint);

    // Advances the simulation by dt seconds in fixed steps (PhysicsSettings::fixedStep). `beforeStep`, if there is
    // one, is called ahead of every fixed step, for the caller that moves kinematic bodies with setKinematicTarget():
    // each step needs its own target, however many a call takes.
    // Returns the number of fixed steps taken.
    int step(float dt, const std::function<void()>& beforeStep = {});

    Mat4 pose(BodyId body) const;  // the body's frame, as given in BodyDesc::pose
    Vec3 linearVelocity(BodyId body) const;
    Vec3 angularVelocity(BodyId body) const;
    void setLinearVelocity(BodyId body, Vec3 velocity);
    void setAngularVelocity(BodyId body, Vec3 velocity);
    void addImpulse(BodyId body, Vec3 impulse, Vec3 worldPoint);
    void setPose(BodyId body, const Mat4& pose);  // teleport
    // Kinematic bodies: reach this pose by the end of the next fixed step, pushing what is in the way.
    void setKinematicTarget(BodyId body, const Mat4& pose);
    MotionType motionType(BodyId body) const;
    // A body that becomes kinematic stops where it is (its velocities are zeroed).
    void setMotionType(BodyId body, MotionType motion);
    bool isAwake(BodyId body) const;
    void wake(BodyId body);
    std::uint64_t userData(BodyId body) const;
    // The mass of a dynamic body, as the world has it (a body made from its shapes and density: the density times their volume);
    // 0 for a static or kinematic one, which no hit moves.
    float mass(BodyId body) const;

    // Contacts that began, and joints that broke, since the last call.
    std::vector<ContactEvent> takeContacts();
    std::vector<JointId> takeBrokenJoints();

    std::optional<RayHit> raycast(Vec3 origin, Vec3 direction, float maxDistance) const;

    // The settings in use: the ones given, with values that cannot work replaced.
    const PhysicsSettings& settings() const;

    std::size_t bodyCount() const;
    std::size_t awakeBodyCount() const;
    std::size_t jointCount() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace mb
