// Plays every converted level with the autopilot against the real game data (needs it: skips with exit code 77 when
// MB3_ASSETS is not set), no GPU.
//
//   mb_gameplay_smoke [--seconds S] [--rule never|type|dyn] [--strict] [--log] [--left] [level]
//        each level is played for S seconds of game time (default 60) by the autopilot (the bat follows the ball that
//        comes down first), with hearts to spare so that it plays on after a lost ball, and the level ending set to
//        "never" (--rule type or dyn to end the level as the game would, which also shows the targets let go of when the
//        level stands still) so that both readings of "destroy every target" (TargetRule) can be followed in one run. The table
//        has, per level: the targets under each reading at the start; bricks destroyed, points, balls lost, bat hits,
//        brick hits and balls freed from a pin; the targets left under each reading, and when each reading's last
//        target fell (a dash: not within S). Sanity is asserted, not the result: no pose may be not finite, no body
//        may fly faster than a sane simulation, a ball's speed must be the rules' speed and its y the plane's after
//        every frame, a ball may only leave past the bat, a ball in flight must move at least 0.3 units in 2 s (the
//        speed the session reports is the one it sets, so a ball frozen by the solver shows only here), and none may
//        be pinned against the bat (the session frees a ball that is pinned elsewhere, in a pocket of the board, and
//        the table counts those). With a level name, --log prints its event log.

#include "mb/game/Autopilot.h"
#include "mb/game/Gameplay.h"
#include "mb/game/LevelCatalog.h"
#include "mb/game/LevelLoader.h"
#include "mb/game/MaterialLibrary.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace mb;

namespace {

constexpr double kDefaultSeconds = 60;
constexpr float kMaxSpeed = 200.0f;  // no body of a sane simulation is faster than this (units per second)
constexpr float kSpeedTolerance = 0.01f;
constexpr float kPlaneTolerance = 1e-3f;
constexpr float kHigh = 2.5f, kReachBeyond = 1.5f;  // see play(): where a ball cannot reach
constexpr float kMoves = 0.3f;                       // a ball in flight is at least this far from where it was ...
constexpr std::uint64_t kMovesWithin = 120;          // ... two seconds (frames) ago

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

struct Outcome {
    SessionStats stats;
    std::int64_t score = 0;
    int hearts = 0;
    int letGo = 0;                   // targets let go of because the level stood still
    int targetsAtStart[2] = {0, 0};  // DynamicBricks, BrickType
    int targetsLeft[2] = {0, 0};
    double finishedAt[2] = {-1, -1};
    double lastProgress = 0;  // when the last brick went (destroyed or fallen): a level far from done that has stood still a long time is stuck
    float fastest = 0;
    float furthest = 0;  // the ball's greatest |x|
    float farthest = 0;  // and its greatest z
    double stepMs = 0;
    std::vector<GameEvent> events;
    std::vector<std::string> problems;
    std::vector<std::string> standing;  // the targets left, one line each
    int standingHigh = 0, standingOutside = 0, standingOnField = 0;  // where, see play()
};

// Where a ball was when it last moved, and when (the frame).
struct Anchor {
    Vec3 at;
    std::uint64_t frame = 0;
};

const TargetRule kRules[2] = {TargetRule::DynamicBricks, TargetRule::BrickType};

const char* motionName(MotionType motion) {
    return motion == MotionType::Dynamic ? "dynamic" : (motion == MotionType::Kinematic ? "kinem." : "static");
}

TargetRule gRule = TargetRule::Never;
bool gStrict = false;  // every target counts, whatever the ball can reach, and the level never stands still

Outcome play(const Level& level, const MaterialLibrary& materials, double seconds) {
    SessionOptions options;
    options.levelEnd = gRule;
    if (gStrict) {
        options.ignoreUnreachable = false;
        options.rules.stallSeconds = 1e9;
    }
    options.hearts = 99;
    GameSession session(level, materials, options);

    Outcome out;
    for (int i = 0; i < 2; ++i) {
        out.targetsAtStart[i] = session.targetsLeft(kRules[i]);
    }
    const GameplayRules& rules = session.rules();
    const PhysicsScene& scene = session.scene();
    std::uint64_t frame = 0;
    std::map<int, Anchor> anchors;  // by ball id
    int freed = 0;
    const auto problem = [&](const std::string& what) {
        if (out.problems.size() < 5) {
            out.problems.push_back(std::format("{} at {:.2f} s", what, session.time()));
        }
    };

    const auto begin = std::chrono::steady_clock::now();
    const PlayResult result = autoplay(session, seconds, [&](const GameSession& s) {
        ++frame;
        for (int i = 0; i < 2; ++i) {
            if (out.finishedAt[i] < 0 && s.targetsLeft(kRules[i]) == 0 && out.targetsAtStart[i] > 0) {
                out.finishedAt[i] = s.time();
            }
        }
        if (s.stats().ballsFreed > freed) {
            freed = s.stats().ballsFreed;
            for (const BallView& ball : s.balls()) {
                const Vec3 bat = s.bat().position;
                if (!ball.held && std::hypot(ball.position.x - bat.x, ball.position.z - bat.z) <
                                      rules.batRadius + kBallRadius + kReachBeyond) {
                    problem(std::format("ball {} was pinned against the bat at ({:.2f}, {:.2f})", ball.id,
                                        static_cast<double>(ball.position.x), static_cast<double>(ball.position.z)));
                }
            }
        }
        for (const BallView& ball : s.balls()) {
            if (ball.held) {
                anchors.erase(ball.id);
                continue;
            }
            Anchor& anchor = anchors.try_emplace(ball.id, Anchor{ball.position, frame}).first->second;
            if (std::hypot(ball.position.x - anchor.at.x, ball.position.z - anchor.at.z) >= kMoves) {
                anchor = {ball.position, frame};
            } else if (frame - anchor.frame >= kMovesWithin) {
                problem(std::format("ball {} has not moved {} units in {} frames, at ({:.2f}, {:.2f})", ball.id,
                                    static_cast<double>(kMoves), kMovesWithin, static_cast<double>(ball.position.x),
                                    static_cast<double>(ball.position.z)));
                anchor.frame = frame;
            }
            const float speed = std::hypot(ball.velocity.x, ball.velocity.z);
            if (!std::isfinite(speed) || std::fabs(speed - rules.ballSpeed) > kSpeedTolerance * rules.ballSpeed ||
                ball.velocity.y != 0) {
                problem(std::format("ball {} speed {:.3f}", ball.id, static_cast<double>(speed)));
            }
            if (!(std::fabs(ball.position.y - rules.ballPlaneY) <= kPlaneTolerance)) {
                problem(std::format("ball {} at y {:.4f}", ball.id, static_cast<double>(ball.position.y)));
            }
            out.furthest = std::max(out.furthest, std::fabs(ball.position.x));
            out.farthest = std::max(out.farthest, ball.position.z);
        }
        if (frame % 6 == 0) {  // a tenth of a second is fine-grained enough to catch a body that flies away
            for (ActorIndex i = 0; i < scene.plan().actors.size(); ++i) {
                if (scene.body(i) == 0 || scene.plan().actors[i].motion == MotionType::Static) {
                    continue;
                }
                if (!finite(scene.actorPose(i))) {
                    problem(std::format("actor {} has a pose that is not finite", i));
                    continue;
                }
                const float speed = length(scene.world().linearVelocity(scene.body(i)));
                out.fastest = std::max(out.fastest, std::isfinite(speed) ? speed : 1e9f);
            }
        }
    });
    out.stepMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() /
                 static_cast<double>(std::max<std::uint64_t>(frame, 1));

    for (const GameEvent& event : result.events) {
        if (event.type == EventType::BrickDestroyed || event.type == EventType::ActorFell) {
            out.lastProgress = event.time;
        }
        // A ball may only leave past the bat.
        if (event.type == EventType::BallLost &&
            !(event.position.z < rules.ballLostZ && event.position.z > rules.ballLostZ - 1.0f &&
              std::fabs(event.position.x) < rules.escapeX)) {
            problem(std::format("ball {} left the board at ({:.1f}, {:.1f})", event.ball,
                                static_cast<double>(event.position.x), static_cast<double>(event.position.z)));
        }
    }
    if (out.fastest > kMaxSpeed) {
        out.problems.push_back(std::format("a body reached {:.0f} units per second", static_cast<double>(out.fastest)));
    }
    out.stats = session.stats();
    out.letGo = session.stats().targetsLetGo;
    out.score = session.score();
    out.hearts = session.hearts();
    for (int i = 0; i < 2; ++i) {
        out.targetsLeft[i] = session.targetsLeft(kRules[i]);
    }
    out.events = result.events;
    const std::vector<ActorRef> refs = flattenActors(level);
    for (ActorIndex i = 0; i < scene.plan().actors.size(); ++i) {
        const bool dyn = session.isTarget(i, kRules[0]), type = session.isTarget(i, kRules[1]);
        if (dyn || type) {
            const Vec3 at = session.centreOf(i);
            // A ball moves on y = 0.6 between the walls: what is higher than it can reach, or farther out than any ball
            // went (the boards' walls are not all in the same place), cannot be touched until it falls.
            const bool outside = std::fabs(at.x) > out.furthest + kReachBeyond || at.z > out.farthest + kReachBeyond;
            (at.y > kHigh ? out.standingHigh : (outside ? out.standingOutside : out.standingOnField))++;
            out.standing.push_back(std::format(
                "  actor {:4} {:10} {:16} {}{} {:7} at ({:6.1f} {:5.1f} {:6.1f}){}", i,
                objectTypeName(scene.plan().actors[i].type), refs[i].owner ? refs[i].owner->material : std::string(),
                dyn ? "dyn " : "", type ? "type" : "", motionName(scene.plan().actors[i].motion),
                static_cast<double>(at.x), static_cast<double>(at.y), static_cast<double>(at.z),
                scene.world().isAwake(scene.body(i)) ? " awake" : ""));
        }
    }
    return out;
}

std::string finish(double at) {
    return at < 0 ? "-" : std::format("{:.1f}", at);
}

int run(const MaterialLibrary& materials, const LevelCatalog& catalog, double seconds, bool log, bool left,
        const char* only) {
    std::printf("%-26s | %14s | %5s %7s %4s %5s %6s %5s | %9s | %-11s | %7s | %s\n", "level", "targets", "dead", "points",
                "lost", "bat", "hits", "freed", "left", "done at (s)", "last (s)", "ms/step");
    std::printf("%-26s | %4s %4s %4s | %5s %7s %4s %5s %6s %5s | %4s %4s | %5s %5s | %7s |\n", "", "dyn", "type", "out", "",
                "", "", "", "", "", "dyn", "type", "dyn", "type", "");
    int failed = 0, levels = 0, finishedBy[2] = {0, 0}, withTargets[2] = {0, 0};
    int emptyLevels = 0, levelsLetGo = 0, standingHigh = 0, standingOutside = 0, standingOnField = 0, freed = 0, levelsWithFreed = 0;
    double share[2] = {0, 0};  // summed over the levels: the part of the targets destroyed
    double stepMs = 0, worstStepMs = 0, slowest = 0;
    std::string worstLevel;
    for (const LevelEntry& entry : catalog.levels()) {
        if (only && entry.name.find(only) == std::string::npos) {
            continue;
        }
        ++levels;
        const Level level = loadLevelJson(entry.path);
        const Outcome out = play(level, materials, seconds);
        std::printf("%-26s | %4d %4d %4d | %5d %7lld %4d %5d %6d %5d | %4d %4d | %5s %5s | %7.1f | %.3f\n", entry.name.c_str(),
                    out.targetsAtStart[0], out.targetsAtStart[1], out.stats.targetsOutOfPlay, out.stats.bricksDestroyed,
                    static_cast<long long>(out.score), out.stats.ballsLost, out.stats.batHits, out.stats.bricksHit,
                    out.stats.ballsFreed, out.targetsLeft[0], out.targetsLeft[1], finish(out.finishedAt[0]).c_str(),
                    finish(out.finishedAt[1]).c_str(), out.lastProgress, out.stepMs);
        std::fflush(stdout);
        for (int i = 0; i < 2; ++i) {
            withTargets[i] += out.targetsAtStart[i] > 0;
            finishedBy[i] += out.finishedAt[i] >= 0;
            if (out.targetsAtStart[i] > 0) {
                share[i] += 1.0 - static_cast<double>(out.targetsLeft[i]) / out.targetsAtStart[i];
            }
        }
        emptyLevels += out.targetsAtStart[0] == 0 || out.targetsAtStart[1] == 0;
        levelsLetGo += out.letGo > 0;
        if (out.letGo > 0) {
            std::printf("  (%d targets were let go: the level stood still from %.1f s)\n", out.letGo, out.lastProgress);
        }
        freed += out.stats.ballsFreed;
        levelsWithFreed += out.stats.ballsFreed > 0;
        standingHigh += out.standingHigh;
        standingOutside += out.standingOutside;
        standingOnField += out.standingOnField;
        stepMs += out.stepMs;
        if (out.stepMs > worstStepMs) {
            worstStepMs = out.stepMs;
            worstLevel = entry.name;
        }
        slowest = std::max(slowest, static_cast<double>(out.fastest));
        for (const std::string& what : out.problems) {
            std::printf("FAIL %s: %s\n", entry.name.c_str(), what.c_str());
            ++failed;
        }
        if (left) {
            for (const std::string& line : out.standing) {
                std::printf("%s\n", line.c_str());
            }
        }
        if (log && only) {
            for (const GameEvent& event : out.events) {
                std::printf("%s\n", describe(event).c_str());
            }
        }
    }
    std::printf("\n%d levels played for %.0f s of game time each by the autopilot\n", levels, seconds);
    std::printf("finished within the time, targets being every dynamic brick and tree (H-dyn): %d of %d levels\n",
                finishedBy[0], withTargets[0]);
    std::printf("finished within the time, targets being every brick of material type 0 (H-type): %d of %d levels\n",
                finishedBy[1], withTargets[1]);
    std::printf("on average %.0f%% of the targets were destroyed under H-dyn and %.0f%% under H-type\n",
                100 * share[0] / std::max(withTargets[0], 1), 100 * share[1] / std::max(withTargets[1], 1));
    std::printf("targets still standing at the end under either reading: %d on the field, %d higher than %.1f (out of "
                "reach until they fall), %d beyond where any ball went (out of reach)\n",
                standingOnField, standingHigh, static_cast<double>(kHigh), standingOutside);
    std::printf("balls freed from a pin: %d, in %d levels\n", freed, levelsWithFreed);
    if (gRule != TargetRule::Never) {
        std::printf("levels that ended by letting go of the targets nothing could touch: %d\n", levelsLetGo);
    }
    if (emptyLevels > 0) {
        std::printf("%d levels have no target under one of the readings\n", emptyLevels);
    }
    std::printf("a fixed step took %.3f ms on average, %.3f ms in the slowest level (%s); the fastest body moved at "
                "%.0f units per second\n",
                stepMs / std::max(levels, 1), worstStepMs, worstLevel.c_str(), slowest);
    return failed == 0 && levels > 0 ? 0 : 1;
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
    double seconds = kDefaultSeconds;
    bool log = false, left = false;
    const char* only = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            seconds = std::max(1.0, std::atof(argv[++i]));
        } else if (std::strcmp(argv[i], "--log") == 0) {
            log = true;
        } else if (std::strcmp(argv[i], "--left") == 0) {
            left = true;
        } else if (std::strcmp(argv[i], "--strict") == 0) {
            gStrict = true;
        } else if (std::strcmp(argv[i], "--rule") == 0 && i + 1 < argc) {
            const std::string rule = argv[++i];
            gRule = rule == "type" ? TargetRule::BrickType : (rule == "dyn" ? TargetRule::DynamicBricks : TargetRule::Never);
        } else if (argv[i][0] != '-') {
            only = argv[i];
        } else {
            std::printf("usage: mb_gameplay_smoke [--seconds S] [--rule never|type|dyn] [--strict] [--log] [--left] [level]\n");
            return 2;
        }
    }
    try {
        const MaterialLibrary materials = MaterialLibrary::load(materialsJson);
        const LevelCatalog catalog(assets / "Data" / "Levels");
        return run(materials, catalog, seconds, log, left, only);
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }
}
