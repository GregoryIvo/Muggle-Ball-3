#pragma once

#include "mb/engine/Math.h"
#include "mb/engine/Physics.h"
#include "mb/game/Level.h"
#include "mb/game/MaterialLibrary.h"
#include "mb/game/PathPlayer.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <span>
#include <vector>

namespace mb {

// Collision groups of the level files. The files hold no filtering, the original's code set it, so
// this is what the data shows (docs/LEVEL_FORMAT.md, "Collision groups"): everything collides except
// group 5 (the marker spheres of lights, shadow casters and backgrounds, which sit inside the board)
// and group 11 (one box per board at the player's end, probably where the bat moves).
inline constexpr std::uint16_t kMarkerGroup = 5;
inline constexpr std::uint16_t kBatZoneGroup = 11;
// NX_TRIGGER_ON_ENTER | ON_LEAVE | ON_STAY: a shape with any of them only reports who is inside it.
inline constexpr std::uint32_t kTriggerShapeFlags = 0x7;

// Whether a shape takes part in collisions.
bool collides(const Shape& shape);

// Where the physics starts from. The levels were saved from a running simulation, with small
// velocities on their bodies and a few asleep; tests/physics_smoke.cpp --measure compares these.
enum class StartState {
    Asleep,           // every moving body asleep with zero velocity: nothing moves until something hits it, but the
                      // machines (what is joined to a motor) start awake with the velocity they were saved with
    SavedVelocities,  // awake, with the velocities the level was saved with
    Still,            // awake, with zero velocity
};

// How the level's joints break. Jolt solves joints before contacts, so a joint holding a body that stands on the
// floor carries the body's whole weight, and the loads inside a structure of joined bodies shift by many times
// the thresholds the levels give (a hundredth of a brick's weight, see tests/physics_smoke.cpp --measure) whenever
// the structure wakes up: judged by load, every joint of a ship lets go the moment something touches it. PhysX
// gave the weight to the contacts. So the scene breaks a joint itself, when a body it holds is hit.
enum class JointBreaking {
    Impact,  // a hit at least as hard as the joint's maxForce breaks it (see PhysicsScene::step); maxTorque is not used
    Load,    // PhysicsWorld's own: the force or torque the joint needs in a step exceeds maxForce or maxTorque
};

struct PhysicsOptions {
    StartState start = StartState::Asleep;
    JointBreaking breaking = JointBreaking::Impact;
    // Some joints were saved stretched (their two anchors up to 11 units apart). True rebuilds the second
    // body's frame of every joint from the saved poses, so that every joint starts at rest and nothing snaps
    // together; false uses the frames as stored and lets the solver pull the bodies together.
    bool rebuildJointFrames = true;
};

// One actor's body, as the level describes it. Actors with no collision shape (the markers) get no body.
struct ActorPlan {
    ObjectType type = ObjectType::Unknown;  // of the object the actor belongs to
    Mat4 pose;                              // rigid, as saved
    MotionType motion = MotionType::Static;
    bool simulated = false;                 // false: no body is made
    BodyDesc body;                          // only meaningful when simulated; userData is the ActorIndex
};

// A box of the level that is not part of the collisions: a board's group 11 strip, or a trigger box.
struct ZoneBox {
    ActorIndex actor = kNoActor;
    std::uint16_t group = 0;
    bool trigger = false;
    Mat4 local;  // relative to the actor
    Vec3 halfExtents{};
};

struct JointPlan {
    ActorIndex actorA = kNoActor;  // kNoActor: the world
    ActorIndex actorB = kNoActor;
    JointDesc desc;                // bodyA and bodyB stay 0 until the bodies exist
    bool toCloth = false;          // one end is a cloth or one of its segments (the corner of a sail)
};

struct PhysicsPlan {
    Vec3 gravity{};
    std::vector<ActorPlan> actors;  // indexed by ActorIndex, see flattenActors()
    std::vector<JointPlan> joints;
    std::vector<ZoneBox> zones;
    std::size_t pinnedBodies = 0;  // dynamic bodies made static because an unbreakable joint welds them to the world

    // What was left out, and why; all zero for the shipped levels but the cloth joints, see PhysicsScene.
    std::size_t unresolvedJoints = 0;   // a reference names no actor, or both ends are the world
    std::size_t unsupportedJoints = 0;  // a joint type the shipped levels do not use
    std::size_t jointsWithoutBody = 0;  // one end is an actor that has no body
    std::size_t unsupportedShapes = 0;  // planes, triangle meshes, hulls without points
};

// Maps a level to bodies and joints; pure, no physics world and no GPU.
//   - Poses (and shape and mass frames) are made rigid with rigidTransform().
//   - Boxes, spheres, capsules (halfHeight = height / 2) and convex hulls keep their shapes.
//   - Motion: all six frozen bits set (markers, the board) -> static; the kinematic flag -> kinematic;
//     otherwise dynamic with the frozen bits as locks and "disable gravity" as no gravity. Cloth and its
//     segments are kinematic: the original simulates them in game code, not in PhysX.
//   - A dynamic body that an unbreakable fixed joint welds to the world is static, and the joint is dropped.
//   - Mass, inertia, mass frame, damping and sleep thresholds are the body's, as saved. Dynamic bodies get swept
//     collision.
//   - Friction, restitution and their combine modes come from the game material named by the owning object
//     (a cloth segment: the cloth's); none, or one the library does not have, gives 0.5 / 0.5 / 0.
//   - A joint's actors are resolved as docs/LEVEL_FORMAT.md ("Joint actor references") says.
PhysicsPlan planPhysics(const Level& level, const MaterialLibrary& materials, const PhysicsOptions& options = {});

// PhysX material of a game material.
PhysicsMaterial physicsMaterial(const GameMaterial& material);

// The box round a body's shapes with its frame at `pose`, in world space.
struct Bounds {
    Vec3 low{1e30f, 1e30f, 1e30f};
    Vec3 high{-1e30f, -1e30f, -1e30f};

    void add(Vec3 p, float grow) {
        low = {std::min(low.x, p.x - grow), std::min(low.y, p.y - grow), std::min(low.z, p.z - grow)};
        high = {std::max(high.x, p.x + grow), std::max(high.y, p.y + grow), std::max(high.z, p.z + grow)};
    }
    bool near(const Bounds& other, float margin) const {
        return low.x < other.high.x + margin && high.x > other.low.x - margin && low.y < other.high.y + margin &&
               high.y > other.low.y - margin && low.z < other.high.z + margin && high.z > other.low.z - margin;
    }
};
Bounds boundsOf(const BodyDesc& body, const Mat4& pose);

// The points of a body's shapes (corners, hull points, the bottom of spheres and capsules) within 0.3 units of
// its lowest one, in world space with the body's frame at `pose`: where it would touch what it rests on.
std::vector<Vec3> lowestPoints(const BodyDesc& body, const Mat4& pose);

inline constexpr float kBallRadius = 0.6f;

// BodyDesc::userData of the bodies that belong to no actor (an actor's body carries its ActorIndex): the test balls
// of launchBall(), and the game's own balls and bat, see addGameBall().
inline constexpr std::uint64_t kTestBallData = kNoActor;
inline constexpr std::uint64_t kGameBallData = std::uint64_t{kNoActor} - 1;
inline constexpr std::uint64_t kBatData = std::uint64_t{kNoActor} - 2;

// How far under its lowest points a body may have the surface it rests on and still count as supported by it: the
// levels were saved with bricks above what holds them (320 dynamic bodies, by 8 mm to 15 cm, 175 of them by more than
// 2 cm), which no contact reaches.
inline constexpr float kSupportGap = 0.15f;

// A level running in a physics world, with the test balls that are shot into it.
class PhysicsScene {
public:
    // The 84 joints that hold a cloth's corners (75 ball joints and 9 fixed ones, on ships, boats and plots) are
    // not made: the cloth is kinematic until step 5 simulates it, so they would pin the ship to the air where its
    // sail was saved. Everything else of the plan is built. The actors that follow a path (docs/LEVEL_FORMAT.md,
    // "Path motion") start on it.
    PhysicsScene(const Level& level, const MaterialLibrary& materials, const PhysicsOptions& options = {},
                 const PhysicsSettings& settings = {});

    const PhysicsPlan& plan() const { return plan_; }
    PhysicsWorld& world() { return world_; }
    const PhysicsWorld& world() const { return world_; }

    // 0 for an actor that has no body (markers, and anything the world refused).
    BodyId body(ActorIndex actor) const { return bodies_[actor]; }
    // Where the actor is now; for one with no body, where it was saved.
    Mat4 actorPose(ActorIndex actor) const;
    // Whether a surface is within kSupportGap under its lowest points.
    bool supported(ActorIndex actor) const;
    // Whether a joint that has not broken still holds the actor.
    bool held(ActorIndex actor) const;

    // The actors that follow a path: kinematic bodies that step() moves along it, one fixed step at a time, so that
    // they push whatever is in the way. An actor whose body is gone (removeActor) drives no more.
    std::span<const PathMover> pathMovers() const { return movers_; }
    // Seconds of fixed steps the paths have been walked: where each mover is, see PathPlayer::frame().
    double pathTime() const { return pathTime_; }
    const PathPlayer& paths() const { return paths_; }

    // Takes an actor out of the world, body and joints: a brick that was destroyed, a piece that fell off the board.
    // Whatever rested on it falls: the sleeping bodies within 0.3 units of it wake at once (and with them whatever is
    // joined to them, which the look for hanging bodies skips), and the next step looks for others with nothing under
    // them. The pieces it was joined to wake, too. The actor has no body afterwards, like a marker; actorPose() gives
    // where it was saved, so a caller that draws it must stop. Does nothing for an actor with no body.
    void removeActor(ActorIndex actor);

    struct Counts {
        std::size_t dynamicBodies = 0, kinematicBodies = 0, staticBodies = 0;
        std::size_t withoutBody = 0;  // actors that take no part in the simulation
        std::size_t joints = 0;
        std::size_t jointsRefused = 0;  // in the plan, but the world could not make them
        std::size_t clothJoints = 0;    // left out until the cloth is simulated, see PhysicsScene::PhysicsScene
        std::size_t pathMovers = 0;     // kinematic bodies that follow a path, see pathMovers()
    };
    const Counts& counts() const { return counts_; }

    // Advances the simulation by dt seconds (fixed steps inside), the path actors going to where their paths have them
    // at the end of each, and then
    //  - breaks the joints that were hit hard enough (JointBreaking::Impact): a hit is a contact that begins at more
    //    than PhysicsSettings::bounceThreshold. It strikes a force of effective mass * closing speed / fixed step (the
    //    mass of the body that is hit if the other cannot move, else the reduced mass) and breaks every joint of both
    //    bodies whose maxForce is below that. The step a hit begins in still solves with the joints; the bodies the
    //    broken joint held wake up, or one asleep would stay where the joint left it;
    //  - wakes dynamic bodies that would hang in the air. Jolt wakes what a moving body touches, which does not
    //    reach a body a few centimetres above what it rests on, nor a body that fell asleep in the moment its
    //    support slid away (it falls asleep after 0.4 s of moving little, whether or not it is about to fall). So
    //    every body that rests on something (kSupportGap, looked at when the level is built and whenever it falls
    //    asleep) and has no joint holding it is looked at again every tenth of a second, and woken when that is
    //    gone. One that fell asleep with nothing under it is woken at once (three times at most: a body that leans
    //    on something touches it away from where the check looks);
    //  - removes test balls that are older than 20 s or have fallen below y = -30.
    void step(float dt);

    // The contacts that began in the last step() (all fixed steps of it), for the game rules; the scene itself only
    // uses them to break joints and to note what the test balls touched.
    std::span<const ContactEvent> contacts() const { return contacts_; }

    // The average position of the dynamic bodies as the level saved them: where to aim a test ball to hit something.
    // (0, 0, 0) for a level with none.
    Vec3 centreOfMovables() const;

    // Shoots a test ball: a sphere of kBallRadius with the game material "ballMaterial" and continuous collision
    // detection. Returns its body.
    BodyId launchBall(Vec3 position, Vec3 velocity);

    struct Ball {
        BodyId body = 0;
        double age = 0;  // seconds
    };
    std::span<const Ball> balls() const { return balls_; }

    // The ball of the game: a sphere of `radius` (kBallRadius, or what a prize makes it) with the game material "ballMaterial" and continuous collision
    // detection, kept on the horizontal plane through `position` (no gravity, no vertical motion, and it turns only
    // about the vertical, up to PhysX's default cap of 7 rad/s). Its friction is the material's, 0.5: the original's
    // walls turn the ball by 11 to 18 degrees on most bounces, in both directions, which spin and friction explain
    // (docs/GAMEPLAY.md, "Corrections"). Its mass is its volume times the material's density, as PhysX gave it; the
    // rules keep its speed. Not one of balls(): nothing ages or removes it, and it carries kGameBallData. Returns its
    // body (0 if the world refused).
    BodyId addGameBall(Vec3 position, Vec3 velocity, float radius = kBallRadius);
    // A kinematic sphere that moves only where setKinematicTarget() puts it, and pushes what it meets: the bat. It
    // has no friction, so that it turns no ball (the combined friction is the lesser). It carries kBatData. Returns
    // its body (0 if the world refused).
    BodyId addBat(Vec3 position, float radius);

    // Actors a test ball has touched, counting from the start.
    const std::set<ActorIndex>& touchedByBalls() const { return touched_; }

    // Indices into plan().joints of the joints that have broken, in the order they did.
    std::span<const std::size_t> brokenJoints() const { return broken_; }

private:
    PhysicsPlan plan_;
    PhysicsWorld world_;
    std::vector<BodyId> bodies_;  // by ActorIndex
    Counts counts_;
    PhysicsMaterial ballMaterial_;
    float ballDensity_ = 1;
    std::vector<Ball> balls_;
    std::vector<ContactEvent> contacts_;
    std::set<ActorIndex> touched_;
    PathPlayer paths_;
    std::vector<PathMover> movers_;  // the ones of paths_ with a kinematic body
    double pathTime_ = 0;

    std::vector<ActorIndex> fallingActors_;  // dynamic, with gravity and free to move down: see wakeUnsupported()
    std::vector<std::uint8_t> wasAwake_;     // by ActorIndex
    std::vector<std::uint8_t> rewoken_;      // by ActorIndex: times wakeUnsupported() woke it in the air
    std::vector<std::uint8_t> restsOn_;      // by ActorIndex: it stood on something when last looked at
    std::uint64_t steps_ = 0;
    bool lookNow_ = false;  // an actor was taken out: the next step looks at the sleepers that rested on something

    JointBreaking breaking_;
    float fixedStep_;
    float impactSpeed_;
    std::vector<JointId> jointIds_;                        // by plan joint; 0: not in the world (refused or broken)
    std::vector<std::vector<std::size_t>> jointsOfActor_;  // by ActorIndex: its plan joints that were built
    std::vector<std::size_t> planJointOfId_;               // by JointId, for Load breaking
    std::vector<std::size_t> broken_;

    bool isActor(std::uint64_t userData) const { return userData < plan_.actors.size(); }  // else a ball or the bat
    float massOf(std::uint64_t userData, BodyId body) const;  // infinity for what a hit cannot move
    void breakByHit(const ContactEvent& contact);
    void breakJoint(std::size_t planJoint);
    void releaseJoint(std::size_t planJoint);  // the joint is gone: its bodies wake, or one asleep would hang
    void jointBroke(std::size_t planJoint);
    void wakeUnsupported();
    void movePathActors();  // before each fixed step
};

}  // namespace mb
