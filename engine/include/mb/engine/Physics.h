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
    // Which bodies touch which. `group` is the kind of body this is (one bit) and `collidesWith` the kinds it touches; two
    // bodies touch only when each one's `collidesWith` has the other's `group`. The defaults touch everything, so only
    // the bodies that must pass through some others (a bullet through the ball and the bat) set them.
    std::uint32_t group = 1;
    std::uint32_t collidesWith = ~std::uint32_t{0};
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

// A sheet of cloth: a grid of particles, each tied to its neighbours by springs that hold its length, its diagonals and (more
// loosely) its folds. Jolt simulates it as a soft body: the particles fall under the world's gravity and are pushed out of
// whatever they touch, and push nothing (a few grams: see PhysicsWorld::addCloth()). Row-major, row 0 first, so the particle at
// column c of row r is r * width + c.
struct ClothDesc {
    int width = 0, height = 0;       // particles across and up, both at least 2
    std::vector<Vec3> positions;     // width * height points in world space: the sheet at rest, and where it starts
    float particleMass = 1;          // each; sets how hard a particle pushes what it meets
    float particleRadius = 0;        // how far a particle stays from the surface of what it meets
    // XPBD compliance (the inverse of a stiffness; 0 is as stiff as the solver can make it) of the springs along the
    // grid, across its cells, and across its folds. A negative bend compliance leaves the folds free.
    float stretchCompliance = 0;
    float shearCompliance = 0;
    float bendCompliance = -1;
    // The springs are this much longer than the distance between their particles at rest (0.05: 5 %): the sheet is cut with
    // slack, and a sheet pinned at its four corners where it lies has to bow out, the way the wind goes, instead of lying taut.
    // The springs grow to that length over `slackSeconds`, so that the sheet fills out instead of snapping into its bow: a sheet
    // that starts compressed and flat gives way all at once.
    float slack = 0;
    float slackSeconds = 0;
    float damping = 0.1f;            // of the particles' speed, per second
    float friction = 0.2f;           // against what it touches
    float restitution = 0;           // of its bounce off what it touches
    int iterations = 5;              // solver passes a step
    std::uint64_t userData = 0;      // what ContactEvent says of it, see addCloth()
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

    // Contacts that began, and joints that broke, since the last call.
    std::vector<ContactEvent> takeContacts();
    std::vector<JointId> takeBrokenJoints();

    std::optional<RayHit> raycast(Vec3 origin, Vec3 direction, float maxDistance) const;
    // Every body the ray crosses within maxDistance, nearest first, each once, at the point where the ray enters it (a body
    // that holds the origin is hit at distance 0). Ignores the kinds of body, as raycast() does.
    std::vector<RayHit> raycastAll(Vec3 origin, Vec3 direction, float maxDistance) const;

    // ---- Cloth
    // A sheet of cloth is a body of its own, with an id that removeBody(), userData(), raycast() (which hits its surface from
    // either side) and the contact events know it by; its pose is the identity and its particles are in world space. Returns 0
    // (and logs why) when the description is unusable.
    //
    // It collides particle by particle, with a particle's radius, against every body but the ones it was told to go through
    // (clothIgnores()); the surface between the particles is not there to a body that moves (it is to a ray), so what is smaller
    // than the gap goes through, as it did the original's spheres. Nothing it touches is moved or woken by it. A touch begins a
    // ContactEvent with `a` the cloth and `b` what it met, the normal pointing from the cloth to it and normalSpeed the speed at
    // which that closed on the particle (the particle counted as still: the sheet is light); like the rigid bodies', only the step
    // a touch begins in reports it. A cloth that has settled goes to sleep, and wakes when a pin goes elsewhere or something that
    // moves comes near.
    BodyId addCloth(const ClothDesc& desc);
    // Where the particles are now, in the order of ClothDesc::positions; empty for an id that is no cloth.
    std::vector<Vec3> clothPositions(BodyId cloth) const;
    // Holds a particle at `position` from the next fixed step on, the springs carrying the rest of the sheet after it, until
    // releaseClothParticle(): call again before each step to move it (the speed that takes it there is its own).
    void pinClothParticle(BodyId cloth, int particle, Vec3 position);
    // Lets a pinned particle go, moving as the pin was.
    void releaseClothParticle(BodyId cloth, int particle);
    // The wind: an acceleration (units per second squared) that every particle takes, along the surface's normal, by its
    // share of the wind square to the sheet there (a sheet edge-on to it feels nothing). Zero is no wind.
    void setClothWind(BodyId cloth, Vec3 acceleration);
    // The cloth goes through `other` from now on: what it hangs from. It still touches everything else.
    void clothIgnores(BodyId cloth, BodyId other);
    void clothIgnoresNothing(BodyId cloth);
    // How many times the solver has sent a cloth's particles off beyond reach (a pin dragged out of the world, a squeeze it could not
    // solve), which put them back where they last were; after the third the cloth is held still where it is. Zero for a cloth that
    // is well and for an id that is none.
    int clothResets(BodyId cloth) const;

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
