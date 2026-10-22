// The special objects of every converted level against the real game data (needs it: skips with exit code 77 when MB3_ASSETS
// is not set), no GPU.
//
//   mb_special_object_smoke [--seconds S] [--log [--everything]] [level]
//        Each level that has dynamite, cannons, firework crates, springs or teleports is played twice. First by the autopilot
//        for S seconds (default 30), as the game would. Then in a second session in which every one of these objects is
//        poked: the first charge of dynamite is hit, a ball is sent at each cannon, crate and spring from the bat's side, and
//        one is put into the trigger box of each teleport source; the session runs for 14 seconds. Asserted: no pose, ball,
//        cannonball or rocket is not finite or far from the board; a blast happens once per charge and the last of them is
//        within 10 s of the poke (a chain ends); every ball that went into a teleport came out and leaves at the ball's speed;
//        every rocket bursts (but one launched in the last 1.2 s); cannonballs do not last; no fixed step is slower than 50 ms on average or 250 ms at the worst. The table
//        has, per level, the objects and what they did. With a level name, --log prints the special events of both sessions (--everything: all
//        events).

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
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace mb;

namespace {

constexpr double kDefaultSeconds = 30;
constexpr double kPokeSeconds = 14;
constexpr double kChainMustEndBy = 10;  // seconds after the poke
constexpr float kFar = 90.0f;           // nothing of the game is farther than this from the board
constexpr double kMostStepMs = 50;        // on average
constexpr double kMostWorstStepMs = 250;  // and at the worst (a chain of blasts in a level of thousands of bodies)

struct Counts {
    int dynamite = 0, cannon = 0, firework = 0, spring = 0, source = 0, exit = 0, metall = 0, other = 0;
    bool any() const { return dynamite + cannon + firework + spring + source + exit + metall + other > 0; }
};

struct Outcome {
    Counts counts;
    SpecialObjects::Stats autoplay, poked;
    std::vector<std::string> problems;
    std::vector<GameEvent> autoplayEvents, events;  // the special ones of the sessions
    double stepMs = 0, worstStepMs = 0;  // of the poked session
};

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool gEverything = false;  // --everything: the log has every event, not only the special ones

bool isSpecial(EventType type) {
    if (gEverything) {
        return true;
    }
    switch (type) {
        case EventType::DynamiteBlast:
        case EventType::CannonFired:
        case EventType::ShotHit:
        case EventType::RocketLaunched:
        case EventType::RocketBurst:
        case EventType::BallEntered:
        case EventType::BallEmerged: return true;
        default: return false;
    }
}

Counts count(const GameSession& session) {
    Counts counts;
    for (const ActorPlan& actor : session.scene().plan().actors) {
        switch (actor.type) {
            case ObjectType::Dynamite: ++counts.dynamite; break;
            case ObjectType::Cannon: ++counts.cannon; break;
            case ObjectType::Firework: ++counts.firework; break;
            case ObjectType::Spring: ++counts.spring; break;
            case ObjectType::TeleportSrc: ++counts.source; break;
            case ObjectType::TeleportDest: ++counts.exit; break;
            case ObjectType::Metall: ++counts.metall; break;
            case ObjectType::Volcano:
            case ObjectType::GeneratorGood:
            case ObjectType::GeneratorBad:
            case ObjectType::Vibro:
            case ObjectType::Flicker: ++counts.other; break;
            default: break;
        }
    }
    return counts;
}

// What every frame of both sessions must satisfy.
struct Watch {
    const char* label;
    std::vector<std::string>* problems;
    void check(const GameSession& session) const {
        const auto fail = [&](const std::string& what) {
            if (problems->size() < 8) {
                problems->push_back(std::format("{}: {} at {:.2f} s", label, what, session.time()));
            }
        };
        for (const BallView& ball : session.balls()) {
            if (!finite(ball.position) || !finite(ball.velocity) || std::fabs(ball.position.x) > kFar || std::fabs(ball.position.z) > kFar) {
                fail(std::format("ball {} at ({:.1f}, {:.1f}, {:.1f})", ball.id, static_cast<double>(ball.position.x),
                                 static_cast<double>(ball.position.y), static_cast<double>(ball.position.z)));
            }
            if (!(ball.appear >= 0 && ball.appear <= 1)) {
                fail(std::format("ball {} appears {}", ball.id, static_cast<double>(ball.appear)));
            }
            const float speed = std::hypot(ball.velocity.x, ball.velocity.z);
            if (!ball.held && !ball.away && std::fabs(speed - session.rules().ballSpeed) > 0.01f * session.rules().ballSpeed) {
                fail(std::format("ball {} speed {:.3f}", ball.id, static_cast<double>(speed)));
            }
        }
        if (session.objects().shots().size() > 64 || session.objects().rockets().size() > 64) {
            fail("more than 64 cannonballs or rockets at once");
        }
        for (const SpecialObjects::Shot& shot : session.objects().shots()) {
            if (!finite(shot.position) || !finite(shot.velocity) || length(shot.position) > 4 * kFar) {
                fail("a cannonball is lost");
            }
        }
        for (const SpecialObjects::Rocket& rocket : session.objects().rockets()) {
            if (!finite(rocket.position) || rocket.position.y > 100) {
                fail("a rocket is lost");
            }
        }
    }
};

SessionOptions optionsFor() {
    SessionOptions options;
    options.hearts = 99;
    return options;
}

void autoplaySession(const Level& level, const MaterialLibrary& materials, double seconds, Outcome& out) {
    GameSession session(level, materials, optionsFor());
    out.counts = count(session);
    Watch watch{"autoplay", &out.problems};
    const PlayResult result = autoplay(session, seconds, [&](const GameSession& s) { watch.check(s); });
    std::ranges::copy_if(result.events, std::back_inserter(out.autoplayEvents), [](const GameEvent& e) { return isSpecial(e.type); });
    out.autoplay = session.objects().stats();
}

// Where a ball is put to touch `actor` from the bat's side, heading up the board.
void sendBallAt(GameSession& session, ActorIndex actor) {
    const Vec3 at = session.centreOf(actor);
    session.addBall({at.x, 0, at.z - 3.5f}, {0, 0, 1});
}

void pokedSession(const Level& level, const MaterialLibrary& materials, Outcome& out) {
    GameSession session(level, materials, optionsFor());
    Watch watch{"poked", &out.problems};
    const PhysicsPlan& plan = session.scene().plan();

    bool charge = false;
    for (ActorIndex i = 0; i < plan.actors.size(); ++i) {
        switch (plan.actors[i].type) {
            case ObjectType::Dynamite:
                if (!charge) {
                    charge = true;
                    session.hit(i, session.centreOf(i));
                }
                break;
            case ObjectType::Cannon:
            case ObjectType::Firework:
            case ObjectType::Spring: sendBallAt(session, i); break;
            case ObjectType::TeleportSrc:
                for (const ZoneBox& zone : plan.zones) {
                    if (zone.actor == i && zone.trigger) {
                        const Vec3 inside = transformPoint({0, 0, 0}, zone.local * plan.actors[i].pose);
                        session.addBall({inside.x, 0, inside.z}, {0, 0, -1});
                        break;
                    }
                }
                break;
            default: break;
        }
    }

    std::set<int> balls;      // that went into a teleport
    std::map<int, int> exits;  // and came out, by ball
    std::set<ActorIndex> blasted;
    double lastBlast = 0, worstStepMs = 0, totalMs = 0;
    int frames = 0;
    for (int i = 0; i < static_cast<int>(kPokeSeconds * 60); ++i) {
        const auto begin = std::chrono::steady_clock::now();
        session.update(1.0 / 60, {});
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        worstStepMs = std::max(worstStepMs, ms);
        totalMs += ms;
        ++frames;
        watch.check(session);
        for (const GameEvent& event : session.takeEvents()) {
            if (!finite(event.position) || !finite(event.velocity)) {
                out.problems.push_back(std::format("poked: {} is not finite", describe(event)));
            }
            if (!isSpecial(event.type)) {
                continue;
            }
            out.events.push_back(event);
            if (event.type == EventType::DynamiteBlast) {
                if (event.speed != 5.0f && event.speed != 3.0f) {  // the big charge and the small
                    out.problems.push_back(std::format("poked: a blast of radius {}", static_cast<double>(event.speed)));
                }
                if (!blasted.insert(event.actor).second) {
                    out.problems.push_back(std::format("poked: charge {} went off twice", event.actor));
                }
                lastBlast = event.time;
            } else if (event.type == EventType::BallEntered) {
                balls.insert(event.ball);
            } else if (event.type == EventType::BallEmerged) {
                ++exits[event.ball];
            }
        }
    }
    out.poked = session.objects().stats();
    out.stepMs = totalMs / std::max(frames, 1);
    out.worstStepMs = worstStepMs;
    if (worstStepMs > kMostWorstStepMs) {
        out.problems.push_back(std::format("poked: a step took {:.1f} ms", worstStepMs));
    }
    if (out.stepMs > kMostStepMs) {
        out.problems.push_back(std::format("poked: a step took {:.1f} ms on average", out.stepMs));
    }
    if (lastBlast > kChainMustEndBy) {
        out.problems.push_back(std::format("poked: the chain was still going at {:.1f} s", lastBlast));
    }
    if (static_cast<int>(blasted.size()) > out.counts.dynamite) {
        out.problems.push_back("poked: more blasts than charges");
    }
    if (static_cast<int>(balls.size()) > 0 && exits.size() != balls.size()) {
        out.problems.push_back(std::format("poked: {} balls went into a teleport and {} came out", balls.size(), exits.size()));
    }
    // (a rocket launched in the last 1.2 s is still going up: every other has burst)
    if (out.poked.rocketsLaunched - out.poked.rocketsBurst != static_cast<int>(session.objects().rockets().size())) {
        out.problems.push_back("poked: a rocket did not burst");
    }
    if (!session.objects().shots().empty() && session.time() > 0) {
        // (a cannon set off by debris in the last moments may have a ball in the air)
        const double newest = std::ranges::max(session.objects().shots(), {}, &SpecialObjects::Shot::firedAt).firedAt;
        if (session.time() - newest > session.rules().objects.shotLife + 0.5) {
            out.problems.push_back("poked: a cannonball outlived its time");
        }
    }
}

int run(const MaterialLibrary& materials, const LevelCatalog& catalog, double seconds, bool log, const char* only) {
    std::printf("%-26s | %3s %3s %3s %3s %3s %3s %3s | %5s %4s %4s %4s %4s %4s | %5s %4s %4s %4s %4s %4s | %s %s\n", "level", "dyn",
                "can", "fw", "spr", "src", "dst", "met", "blast", "shot", "land", "rkt", "brst", "tele", "blast", "shot", "land",
                "rkt", "brst", "tele", "ms/step", "worst");
    std::printf("%-26s | %31s | %-31s | %-31s |\n", "", "objects", "autoplay", "poked");
    int failed = 0, levels = 0;
    SpecialObjects::Stats total;
    for (const LevelEntry& entry : catalog.levels()) {
        if (only && entry.name.find(only) == std::string::npos) {
            continue;
        }
        const Level level = loadLevelJson(entry.path);
        Outcome out;
        try {
            autoplaySession(level, materials, seconds, out);
            if (!out.counts.any()) {
                continue;
            }
            pokedSession(level, materials, out);
        } catch (const std::exception& e) {
            out.problems.push_back(std::format("exception: {}", e.what()));
        }
        ++levels;
        const Counts& c = out.counts;
        const SpecialObjects::Stats &a = out.autoplay, &p = out.poked;
        std::printf("%-26s | %3d %3d %3d %3d %3d %3d %3d | %5d %4d %4d %4d %4d %4d | %5d %4d %4d %4d %4d %4d | %.3f %.1f\n",
                    entry.name.c_str(), c.dynamite, c.cannon, c.firework, c.spring, c.source, c.exit, c.metall, a.blasts,
                    a.shotsFired, a.shotsLanded, a.rocketsLaunched, a.rocketsBurst, a.teleports, p.blasts, p.shotsFired,
                    p.shotsLanded, p.rocketsLaunched, p.rocketsBurst, p.teleports, out.stepMs, out.worstStepMs);
        std::fflush(stdout);
        total.blasts += a.blasts + p.blasts;
        total.shotsFired += a.shotsFired + p.shotsFired;
        total.shotsLanded += a.shotsLanded + p.shotsLanded;
        total.rocketsLaunched += a.rocketsLaunched + p.rocketsLaunched;
        total.rocketsBurst += a.rocketsBurst + p.rocketsBurst;
        total.teleports += a.teleports + p.teleports;
        for (const std::string& what : out.problems) {
            std::printf("FAIL %s: %s\n", entry.name.c_str(), what.c_str());
            ++failed;
        }
        if (log && only) {
            std::printf("autoplay:\n");
            for (const GameEvent& event : out.autoplayEvents) {
                std::printf("%s\n", describe(event).c_str());
            }
            std::printf("poked:\n");
            for (const GameEvent& event : out.events) {
                std::printf("%s\n", describe(event).c_str());
            }
        }
    }
    std::printf("\n%d levels with special objects: %d blasts, %d cannonballs (%d landed), %d rockets (%d burst), %d balls "
                "through a teleport\n",
                levels, total.blasts, total.shotsFired, total.shotsLanded, total.rocketsLaunched, total.rocketsBurst,
                total.teleports);
    if (!only && (total.blasts == 0 || total.shotsFired == 0 || total.rocketsLaunched == 0 || total.teleports == 0)) {
        std::printf("FAIL: some kind of special object never did anything in any level\n");
        ++failed;
    }
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
    bool log = false;
    const char* only = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            seconds = std::max(1.0, std::atof(argv[++i]));
        } else if (std::strcmp(argv[i], "--log") == 0) {
            log = true;
        } else if (std::strcmp(argv[i], "--everything") == 0) {
            gEverything = true;
        } else if (argv[i][0] != '-') {
            only = argv[i];
        } else {
            std::printf("usage: mb_special_object_smoke [--seconds S] [--log [--everything]] [level]\n");
            return 2;
        }
    }
    try {
        const MaterialLibrary materials = MaterialLibrary::load(materialsJson);
        const LevelCatalog catalog(assets / "Data" / "Levels");
        return run(materials, catalog, seconds, log, only);
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }
}
