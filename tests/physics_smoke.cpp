// Runs every converted level in the physics world (needs the game data: skips with exit code 77 when
// MB3_ASSETS is not set), no GPU.
//
//   mb_physics_smoke             the test: each level stands still for 10 s on its own, then a test ball is
//                                shot into it for 10 s and nothing may blow up (a failing level is named; the
//                                levels the ball did not reach are listed)
//   mb_physics_smoke --measure   prints how the ways of starting from the saved state compare, see measure()

#include "mb/game/LevelCatalog.h"
#include "mb/game/LevelLoader.h"
#include "mb/game/MaterialLibrary.h"
#include "mb/game/PhysicsScene.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <optional>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace mb;

namespace {

constexpr float kStep = 1.0f / 60.0f;
constexpr float kMoveLimit = 0.02f;       // a body that moves more than 2 cm has not stayed where it was designed
constexpr float kRunSeconds = 10.0f;
constexpr float kSupportGap = 0.15f;   // see PhysicsScene::supported()
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

// Moves the world on by `seconds`, tracking the bodies the plan makes dynamic or kinematic.
Outcome run(PhysicsScene& scene, float seconds) {
    Outcome out;
    const PhysicsPlan& plan = scene.plan();
    std::vector<ActorIndex> tracked;
    std::vector<Mat4> start;
    for (ActorIndex i = 0; i < plan.actors.size(); ++i) {
        if (scene.body(i) != 0 && plan.actors[i].motion != MotionType::Static) {
            tracked.push_back(i);
            start.push_back(scene.actorPose(i));
        }
    }
    out.bodies = tracked.size();
    out.joints = scene.counts().joints;

    const int steps = static_cast<int>(std::lround(seconds / kStep));
    const auto begin = std::chrono::steady_clock::now();
    for (int s = 0; s < steps; ++s) {
        scene.step(kStep);
        if (s % 6 == 5) {  // a tenth of a second is fine-grained enough to catch a body that flies away
            for (const ActorIndex i : tracked) {
                const float speed = length(scene.world().linearVelocity(scene.body(i)));
                out.fastest = std::max(out.fastest, std::isfinite(speed) ? speed : 1e9f);
            }
        }
    }
    out.brokenJoints = scene.brokenJoints().size();
    out.stepMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() / steps;

    for (std::size_t k = 0; k < tracked.size(); ++k) {
        const Mat4 pose = scene.actorPose(tracked[k]);
        if (!finite(pose)) {
            out.nonFinite = true;
            continue;
        }
        const float moved = length(pose.translationPart() - start[k].translationPart());
        out.worstMove = std::max(out.worstMove, moved);
        out.moved += moved > kMoveLimit;
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

// The test: each level, built the way the game builds it, stands still for kRunSeconds with nothing near it,
// then gets a test ball for kRunSeconds more.
int test(const MaterialLibrary& materials, const LevelCatalog& catalog) {
    int failed = 0;
    const auto fail = [&](const std::string& level, const std::string& what) {
        std::printf("FAIL %s: %s\n", level.c_str(), what.c_str());
        ++failed;
    };
    std::vector<std::string> untouched;
    std::size_t levels = 0, joints = 0, hits = 0, broken = 0, floating = 0, restingTotal = 0, sunk = 0;
    PhysicsScene::Counts total;
    for (const LevelEntry& entry : catalog.levels()) {
        ++levels;
        const Level level = loadLevelJson(entry.path);
        {
            PhysicsScene scene(level, materials);
            const PhysicsPlan& plan = scene.plan();
            const PhysicsScene::Counts& counts = scene.counts();
            total.dynamicBodies += counts.dynamicBodies;
            total.kinematicBodies += counts.kinematicBodies;
            total.staticBodies += counts.staticBodies;
            total.withoutBody += counts.withoutBody;
            total.clothJoints += counts.clothJoints;
            joints += counts.joints;
            if (counts.dynamicBodies == 0) {
                fail(entry.name, "no dynamic body");
            }
            if (plan.unresolvedJoints + plan.unsupportedJoints + plan.jointsWithoutBody + counts.jointsRefused +
                    plan.unsupportedShapes > 0) {
                fail(entry.name, std::format("{} joints unresolved, {} unsupported, {} without a body, {} refused, {} "
                                             "shapes unsupported",
                                             plan.unresolvedJoints, plan.unsupportedJoints, plan.jointsWithoutBody,
                                             counts.jointsRefused, plan.unsupportedShapes));
            }
            const Outcome alone = run(scene, kRunSeconds);
            if (alone.moved > 0 || alone.brokenJoints > 0 || alone.nonFinite) {
                fail(entry.name, std::format("on its own {} bodies moved more than 2 cm (up to {:.3f}), {} joints broke",
                                             alone.moved, alone.worstMove, alone.brokenJoints));
            }
        }
        PhysicsScene scene(level, materials);
        // Dynamic bodies that stand on something now (a stack, a plank on a post, a brick on the floor).
        std::vector<ActorIndex> resting;
        for (ActorIndex i = 0; i < scene.plan().actors.size(); ++i) {
            if (scene.body(i) != 0 && scene.plan().actors[i].motion == MotionType::Dynamic && scene.supported(i)) {
                resting.push_back(i);
            }
        }
        shootBall(scene);
        const Outcome shot = run(scene, kRunSeconds);
        broken += shot.brokenJoints;
        if (shot.nonFinite) {
            fail(entry.name, "a pose is not finite after the ball");
        }
        if (shot.fastest > kMaxSpeed) {
            fail(entry.name, std::format("a body reached {:.0f} units per second", shot.fastest));
        }

        // Nothing may have gone through the floor: a body over the board whose lowest point is more than half a unit
        // under the floor's plane, which it was not at the start.
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
                std::fabs(p.x) < 20.0f && std::fabs(p.z) < 20.0f) {
                ++sunk;
                fail(entry.name, std::format("actor {} ({}) lies under the board at ({:.1f} {:.1f} {:.1f})", i,
                                             objectTypeName(scene.plan().actors[i].type), static_cast<double>(p.x),
                                             static_cast<double>(p.y), static_cast<double>(p.z)));
            }
        }

        // After the hit nothing that stood on something may hang in the air, asleep, unless a joint still holds it. A
        // body asleep with nothing under it by a ray is woken and given half a second: if it is held up it stays, and
        // if its support left without waking it, it falls.
        std::size_t hanging = 0;
        ActorIndex firstHanging = kNoActor;
        for (const ActorIndex i : resting) {
            const bool jointed = std::ranges::any_of(scene.plan().joints, [&](const JointPlan& j) {
                return j.actorA == i || j.actorB == i;
            });
            if (jointed || scene.world().isAwake(scene.body(i)) || scene.supported(i)) {
                continue;
            }
            const Vec3 before = scene.actorPose(i).translationPart();
            scene.world().wake(scene.body(i));
            run(scene, 0.5f);
            // (A body the level saved a few centimetres above what it rests on settles by that much when woken.)
            if (before.y - scene.actorPose(i).translationPart().y > 2 * kSupportGap) {
                ++hanging;
                if (firstHanging == kNoActor) {
                    firstHanging = i;
                }
            }
        }
        floating += hanging;
        restingTotal += resting.size();
        if (hanging > 0) {
            const ActorPlan& first = scene.plan().actors[firstHanging];
            fail(entry.name, std::format("{} bodies that stood on something hang in the air after the ball, the first "
                                         "is actor {} ({}, at y {:.2f})",
                                         hanging, firstHanging, objectTypeName(first.type),
                                         static_cast<double>(first.pose.translationPart().y)));
        }

        const bool touchedSomething = std::ranges::any_of(scene.touchedByBalls(), [&](ActorIndex actor) {
            return scene.plan().actors[actor].type != ObjectType::Board;
        });
        hits += touchedSomething;
        if (!touchedSomething) {
            untouched.push_back(entry.name);
        }
    }
    std::printf("physics: %zu levels, %zu dynamic, %zu kinematic and %zu static bodies (%zu actors without one), %zu "
                "joints (%zu on cloth left out)\n",
                levels, total.dynamicBodies, total.kinematicBodies, total.staticBodies, total.withoutBody, joints,
                total.clothJoints);
    std::printf("the test ball touched something besides the board in %zu of them and broke %zu joints; of %zu bodies "
                "standing on something, %zu hang in the air at the end, %zu lie under the board\n",
                hits, broken, restingTotal, floating, sunk);
    if (!untouched.empty()) {
        std::printf("the ball touched nothing in:");
        for (const std::string& name : untouched) {
            std::printf(" %s", name.c_str());
        }
        std::printf("\n");
    }
    // "Most": the ball is shot at the middle of the movable bodies, which can be an empty patch of floor.
    if (hits * 4 < levels * 3) {
        fail("all", std::format("the ball touched something in only {} of {} levels", hits, levels));
    }
    return failed == 0 && levels > 0 ? 0 : 1;
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
Outcome firstTenth(const Level& level, const MaterialLibrary& materials, bool rebuildJointFrames) {
    PhysicsOptions options;
    options.start = StartState::Still;
    options.rebuildJointFrames = rebuildJointFrames;
    PhysicsScene scene(level, materials, options);
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
            const Outcome snap = firstTenth(level, materials, rebuild);
            Outcome& total = rebuild ? snapRebuilt : snapAsSaved;
            total.moved += snap.moved;
            total.worstMove = std::max(total.worstMove, snap.worstMove);
        }
        std::size_t bodies = 0;
        std::size_t levelJoints = 0;
        std::vector<Outcome> outcomes;
        for (const Config& c : configs) {
            PhysicsScene scene(level, materials, c.options);
            if (c.ball) {
                shootBall(scene);
            }
            outcomes.push_back(run(scene, kRunSeconds));
            bodies = outcomes.back().bodies;
            levelJoints = scene.counts().joints;
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
    try {
        const MaterialLibrary materials = MaterialLibrary::load(materialsJson);
        const LevelCatalog catalog(assets / "Data" / "Levels");
        if (argc > 1 && std::strcmp(argv[1], "--measure") == 0) {
            return measure(materials, catalog, argc > 2 ? argv[2] : nullptr);
        }
        return test(materials, catalog);
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }
}
