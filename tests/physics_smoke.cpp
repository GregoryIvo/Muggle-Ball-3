// Runs every converted level in the physics world (needs the game data: skips with exit code 77 when
// MB3_ASSETS is not set), no GPU.
//
//   mb_physics_smoke [--shots N] [--seconds S] [--physics-profile name] [level]
//        the test: each level stands for S seconds on its own (default 10): what is not a running machine or an actor on a
//        path must not move, and the machines must turn the way their motors drive them. Then N test balls are shot into it, one
//        at a time into a fresh copy (default 4, at most 24, from where the bat is, at different x and angles), S
//        seconds each, and nothing may blow up, sink through the floor or be left hanging in the air. A failing
//        level is named; the levels no ball reached are listed. Under each level's row, a line of fixed steps taken and dropped
//        (PhysicsWorld::droppedSteps, not a failure here: the host can drop steps when it stalls) and the peak awake and total
//        body counts; the summary totals them.
//   mb_physics_smoke --measure [--physics-profile name] [level]
//        prints how the ways of starting from the saved state compare, see measure(); each level's row ends with the peak bodies
//        and peak awake bodies seen over every configuration, and the summary gives the peak over all levels and the Vita
//        profile's world size rule applied to it (docs/PHYSICS.md).
//   --physics-profile (default "desktop", see mb/game/PhysicsProfile.h) picks every level's starting PhysicsSettings; the four
//   flags below still apply on top of it, for either mode, so a run can test a variant of a profile.
//   --velocity-steps, --position-steps, --collision-steps and --threads change the world's settings for either.

#include "mb/game/LevelCatalog.h"
#include "mb/game/LevelLoader.h"
#include "mb/game/MaterialLibrary.h"
#include "mb/game/PhysicsProfile.h"
#include "mb/game/PhysicsScene.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <optional>
#include <filesystem>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace mb;

namespace {

constexpr float kStep = 1.0f / 60.0f;

// --physics-profile: every scene's settings start here, by level id; a command line that passes the flag
// overrides this in main(). Safe to build at static-init time: physicsProfile() reaches PhysicsProfile.cpp's
// table through a function-local static, which C++ builds the first time anything calls it, in whatever order
// translation units' globals are otherwise initialized.
PhysicsProfile gProfile = *physicsProfile("desktop");
// --velocity-steps, --position-steps, --collision-steps, --threads: on top of the profile, for every level, so a
// run can compare solver settings against a profile other than plain defaults.
std::optional<int> gVelocitySteps, gPositionSteps, gCollisionSteps, gThreads;

// What a scene for `levelId` is built with: the profile's settings (the level's override if it has one), with
// any of the four solver flags above applied on top.
PhysicsSettings settingsFor(std::string_view levelId) {
    PhysicsSettings s = physicsSettingsFor(gProfile, levelId);
    if (gVelocitySteps) {
        s.velocitySteps = *gVelocitySteps;
    }
    if (gPositionSteps) {
        s.positionSteps = *gPositionSteps;
    }
    if (gCollisionSteps) {
        s.collisionSteps = *gCollisionSteps;
    }
    if (gThreads) {
        s.threads = *gThreads;
    }
    return s;
}

constexpr float kMoveLimit = 0.02f;       // a body that moves more than 2 cm has not stayed where it was designed
constexpr float kMachineSettle = 0.25f;   // how far what a running machine rests on may settle
// Thin light pieces of wrecked ships go through the floor in about one shot in 700 (3 of 1,992 with 24 shots per level,
// 1 of 664 with 8): a small share of the shots may end that way.
constexpr double kMostSunkShots = 0.01;
constexpr float kRunSeconds = 10.0f;
constexpr float kMaxSpeed = 200.0f;       // no body of a sane simulation is faster than this (units per second)

struct Outcome {
    std::size_t bodies = 0;       // moving bodies (dynamic and kinematic)
    std::size_t joints = 0;
    std::size_t moved = 0;        // bodies that ended more than kMoveLimit from where they started
    std::size_t brokenJoints = 0;
    std::size_t awakeAtEnd = 0;
    float worstMove = 0;
    float fastest = 0;            // fastest body speed seen, units per second
    bool nonFinite = false;
    double stepMs = 0;            // average time of a fixed step
    std::uint64_t droppedSteps = 0;  // PhysicsWorld::droppedSteps(), at the end of the run
    int peakBodies = 0, peakActive = 0;  // the world's body count and Jolt's active body count, peak over the run
    std::size_t tempAllocatorPeakBytes = 0;  // PhysicsWorld::tempAllocatorPeakBytes(), at the end of the run
    struct Mover {
        ActorIndex actor;
        float distance;
    };
    std::vector<Mover> movers;  // the bodies counted in `moved`
};

bool finite(const Mat4& m) {
    for (const auto& row : m.m) {
        for (const float v : row) {
            if (!std::isfinite(v)) {
                return false;
            }
        }
    }
    return true;
}

// The actors that follow a path: they are meant to move, and to turn as fast as the path does (a shark that pivots on the
// spot at the end of its run swings its centre of mass, a few units from the origin of its frame, round at whatever speed
// that takes). mb_path_smoke has them.
std::vector<bool> pathActors(const PhysicsScene& scene) {
    std::vector<bool> onPath(scene.plan().actors.size(), false);
    for (const PathMover& mover : scene.pathMovers()) {
        onPath[mover.actor] = true;
    }
    return onPath;
}

// Moves the world on by `seconds`, tracking the bodies the plan makes dynamic or kinematic (but the path actors' speed).
Outcome run(PhysicsScene& scene, float seconds) {
    Outcome out;
    const PhysicsPlan& plan = scene.plan();
    const std::vector<bool> onPath = pathActors(scene);
    std::vector<ActorIndex> tracked;
    std::vector<Mat4> start;
    for (ActorIndex i = 0; i < plan.actors.size(); ++i) {
        if (scene.body(i) != 0 && plan.actors[i].motion != MotionType::Static && scene.cloths().clothOf(i) < 0) {  // (the sails: mb_cloth_smoke)
            tracked.push_back(i);
            start.push_back(scene.actorPose(i));
        }
    }
    out.bodies = tracked.size();
    out.joints = scene.counts().joints;
    out.peakBodies = static_cast<int>(scene.world().bodyCount());  // does not change during the run: no body is added or removed here

    const int steps = static_cast<int>(std::lround(seconds / kStep));
    const auto begin = std::chrono::steady_clock::now();
    for (int s = 0; s < steps; ++s) {
        scene.step(kStep);
        if (s % 6 == 5) {  // a tenth of a second is fine-grained enough to catch a body that flies away
            for (const ActorIndex i : tracked) {
                if (onPath[i]) {
                    continue;
                }
                const float speed = length(scene.world().linearVelocity(scene.body(i)));
                out.fastest = std::max(out.fastest, std::isfinite(speed) ? speed : 1e9f);
            }
            out.peakActive = std::max(out.peakActive, scene.world().activeBodyCount());
        }
    }
    out.brokenJoints = scene.brokenJoints().size();
    out.stepMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() / steps;
    out.droppedSteps = scene.world().droppedSteps();
    out.tempAllocatorPeakBytes = scene.world().tempAllocatorPeakBytes();

    for (std::size_t k = 0; k < tracked.size(); ++k) {
        const Mat4 pose = scene.actorPose(tracked[k]);
        if (!finite(pose)) {
            out.nonFinite = true;
            continue;
        }
        const float moved = length(pose.translationPart() - start[k].translationPart());
        out.worstMove = std::max(out.worstMove, moved);
        out.moved += moved > kMoveLimit;
        if (moved > kMoveLimit) {
            out.movers.push_back({tracked[k], moved});
        }
        out.awakeAtEnd += scene.world().isAwake(scene.body(tracked[k]));
    }
    return out;
}

// Shot from where the bat would be, at the middle of everything that can move.
void shootBall(PhysicsScene& scene) {
    const Vec3 from{0, 1.2f, -15};
    Vec3 aim = scene.centreOfMovables();
    aim.y = from.y;
    scene.launchBall(from, normalize(aim - from) * 25.0f);
}

// What the Space key does: from the bat's place along the board, at 25 units per second, a little off straight on.
struct Shot {
    float x, degrees;
};
constexpr Shot kShots[] = {{0, 0}, {-6, 4}, {6, -4}, {2, -5}, {-3, 5}, {9, 0}, {-9, 0}, {0, 3}, {-12, 2}, {12, -2}, {4, 4}, {-4, -4}, {7, 3}, {-7, -3}, {1, 5}, {-1, -5}, {-10, 5}, {10, -5}, {5, 0}, {-5, 0}, {3, -3}, {-2, 2}, {8, -4}, {-8, 4}};
constexpr int kMostShots = static_cast<int>(std::size(kShots));

void shoot(PhysicsScene& scene, int shot) {
    // The first is aimed at the middle of the level; the others are the pattern above.
    if (shot == 0) {
        shootBall(scene);
        return;
    }
    const Shot s = kShots[shot];
    const float angle = radians(s.degrees);
    scene.launchBall({s.x, 1.2f, -15}, {std::sin(angle) * 25.0f, 0, std::cos(angle) * 25.0f});
}

// The floor the level stands on: the board's boxes whose top is at y = 0 (some are turned about y).
struct Floor {
    struct Slab {
        Mat4 toLocal;
        float halfX, halfZ;
    };
    std::vector<Slab> slabs;

    bool contains(Vec3 p, float margin) const {
        return std::ranges::any_of(slabs, [&](const Slab& slab) {
            const Vec3 local = transformPoint(p, slab.toLocal);
            return std::fabs(local.x) < slab.halfX - margin && std::fabs(local.z) < slab.halfZ - margin;
        });
    }
};

Floor floorOf(const PhysicsPlan& plan) {
    Floor floor;
    for (const ActorPlan& actor : plan.actors) {
        if (actor.type != ObjectType::Board) {
            continue;
        }
        for (const PhysicsShape& shape : actor.body.shapes) {
            const Mat4 world = shape.localPose * actor.pose;
            const Vec3 up = transformVector({0, 1, 0}, world);  // the box's y axis
            const float top = world.translationPart().y + shape.halfExtents.y * up.y;
            if (shape.type == PhysicsShapeType::Box && std::fabs(top) < 0.1f && up.y > 0.99f) {
                floor.slabs.push_back({inverse(world), shape.halfExtents.x, shape.halfExtents.z});
            }
        }
    }
    return floor;
}

// Bodies that start awake because a motor drives them: the windmills and the Ferris wheel.
std::vector<bool> machineBodies(const PhysicsScene& scene) {
    std::vector<bool> machine(scene.plan().actors.size(), false);
    for (ActorIndex i = 0; i < machine.size(); ++i) {
        const ActorPlan& actor = scene.plan().actors[i];
        machine[i] = scene.body(i) != 0 && actor.motion == MotionType::Dynamic && !actor.body.startAsleep;
    }
    return machine;
}

// How fast body A of a motor turns relative to body B about the joint's axis, now. The joint drives it towards the
// motor's velocity (PhysX's convention, see JointDesc).
float motorSpeed(const PhysicsScene& scene, const JointPlan& joint) {
    const Mat4 poseA = scene.actorPose(joint.actorA);
    const Vec3 axis = normalize(transformVector(joint.desc.localAxis[0], poseA));
    return dot(scene.world().angularVelocity(scene.body(joint.actorA)) -
                   scene.world().angularVelocity(scene.body(joint.actorB)),
               axis);
}

// Bodies that stood on something at the start and are now asleep with nothing under them and no joint holding them:
// they would fall if anything woke them. Woken together and given half a second, those that drop by more than a
// support gap and a half hung in the air (a level saved a brick that much above what it rests on, and settles by it).
std::size_t hangingBodies(PhysicsScene& scene, const std::vector<ActorIndex>& resting, ActorIndex& first) {
    std::vector<ActorIndex> candidates;
    std::vector<float> height;
    for (const ActorIndex i : resting) {
        if (!scene.held(i) && !scene.world().isAwake(scene.body(i)) && !scene.supported(i)) {
            candidates.push_back(i);
            height.push_back(scene.actorPose(i).translationPart().y);
            scene.world().wake(scene.body(i));
        }
    }
    run(scene, 0.5f);
    std::size_t hanging = 0;
    for (std::size_t k = 0; k < candidates.size(); ++k) {
        if (height[k] - scene.actorPose(candidates[k]).translationPart().y > 1.5f * kSupportGap) {
            hanging += 1;
            first = hanging == 1 ? candidates[k] : first;
        }
    }
    return hanging;
}

struct Totals {
    std::size_t levels = 0, joints = 0, balls = 0, hits = 0, broken = 0, resting = 0, hanging = 0, sunk = 0, pinned = 0;
    std::size_t sunkShots = 0;
    std::size_t machines = 0;
    PhysicsScene::Counts counts;
    double stepMs = 0;       // summed over the balls: the mean time of a fixed step of each run
    double worstStepMs = 0;  // the slowest run
    std::string worstLevel;
    std::uint64_t totalFixedSteps = 0, totalDroppedSteps = 0;
    int peakBodiesOfAll = 0, peakActiveOfAll = 0;
    int levelsWithDroppedSteps = 0;
    std::size_t peakTempAllocatorBytesOfAll = 0;  // FallbackTempAllocator's high-water mark, peak over every level
};

// A level stands on its own: nothing that is not a machine, or touched by one, may move or break; and each motor
// must turn what it drives, the way it is told to.
Outcome standAlone(const Level& level, const MaterialLibrary& materials, float seconds, std::string_view levelId, Totals& total,
                   const std::function<void(const std::string&)>& fail) {
    PhysicsScene scene(level, materials, {}, settingsFor(levelId));
    const PhysicsPlan& plan = scene.plan();
    const PhysicsScene::Counts& counts = scene.counts();
    total.counts.dynamicBodies += counts.dynamicBodies;
    total.counts.kinematicBodies += counts.kinematicBodies;
    total.counts.staticBodies += counts.staticBodies;
    total.counts.withoutBody += counts.withoutBody;
    total.counts.clothJoints += counts.clothJoints;
    total.counts.cloths += counts.cloths;
    total.counts.clothParticles += counts.clothParticles;
    total.joints += counts.joints;
    total.pinned += plan.pinnedBodies;
    if (counts.dynamicBodies == 0) {
        fail("no dynamic body");
    }
    if (plan.unresolvedJoints + plan.unsupportedJoints + plan.jointsWithoutBody + counts.jointsRefused +
            plan.unsupportedShapes > 0) {
        fail(std::format("{} joints unresolved, {} unsupported, {} without a body, {} refused, {} shapes unsupported",
                         plan.unresolvedJoints, plan.unsupportedJoints, plan.jointsWithoutBody, counts.jointsRefused,
                         plan.unsupportedShapes));
    }

    const std::vector<bool> machine = machineBodies(scene);
    std::vector<const JointPlan*> motors;
    for (const JointPlan& joint : plan.joints) {
        if (joint.desc.motorEnabled && !joint.toCloth) {
            motors.push_back(&joint);
        }
    }
    const std::size_t machines = static_cast<std::size_t>(std::ranges::count(machine, true));
    total.machines += machines;
    const Outcome alone = run(scene, seconds);
    // What a running machine rests on settles under its weight (a windmill's tower, saved with overlaps, by up to
    // 0.25), so beside a machine the bodies that are none may move that much.
    const float allowed = machines > 0 ? kMachineSettle : kMoveLimit;
    const std::vector<bool> onPath = pathActors(scene);
    std::size_t strays = 0;
    ActorIndex first = kNoActor;
    for (const Outcome::Mover& mover : alone.movers) {
        if (!machine[mover.actor] && !onPath[mover.actor] && mover.distance > allowed) {
            first = strays++ == 0 ? mover.actor : first;
        }
    }
    if (strays > 0 || alone.brokenJoints > 0 || alone.nonFinite) {
        fail(std::format("on its own {} bodies that are no machine moved more than {:.0f} cm{}, {} joints broke", strays,
                         static_cast<double>(allowed) * 100,
                         first == kNoActor ? std::string()
                                           : std::format(" (the first is actor {}, {})", first,
                                                         objectTypeName(plan.actors[first].type)),
                         alone.brokenJoints));
    }
    for (const JointPlan* motor : motors) {
        // (The Ferris wheel's motor has 80 against a wheel of 70,000 and cannot reach its 3 rad/s: turning the right way
        // at a tenth of a radian per second is enough.)
        const float speed = motorSpeed(scene, *motor), target = motor->desc.motorVelocity;
        if (speed * target <= 0 || std::fabs(speed) < std::min(0.1f, 0.25f * std::fabs(target))) {
            fail(std::format("a motor drives at {:.2f} rad/s and its joint turns at {:.2f}",
                             static_cast<double>(target), static_cast<double>(speed)));
        }
    }
    return alone;
}

// The test: each level, built the way the game builds it, stands for `seconds` with nothing near it, then gets
// test balls one at a time.
int test(const MaterialLibrary& materials, const LevelCatalog& catalog, int shots, float seconds, const char* only) {
    int failed = 0;
    std::vector<std::string> untouched;
    Totals total;
    for (const LevelEntry& entry : catalog.levels()) {
        if (only && entry.name.find(only) == std::string::npos) {
            continue;
        }
        ++total.levels;
        const Level level = loadLevelJson(entry.path);
        const auto fail = [&](const std::string& what) {
            std::printf("FAIL %s: %s\n", entry.name.c_str(), what.c_str());
            ++failed;
        };
        const Outcome alone = standAlone(level, materials, seconds, entry.name, total, fail);
        std::uint64_t levelFixedSteps = static_cast<std::uint64_t>(std::lround(seconds / kStep));
        std::uint64_t levelDroppedSteps = alone.droppedSteps;
        int levelPeakBodies = alone.peakBodies, levelPeakActive = alone.peakActive;
        std::size_t levelPeakTempBytes = alone.tempAllocatorPeakBytes;

        bool touchedSomething = false;
        for (int shot = 0; shot < shots; ++shot) {
            PhysicsScene scene(level, materials, {}, settingsFor(entry.name));
            const Floor floor = floorOf(scene.plan());
            // Dynamic bodies that stand on something now (a stack, a plank on a post, a brick on the floor).
            std::vector<ActorIndex> resting;
            for (ActorIndex i = 0; i < scene.plan().actors.size(); ++i) {
                if (scene.body(i) != 0 && scene.plan().actors[i].motion == MotionType::Dynamic && scene.supported(i)) {
                    resting.push_back(i);
                }
            }
            shoot(scene, shot);
            const Outcome shotOutcome = run(scene, seconds);
            ++total.balls;
            total.broken += shotOutcome.brokenJoints;
            total.stepMs += shotOutcome.stepMs;
            levelFixedSteps += static_cast<std::uint64_t>(std::lround(seconds / kStep));
            levelDroppedSteps += shotOutcome.droppedSteps;
            levelPeakBodies = std::max(levelPeakBodies, shotOutcome.peakBodies);
            levelPeakActive = std::max(levelPeakActive, shotOutcome.peakActive);
            levelPeakTempBytes = std::max(levelPeakTempBytes, shotOutcome.tempAllocatorPeakBytes);
            if (shotOutcome.stepMs > total.worstStepMs) {
                total.worstStepMs = shotOutcome.stepMs;
                total.worstLevel = entry.name;
            }
            const std::string label = shots > 1 ? std::format(" (shot {}: x {:.0f}, {:.0f} degrees)", shot,
                                                              static_cast<double>(kShots[shot].x),
                                                              static_cast<double>(kShots[shot].degrees))
                                                : "";
            if (shotOutcome.nonFinite) {
                fail("a pose is not finite after the ball" + label);
            }
            if (shotOutcome.fastest > kMaxSpeed) {
                fail(std::format("a body reached {:.0f} units per second{}", static_cast<double>(shotOutcome.fastest), label));
            }

            // Nothing may have gone through the floor: a body over the board whose lowest point is more than half a
            // unit under the floor's plane, which it was not at the start. (A thin light fragment of a wrecked ship,
            // thrown by a ball that is 150 times its weight, now and then does; see kMostSunkShots.)
            bool sunkInThisShot = false;
            for (ActorIndex i = 0; i < scene.plan().actors.size(); ++i) {
                if (scene.body(i) == 0 || scene.plan().actors[i].motion != MotionType::Dynamic) {
                    continue;
                }
                const Vec3 p = scene.actorPose(i).translationPart();
                const auto lowestY = [&](const Mat4& pose) {
                    float lowest = 1e9f;
                    for (const Vec3 q : lowestPoints(scene.plan().actors[i].body, pose)) {
                        lowest = std::min(lowest, q.y);
                    }
                    return lowest;
                };
                if (lowestY(scene.actorPose(i)) < -0.5f && lowestY(scene.plan().actors[i].pose) > -0.5f &&
                    floor.contains(p, 1.0f)) {
                    ++total.sunk;
                    sunkInThisShot = true;
                    std::printf("note %s: actor %u (%s) lies under the board at (%.1f %.1f %.1f)%s\n", entry.name.c_str(), i,
                                std::string(objectTypeName(scene.plan().actors[i].type)).c_str(), static_cast<double>(p.x),
                                static_cast<double>(p.y), static_cast<double>(p.z), label.c_str());
                }
            }

            total.sunkShots += sunkInThisShot;

            // The pieces do not hang in the air after the hit (and give the scene the moment it takes to notice).
            const Outcome settleOutcome = run(scene, 0.25f);
            levelFixedSteps += static_cast<std::uint64_t>(std::lround(0.25f / kStep));
            levelDroppedSteps += settleOutcome.droppedSteps;
            levelPeakBodies = std::max(levelPeakBodies, settleOutcome.peakBodies);
            levelPeakActive = std::max(levelPeakActive, settleOutcome.peakActive);
            levelPeakTempBytes = std::max(levelPeakTempBytes, settleOutcome.tempAllocatorPeakBytes);
            ActorIndex firstHanging = kNoActor;
            const std::size_t hanging = hangingBodies(scene, resting, firstHanging);
            total.hanging += hanging;
            total.resting += resting.size();
            if (hanging > 0) {
                const ActorPlan& first = scene.plan().actors[firstHanging];
                fail(std::format("{} bodies that stood on something hang in the air after the ball, the first is actor "
                                 "{} ({}, at y {:.2f}){}",
                                 hanging, firstHanging, objectTypeName(first.type),
                                 static_cast<double>(first.pose.translationPart().y), label));
            }

            touchedSomething = touchedSomething || std::ranges::any_of(scene.touchedByBalls(), [&](ActorIndex actor) {
                return scene.plan().actors[actor].type != ObjectType::Board;
            });
        }
        total.hits += touchedSomething;
        if (!touchedSomething) {
            untouched.push_back(entry.name);
        }
        // Not failed on: the host can drop steps when the machine running the test stalls (docs/PHYSICS.md).
        std::printf("  physics: %llu steps (%llu dropped), peak %d awake, %d bodies, temp allocator peak %zu bytes\n",
                    static_cast<unsigned long long>(levelFixedSteps), static_cast<unsigned long long>(levelDroppedSteps),
                    levelPeakActive, levelPeakBodies, levelPeakTempBytes);
        total.totalFixedSteps += levelFixedSteps;
        total.totalDroppedSteps += levelDroppedSteps;
        total.levelsWithDroppedSteps += levelDroppedSteps > 0;
        total.peakBodiesOfAll = std::max(total.peakBodiesOfAll, levelPeakBodies);
        total.peakActiveOfAll = std::max(total.peakActiveOfAll, levelPeakActive);
        total.peakTempAllocatorBytesOfAll = std::max(total.peakTempAllocatorBytesOfAll, levelPeakTempBytes);
    }
    const PhysicsScene::Counts& counts = total.counts;
    std::printf("physics: %zu levels, %zu dynamic (%zu of them running machines), %zu kinematic and %zu static bodies "
                "(%zu actors without one; %zu bodies welded to the world are among the static), %zu joints (and %zu more that "
                "pin the corners of %zu sails, whose %zu actors are particles)\n",
                total.levels, counts.dynamicBodies, total.machines, counts.kinematicBodies, counts.staticBodies,
                counts.withoutBody, total.pinned, total.joints, counts.clothJoints, counts.cloths, counts.clothParticles);
    std::printf("%zu test balls (%d per level) touched something besides the board in %zu levels and broke %zu joints; "
                "of %zu bodies standing on something, %zu hang in the air at the end, %zu went through the floor (in %zu shots)\n",
                total.balls, shots, total.hits, total.broken, total.resting, total.hanging, total.sunk, total.sunkShots);
    const PhysicsSettings summarySettings = settingsFor({});
    std::printf("a fixed step took %.3f ms on average with a ball in the level, %.3f ms in the slowest run (%s); solver: "
                "%d velocity, %d position and %d collision steps\n",
                total.stepMs / static_cast<double>(std::max<std::size_t>(total.balls, 1)), total.worstStepMs,
                total.worstLevel.c_str(), summarySettings.velocitySteps, summarySettings.positionSteps,
                summarySettings.collisionSteps);
    std::printf("physics profile \"%.*s\": %llu fixed steps over all levels, %llu dropped (in %d levels; the host can drop "
                "steps when it stalls, which is not a failure here); peak %d bodies awake at once, %d bodies in the "
                "largest level; temp allocator peak %zu bytes (of %zu MiB)\n",
                static_cast<int>(gProfile.name.size()), gProfile.name.data(), static_cast<unsigned long long>(total.totalFixedSteps),
                static_cast<unsigned long long>(total.totalDroppedSteps), total.levelsWithDroppedSteps, total.peakActiveOfAll,
                total.peakBodiesOfAll, total.peakTempAllocatorBytesOfAll, gProfile.settings.tempAllocatorBytes >> 20);
    if (!untouched.empty()) {
        std::printf("the balls touched nothing in:");
        for (const std::string& name : untouched) {
            std::printf(" %s", name.c_str());
        }
        std::printf("\n");
    }
    if (static_cast<double>(total.sunkShots) > kMostSunkShots * static_cast<double>(total.balls)) {
        std::printf("FAIL all: a body went through the floor in %zu of %zu shots (%zu bodies)\n", total.sunkShots, total.balls,
                    total.sunk);
        ++failed;
    }
    // "Most": a ball is shot at the middle of the movable bodies or along the board, which can be an empty patch of floor.
    if (!only && total.hits * 4 < total.levels * 3) {
        std::printf("FAIL all: the balls touched something in only %zu of %zu levels\n", total.hits, total.levels);
        ++failed;
    }
    return failed == 0 && total.levels > 0 ? 0 : 1;
}

// How far apart a joint's two anchors are in world space with the frames as the level saved them.
float stretch(const PhysicsPlan& plan, const JointPlan& joint) {
    const auto anchor = [&](ActorIndex actor, Vec3 local) {
        return actor == kNoActor ? local : transformPoint(local, plan.actors[actor].pose);
    };
    return length(anchor(joint.actorA, joint.desc.localAnchor[0]) - anchor(joint.actorB, joint.desc.localAnchor[1]));
}

// How many dynamic bodies a level moves by more than 2 cm in its first 0.1 s awake (zero velocities), and how far the
// furthest goes: what joints saved stretched do when they are let pull their bodies together.
Outcome firstTenth(const Level& level, const MaterialLibrary& materials, bool rebuildJointFrames, std::string_view levelId) {
    PhysicsOptions options;
    options.start = StartState::Still;
    options.rebuildJointFrames = rebuildJointFrames;
    PhysicsScene scene(level, materials, options, settingsFor(levelId));
    std::vector<Vec3> start;
    for (ActorIndex i = 0; i < scene.plan().actors.size(); ++i) {
        start.push_back(scene.actorPose(i).translationPart());
    }
    for (int i = 0; i < 6; ++i) {
        scene.step(kStep);
    }
    Outcome out;
    for (ActorIndex i = 0; i < start.size(); ++i) {
        if (scene.body(i) != 0 && scene.plan().actors[i].motion == MotionType::Dynamic) {
            const float moved = length(scene.actorPose(i).translationPart() - start[i]);
            out.worstMove = std::max(out.worstMove, moved);
            out.moved += moved > kMoveLimit;
        }
    }
    return out;
}

struct Config {
    const char* name;
    PhysicsOptions options;
    bool ball;
};

// How the ways of starting from the saved state, and the two ways of breaking joints, do over kRunSeconds, without
// a ball and with one: bodies that ended more than 2 cm from where they began, and joints that broke.
int measure(const MaterialLibrary& materials, const LevelCatalog& catalog, const char* only) {
    using enum StartState;
    constexpr JointBreaking load = JointBreaking::Load, impact = JointBreaking::Impact;
    int peakBodiesOfAll = 0, peakActiveOfAll = 0;  // over every level and every configuration below
    std::size_t peakTempBytesOfAll = 0;  // FallbackTempAllocator's high-water mark, likewise
    const Config configs[] = {
        {"A load", {.start = Asleep, .breaking = load}, false},
        {"B load", {.start = SavedVelocities, .breaking = load}, false},
        {"C load", {.start = Still, .breaking = load}, false},
        {"B load, file frames", {.start = SavedVelocities, .breaking = load, .rebuildJointFrames = false}, false},
        {"C load, file frames", {.start = Still, .breaking = load, .rebuildJointFrames = false}, false},
        {"C impact, file frames", {.start = Still, .breaking = impact, .rebuildJointFrames = false}, false},
        {"A impact", {.start = Asleep, .breaking = impact}, false},
        {"B impact", {.start = SavedVelocities, .breaking = impact}, false},
        {"C impact", {.start = Still, .breaking = impact}, false},
        {"A load + ball", {.start = Asleep, .breaking = load}, true},
        {"A impact + ball", {.start = Asleep, .breaking = impact}, true},
    };
    constexpr std::size_t kConfigs = std::size(configs);
    std::vector<Outcome> total(kConfigs);
    std::vector<std::size_t> levelsMoved(kConfigs, 0), levelsBroken(kConfigs, 0);
    std::size_t levels = 0, joints = 0, stretched = 0, stretchedFar = 0;
    Outcome snapRebuilt, snapAsSaved;
    float mostStretched = 0;
    std::string mostStretchedLevel;
    std::printf("each cell: bodies moved more than 2 cm / joints broken (of all bodies and joints of the level)\n");
    std::printf("%-28s %6s %6s", "level", "bodies", "joints");
    for (const Config& c : configs) {
        std::printf(" | %-*s", 11, c.name);
    }
    std::printf("\n");
    for (const LevelEntry& entry : catalog.levels()) {
        if (only && entry.name.find(only) == std::string::npos) {
            continue;
        }
        const Level level = loadLevelJson(entry.path);
        ++levels;
        const PhysicsPlan saved = planPhysics(level, materials, {.rebuildJointFrames = false});
        for (const JointPlan& joint : saved.joints) {
            const float apart = stretch(saved, joint);
            ++joints;
            stretched += apart > 0.05f;
            stretchedFar += apart > 1.0f;
            if (apart > mostStretched) {
                mostStretched = apart;
                mostStretchedLevel = entry.name;
            }
        }
        for (const bool rebuild : {true, false}) {
            const Outcome snap = firstTenth(level, materials, rebuild, entry.name);
            Outcome& sum = rebuild ? snapRebuilt : snapAsSaved;
            sum.moved += snap.moved;
            sum.worstMove = std::max(sum.worstMove, snap.worstMove);
        }
        std::size_t bodies = 0;
        std::size_t levelJoints = 0;
        std::vector<Outcome> outcomes;
        int levelPeakBodies = 0, levelPeakActive = 0;
        std::size_t levelPeakTempBytes = 0;
        for (const Config& c : configs) {
            PhysicsScene scene(level, materials, c.options, settingsFor(entry.name));
            if (c.ball) {
                shootBall(scene);
            }
            outcomes.push_back(run(scene, kRunSeconds));
            bodies = outcomes.back().bodies;
            levelJoints = scene.counts().joints;
            levelPeakBodies = std::max(levelPeakBodies, outcomes.back().peakBodies);
            levelPeakActive = std::max(levelPeakActive, outcomes.back().peakActive);
            levelPeakTempBytes = std::max(levelPeakTempBytes, outcomes.back().tempAllocatorPeakBytes);
        }
        std::printf("%-28s %6zu %6zu", entry.name.c_str(), bodies, levelJoints);
        for (std::size_t i = 0; i < kConfigs; ++i) {
            const Outcome& o = outcomes[i];
            std::printf(" | %5zu/%-5zu", o.moved, o.brokenJoints);
            total[i].moved += o.moved;
            total[i].brokenJoints += o.brokenJoints;
            total[i].worstMove = std::max(total[i].worstMove, o.worstMove);
            total[i].fastest = std::max(total[i].fastest, o.fastest);
            total[i].stepMs += o.stepMs;
            levelsMoved[i] += o.moved > 0;
            levelsBroken[i] += o.brokenJoints > 0;
        }
        // The Vita's world size (docs/PHYSICS.md, "The Vita profile"; docs/VITA.md) is sized from these numbers'
        // peak over all 83 levels and every configuration above (whichever one peaks in, not necessarily together).
        std::printf(" | peak %4d bodies, %4d awake, temp alloc %7zu B", levelPeakBodies, levelPeakActive,
                    levelPeakTempBytes);
        peakBodiesOfAll = std::max(peakBodiesOfAll, levelPeakBodies);
        peakActiveOfAll = std::max(peakActiveOfAll, levelPeakActive);
        peakTempBytesOfAll = std::max(peakTempBytesOfAll, levelPeakTempBytes);
        std::printf("\n");
        std::fflush(stdout);
    }
    const auto row = [&](const char* label, auto cell) {
        std::printf("%-28s %6s %6s", label, "", "");
        for (std::size_t i = 0; i < kConfigs; ++i) {
            std::printf(" | %-11s", cell(i).c_str());
        }
        std::printf("\n");
    };
    std::printf("\n");
    row("bodies moved (all levels)", [&](std::size_t i) { return std::to_string(total[i].moved); });
    row("joints broken (all levels)", [&](std::size_t i) { return std::to_string(total[i].brokenJoints); });
    row("levels with a body moved", [&](std::size_t i) { return std::to_string(levelsMoved[i]); });
    row("levels with a joint broken", [&](std::size_t i) { return std::to_string(levelsBroken[i]); });
    row("furthest any body moved", [&](std::size_t i) { return std::format("{:.2f}", total[i].worstMove); });
    row("fastest body", [&](std::size_t i) { return std::format("{:.0f}", total[i].fastest); });
    row("mean ms per fixed step", [&](std::size_t i) {
        return std::format("{:.3f}", total[i].stepMs / static_cast<double>(std::max<std::size_t>(levels, 1)));
    });
    std::printf("\nfirst 0.1 s awake, all levels: %zu bodies move more than 2 cm (the furthest %.2f) with the joint frames "
                "rebuilt, %zu (%.2f) with the frames as saved\n",
                snapRebuilt.moved, static_cast<double>(snapRebuilt.worstMove), snapAsSaved.moved,
                static_cast<double>(snapAsSaved.worstMove));
    std::printf("joint anchors as the levels saved them: %zu of %zu joints are more than 5 cm apart, %zu more than "
                "1 unit, the most %.2f (%s)\n",
                stretched, joints, stretchedFar, static_cast<double>(mostStretched), mostStretchedLevel.c_str());
    // docs/PHYSICS.md, "The Vita profile": maxBodies is the next power of two at or above 2x the peak body count
    // (at least 512), maxBodyPairs 8x that and maxContactConstraints 4x that; these two are rule-derived from the
    // body peak, not measured on their own. tempAllocatorBytes is sized by a separate rule, from its own measured
    // peak: the next whole MiB at or above 2x that peak, at least 1 MiB. That peak is mostly Jolt's per-step
    // contact-constraint buffer, 488 bytes per maxContactConstraints in force (the thread count plays no part), so
    // pass --physics-profile to match the profile being sized, or this number is for a different constraint count
    // than the one that will use it.
    const std::uint32_t suggestedMaxBodies =
        std::bit_ceil(std::max<std::uint32_t>(512, static_cast<std::uint32_t>(peakBodiesOfAll) * 2));
    constexpr std::size_t kMiB = 1u << 20;
    const std::size_t suggestedTempBytes = std::max<std::size_t>(kMiB, ((peakTempBytesOfAll * 2 + kMiB - 1) / kMiB) * kMiB);
    std::printf("\npeak over all %zu levels and every configuration above: %d bodies, %d awake at once, temp allocator "
                "%zu bytes; the Vita profile's rule (docs/PHYSICS.md) gives maxBodies %u, maxBodyPairs %u, "
                "maxContactConstraints %u, tempAllocatorBytes %zu (%zu MiB)\n",
                levels, peakBodiesOfAll, peakActiveOfAll, peakTempBytesOfAll, suggestedMaxBodies, suggestedMaxBodies * 8,
                suggestedMaxBodies * 4, suggestedTempBytes, suggestedTempBytes / kMiB);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const char* env = std::getenv("MB3_ASSETS");
    if (!env || !*env) {
        std::puts("SKIP: MB3_ASSETS is not set");
        return 77;
    }
    const fs::path assets = env;
    const fs::path materialsJson = assets / "Data" / "Materials" / "materials.json";
    if (!fs::is_directory(assets / "Data" / "Levels") || !fs::is_regular_file(materialsJson)) {
        std::printf("SKIP: converted levels or materials not found under %s\n", assets.string().c_str());
        return 77;
    }
    bool measuring = false;
    int shots = 4;
    float seconds = kRunSeconds;
    const char* only = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--measure") == 0) {
            measuring = true;
        } else if (std::strcmp(argv[i], "--shots") == 0 && i + 1 < argc) {
            shots = std::clamp(std::atoi(argv[++i]), 1, kMostShots);
        } else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            seconds = std::max(1.0f, static_cast<float>(std::atof(argv[++i])));
        } else if (std::strcmp(argv[i], "--velocity-steps") == 0 && i + 1 < argc) {
            gVelocitySteps = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--position-steps") == 0 && i + 1 < argc) {
            gPositionSteps = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--collision-steps") == 0 && i + 1 < argc) {
            gCollisionSteps = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            gThreads = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--physics-profile") == 0 && i + 1 < argc) {
            const std::optional<PhysicsProfile> profile = physicsProfile(argv[++i]);
            if (!profile) {
                std::printf("unknown physics profile \"%s\"; known profiles:", argv[i]);
                for (const std::string_view name : physicsProfileNames()) {
                    std::printf(" %.*s", static_cast<int>(name.size()), name.data());
                }
                std::printf("\n");
                return 2;
            }
            gProfile = *profile;
        } else if (argv[i][0] != '-') {
            only = argv[i];
        } else {
            std::printf("usage: mb_physics_smoke [--shots 1..%d] [--seconds S] [--velocity-steps N] [--position-steps N] "
                        "[--collision-steps N] [--threads N] [--physics-profile name] [level] | --measure [level]\n",
                        kMostShots);
            return 2;
        }
    }
    try {
        const MaterialLibrary materials = MaterialLibrary::load(materialsJson);
        const LevelCatalog catalog(assets / "Data" / "Levels");
        return measuring ? measure(materials, catalog, only) : test(materials, catalog, shots, seconds, only);
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }
}
