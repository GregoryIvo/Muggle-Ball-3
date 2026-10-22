// The weapons and the strikes in every converted level against the real game data (needs it: skips with exit code 77 when MB3_ASSETS
// is not set), no GPU.
//
//   mb_weapon_smoke [--seconds S] [--everything] [level]
//        Each level is played for S seconds (default 24) by the autopilot, with hearts to spare, after the prizes of all four weapons (each
//        caught twice, so that the shots outlast the run), the Thunderbolt and the Meteorite Rain were given at once. The click is held
//        down: a shot leaves every shot interval while the balls are in play, from wherever the bat is, whether or not anything stands
//        ahead of it. Asserted: no projectile, beam or event is not finite or off the board; shots are counted (what was given, by the
//        prizes forced and the ones caught, is the ShotFired events plus the shots left and the ones an ended effect gave up; each
//        ShotFired says the shots left; the right projectiles for each weapon: two bullets a Machine Gun shot, one kernel, one rocket,
//        none for a laser); every projectile that was made ends in a hit, is reported gone or is still in the air, and none stays in
//        the air longer than its life; the strikes that were due are all thrown (the later bolts within 12 s of the catch) and are
//        not left pending; a bolt strikes an actor; a beam ends on the board and no
//        farther than its range; the balls keep their speed and plane; no fixed step is slower than 50 ms on average or 250 ms at the
//        worst. The table has, per level, what the weapons did. With a level name, the weapons' events are printed (--everything: all
//        events).

#include "mb/game/Autopilot.h"
#include "mb/game/Campaign.h"
#include "mb/game/Gameplay.h"
#include "mb/game/LevelCatalog.h"
#include "mb/game/LevelLoader.h"
#include "mb/game/MaterialLibrary.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace mb;

namespace {

constexpr double kDefaultSeconds = 24;
constexpr float kFar = 90.0f;             // nothing of the game is farther than this from the board
constexpr float kMostHeight = 120.0f;     // and nothing is higher than this (a bolt starts about 30 up)
constexpr std::size_t kMostInTheAir = 128;
constexpr double kMostStepMs = 50;        // on average
constexpr double kMostWorstStepMs = 250;  // and at the worst
constexpr int kCatches = 2;               // of each weapon: 24 gun shots, 12 cannon, 14 bazooka and 10 laser

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool isWeaponEvent(EventType type) {
    switch (type) {
        case EventType::ShotFired:
        case EventType::ProjectileHit:
        case EventType::ProjectileExploded:
        case EventType::ProjectileGone:
        case EventType::LaserBeam:
        case EventType::BoltStruck:
        case EventType::MeteorFalling:
        case EventType::MeteorImpact:
        case EventType::StrikeGranted: return true;
        default: return false;
    }
}

struct Outcome {
    std::array<int, kWeaponTypeCount> given{}, fired{};  // shots, by WeaponType
    Weapons::Stats stats;
    int bolts = 0, meteors = 0;  // struck
    int boltsDue = 0, meteorsDue = 0;
    int destroyed = 0;
    double stepMs = 0, worstStepMs = 0;
    std::vector<std::string> problems;
    std::vector<GameEvent> events;
};

Outcome play(const Level& level, const MaterialLibrary& materials, double seconds, int campaignLevel, bool everything) {
    SessionOptions options;
    options.hearts = 99;
    options.campaignLevel = campaignLevel;
    // Each level has prizes of its own: the default seed would drop the same first dozen in every level (see gameplay_smoke.cpp).
    options.prizeSeed = 0x9E3779B97F4A7C15ull + static_cast<std::uint64_t>(campaignLevel);
    GameSession session(level, materials, options);
    Outcome out;
    const auto problem = [&](const std::string& what) {
        if (out.problems.size() < 8) {
            out.problems.push_back(std::format("{} at {:.2f} s", what, session.time()));
        }
    };
    const WeaponRules& rules = session.rules().weapons;
    const std::size_t actors = session.scene().plan().actors.size();

    // Everything at once, at the first frame the balls are in play.
    bool given = false;
    double grantedAt = 0;  // when the last strike prize was caught
    const Autopilot pilot;
    int frames = 0;
    double totalMs = 0;
    const auto end = session.time() + seconds;
    while (session.time() + 1.0 / 120 < end && session.phase() != SessionPhase::Complete && session.phase() != SessionPhase::GameOver) {
        GameInput input = pilot.decide(session);
        if (!given && session.phase() == SessionPhase::Playing) {
            given = true;
            grantedAt = session.time();
            for (int i = 0; i < kCatches; ++i) {
                for (const PrizeKind kind : {PrizeKind::Gun, PrizeKind::Cannon, PrizeKind::Bazooka, PrizeKind::Laser}) {
                    session.givePrize(kind);
                    out.given[static_cast<std::size_t>(*weaponOf(kind))] += prizeInfo(kind).count;
                }
            }
            session.givePrize(PrizeKind::Thunderbolt);
            session.givePrize(PrizeKind::Meteor);
            out.boltsDue = session.weapons().strikesPending() - rules.meteors;
            out.meteorsDue = rules.meteors;
            for (const GameEvent& event : session.takeEvents()) {  // (the gives were made between frames)
                out.events.push_back(event);
            }
        }
        input.launch = input.launch || session.weapon().has_value();  // the click is held down
        const auto begin = std::chrono::steady_clock::now();
        session.update(1.0 / 60, input);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        totalMs += ms;
        out.worstStepMs = std::max(out.worstStepMs, ms);
        ++frames;

        for (const Projectile& projectile : session.projectiles()) {
            if (!finite(projectile.position) || !finite(projectile.velocity) || std::fabs(projectile.position.x) > kFar ||
                std::fabs(projectile.position.z) > kFar || projectile.position.y > kMostHeight || projectile.position.y < rules.lowLimit) {
                problem(std::format("projectile {} is at ({:.1f}, {:.1f}, {:.1f})", projectile.id, static_cast<double>(projectile.position.x),
                                    static_cast<double>(projectile.position.y), static_cast<double>(projectile.position.z)));
            }
            if (projectile.age > rules.projectileLife + 0.1) {
                problem(std::format("projectile {} has been in the air for {:.2f} s", projectile.id, projectile.age));
            }
        }
        if (session.projectiles().size() > kMostInTheAir) {
            problem(std::format("{} projectiles in the air", session.projectiles().size()));
        }
        for (const LaserBeam& beam : session.laserBeams()) {
            if (!finite(beam.from) || !finite(beam.to) || beam.to.z > beam.from.z + rules.laserRange + 0.01f || beam.to.z < beam.from.z - 0.01f) {
                problem("a beam is lost");
            }
        }
        for (const BallView& ball : session.balls()) {
            if (ball.held || ball.away || ball.stuck) {
                continue;
            }
            const float speed = std::hypot(ball.velocity.x, ball.velocity.z);
            if (!std::isfinite(speed) || std::fabs(speed - session.ballSpeed()) > 0.01f * session.ballSpeed()) {
                problem(std::format("ball {} speed {:.3f}", ball.id, static_cast<double>(speed)));
            }
        }
        for (const GameEvent& event : session.takeEvents()) {
            if (!finite(event.position) || !finite(event.velocity) || !finite(event.end)) {
                problem(std::format("{} is not finite", describe(event)));
            }
            if (event.type == EventType::BrickDestroyed) {
                ++out.destroyed;
            }
            if (isWeaponEvent(event.type) || everything) {
                out.events.push_back(event);
            }
            switch (event.type) {
                case EventType::PrizeCaught:  // a weapon the bat caught adds its shots
                    if (event.prize && weaponOf(*event.prize)) {
                        out.given[static_cast<std::size_t>(*weaponOf(*event.prize))] += prizeInfo(*event.prize).count;
                    }
                    break;
                case EventType::EffectEnded:  // the bat was lost: the weapon's shots left are given up
                    if (event.prize && weaponOf(*event.prize)) {
                        const auto type = static_cast<std::size_t>(*weaponOf(*event.prize));
                        out.given[type] = out.fired[type];
                    }
                    break;
                case EventType::StrikeGranted: grantedAt = event.time; break;
                case EventType::ShotFired: {
                    const std::optional<WeaponType> weapon = event.prize ? weaponOf(*event.prize) : std::nullopt;
                    if (!weapon) {
                        problem("a shot of no weapon");
                        break;
                    }
                    ++out.fired[static_cast<std::size_t>(*weapon)];
                    if (event.count != out.given[static_cast<std::size_t>(*weapon)] - out.fired[static_cast<std::size_t>(*weapon)]) {
                        problem(std::format("{} shots left of the {}, expected {}", event.count, prizeInfo(*event.prize).stem,
                                            out.given[static_cast<std::size_t>(*weapon)] - out.fired[static_cast<std::size_t>(*weapon)]));
                    }
                    break;
                }
                case EventType::ProjectileHit:
                    if (event.actor >= actors) {
                        problem("a projectile hit no actor");
                    }
                    break;
                case EventType::BoltStruck:
                    ++out.bolts;
                    if (event.actor >= actors) {
                        problem("a bolt struck no actor");
                    }
                    if (event.time - grantedAt > rules.boltLaterUntil + 1.5) {
                        problem(std::format("a bolt struck {:.1f} s after the catch", event.time - grantedAt));
                    }
                    break;
                case EventType::MeteorImpact:
                    ++out.meteors;
                    if (event.actor >= actors) {
                        problem("a meteor hit no actor");
                    }
                    break;
                case EventType::LaserBeam:
                    if (event.end.z > event.position.z + rules.laserRange + 0.01f || event.count < 0) {
                        problem("a beam ends beyond its range");
                    }
                    break;
                default: break;
            }
        }
    }
    out.stepMs = totalMs / std::max(frames, 1);
    out.stats = session.weapons().stats();

    if (!given) {  // the balls were never in play: nothing to check
        return out;
    }
    // Shots: counted once each, by the weapon's own projectiles.
    int firedAll = 0;
    for (std::size_t type = 0; type < kWeaponTypeCount; ++type) {
        const int left = session.shotsLeft(static_cast<WeaponType>(type));
        firedAll += out.fired[type];
        if (out.fired[type] + left != out.given[type]) {
            out.problems.push_back(std::format("{}: {} given, {} fired, {} left", prizeInfo(prizeOf(static_cast<WeaponType>(type))).stem,
                                               out.given[type], out.fired[type], left));
        }
    }
    if (firedAll != out.stats.shots) {
        out.problems.push_back(std::format("{} ShotFired events and {} shots in the statistics", firedAll, out.stats.shots));
    }
    const int gunShots = out.fired[static_cast<std::size_t>(WeaponType::Gun)], kernels = out.fired[static_cast<std::size_t>(WeaponType::Cannon)],
              rockets = out.fired[static_cast<std::size_t>(WeaponType::Bazooka)], beams = out.fired[static_cast<std::size_t>(WeaponType::Laser)];
    if (out.stats.beams != beams) {
        out.problems.push_back(std::format("{} laser shots and {} beams", beams, out.stats.beams));
    }
    const int strikesThrown = out.stats.bolts + out.stats.meteors;
    if (out.stats.projectiles != 2 * gunShots + kernels + rockets + strikesThrown) {
        out.problems.push_back(std::format("{} projectiles made for {} gun shots, {} kernels, {} rockets and {} strikes", out.stats.projectiles,
                                           gunShots, kernels, rockets, strikesThrown));
    }
    if (out.stats.projectiles != out.stats.hits + out.stats.gone + static_cast<int>(session.projectiles().size())) {
        out.problems.push_back(std::format("{} projectiles made, {} hit, {} gone, {} in the air", out.stats.projectiles, out.stats.hits,
                                           out.stats.gone, session.projectiles().size()));
    }
    // Strikes: every one that was due is thrown (more, if the bat caught another prize of the kind), and once the later bolts' time has
    // passed none is left pending, unless the level ended first.
    const bool ended = session.phase() == SessionPhase::Complete || session.phase() == SessionPhase::GameOver;
    if (!ended && (out.stats.bolts < out.boltsDue || out.stats.meteors < out.meteorsDue ||
                   (session.time() - grantedAt > rules.boltLaterUntil + 2 && session.weapons().strikesPending() != 0))) {
        out.problems.push_back(std::format("{} of {} bolts and {} of {} meteors were thrown, {} are still due", out.stats.bolts, out.boltsDue,
                                           out.stats.meteors, out.meteorsDue, session.weapons().strikesPending()));
    }
    int boltsInTheAir = 0, meteorsInTheAir = 0;
    for (const Projectile& projectile : session.projectiles()) {
        boltsInTheAir += projectile.kind == ProjectileKind::Bolt;
        meteorsInTheAir += projectile.kind == ProjectileKind::Meteor;
    }
    const int boltsGone = static_cast<int>(std::ranges::count_if(out.events, [](const GameEvent& e) {
        return e.type == EventType::ProjectileGone && e.prize == PrizeKind::Thunderbolt;
    }));
    const int meteorsGone = static_cast<int>(std::ranges::count_if(out.events, [](const GameEvent& e) {
        return e.type == EventType::ProjectileGone && e.prize == PrizeKind::Meteor;
    }));
    if (out.stats.bolts != out.bolts + boltsGone + boltsInTheAir || out.stats.meteors != out.meteors + meteorsGone + meteorsInTheAir) {
        out.problems.push_back(std::format("bolts: {} thrown, {} struck, {} gone, {} falling; meteors: {} thrown, {} struck, {} gone, {} falling",
                                           out.stats.bolts, out.bolts, boltsGone, boltsInTheAir, out.stats.meteors, out.meteors, meteorsGone,
                                           meteorsInTheAir));
    }
    if (out.stepMs > kMostStepMs) {
        out.problems.push_back(std::format("a step took {:.1f} ms on average", out.stepMs));
    }
    if (out.worstStepMs > kMostWorstStepMs) {
        out.problems.push_back(std::format("a step took {:.1f} ms", out.worstStepMs));
    }
    return out;
}

int run(const MaterialLibrary& materials, const LevelCatalog& catalog, const Campaign& campaign, double seconds, bool everything,
        const char* only) {
    std::printf("%-26s | %4s %4s %4s %4s | %5s %4s %4s | %4s %4s | %4s %4s | %5s | %s\n", "level", "gun", "can", "baz", "las", "made", "hit",
                "gone", "boltS", "mtrI", "hurt", "dead", "reach", "ms/step");
    int failed = 0, levels = 0;
    Weapons::Stats total;
    std::array<int, kWeaponTypeCount> fired{};
    int bolts = 0, meteors = 0, destroyed = 0;
    double stepMs = 0, worst = 0;
    for (const LevelEntry& entry : catalog.levels()) {
        if (only && entry.name.find(only) == std::string::npos) {
            continue;
        }
        ++levels;
        Outcome out;
        try {
            const Level level = loadLevelJson(entry.path);
            out = play(level, materials, seconds, campaign.indexOf(entry.name).value_or(0) + 1, everything);
        } catch (const std::exception& e) {
            out.problems.push_back(std::format("exception: {}", e.what()));
        }
        std::printf("%-26s | %4d %4d %4d %4d | %5d %4d %4d | %4d %4d | %4d %4d | %5d | %.3f\n", entry.name.c_str(), out.fired[0], out.fired[1],
                    out.fired[2], out.fired[3], out.stats.projectiles, out.stats.hits, out.stats.gone, out.bolts, out.meteors, out.stats.hurt,
                    out.destroyed, out.stats.beamReached, out.stepMs);
        std::fflush(stdout);
        total.projectiles += out.stats.projectiles;
        total.hits += out.stats.hits;
        total.gone += out.stats.gone;
        total.hurt += out.stats.hurt;
        total.beamReached += out.stats.beamReached;
        total.shots += out.stats.shots;
        for (std::size_t type = 0; type < kWeaponTypeCount; ++type) {
            fired[type] += out.fired[type];
        }
        bolts += out.bolts;
        meteors += out.meteors;
        destroyed += out.destroyed;
        stepMs += out.stepMs;
        worst = std::max(worst, out.worstStepMs);
        for (const std::string& what : out.problems) {
            std::printf("FAIL %s: %s\n", entry.name.c_str(), what.c_str());
            ++failed;
        }
        if (only) {
            for (const GameEvent& event : out.events) {
                std::printf("%s\n", describe(event).c_str());
            }
        }
    }
    std::printf("\n%d levels played for %.0f s each with every weapon and both strikes given at once and the click held down\n", levels, seconds);
    std::printf("shots: %d Machine Gun, %d Cannon, %d Bazooka, %d Laser; %d projectiles made (%d hit something, %d went), %d actors hurt, %d bolts and "
                "%d meteors struck, %d actors reached by beams, %d destroyed in all\n",
                fired[0], fired[1], fired[2], fired[3], total.projectiles, total.hits, total.gone, total.hurt, bolts, meteors, total.beamReached,
                destroyed);
    std::printf("a fixed step took %.3f ms on average, %.3f ms at the worst\n", stepMs / std::max(levels, 1), worst);
    if (!only && (fired[0] == 0 || fired[1] == 0 || fired[2] == 0 || fired[3] == 0 || bolts == 0 || meteors == 0 || total.hits == 0)) {
        std::printf("FAIL: some weapon or strike did nothing in any level\n");
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
    bool everything = false;
    const char* only = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            seconds = std::max(1.0, std::atof(argv[++i]));
        } else if (std::strcmp(argv[i], "--everything") == 0) {
            everything = true;
        } else if (argv[i][0] != '-') {
            only = argv[i];
        } else {
            std::printf("usage: mb_weapon_smoke [--seconds S] [--everything] [level]\n");
            return 2;
        }
    }
    try {
        const MaterialLibrary materials = MaterialLibrary::load(materialsJson);
        const LevelCatalog catalog(assets / "Data" / "Levels");
        const Campaign campaign = Campaign::load(assets / "Data" / "xml" / "levels.json");
        return run(materials, catalog, campaign, seconds, everything, only);
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }
}
