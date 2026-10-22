// The path actors of the real levels (needs the game data: skips with exit code 77 when MB3_ASSETS is not set), no GPU.
//
//   mb_path_smoke [--seconds S] [level]
//        every level is loaded and run in a PhysicsScene for S seconds (default 40). Its path actors must be the 35 the
//        data holds (in 8 levels: the objects with the path flag), start where their key puts them and stay finite, near
//        the board, on their path and at their track's speed; and after one lap of a closed path they are back where they
//        began. `level` runs one level and prints where each of its path actors is, every few seconds.

#include "mb/game/LevelCatalog.h"
#include "mb/game/LevelLoader.h"
#include "mb/game/MaterialLibrary.h"
#include "mb/game/PhysicsScene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace mb;

namespace {

constexpr float kStep = 1.0f / 60.0f;
// The data: 35 objects with the path flag, in 8 levels.
constexpr std::size_t kExpectedMovers = 35;
constexpr std::size_t kExpectedLevels = 8;
// A key lies on its path to within a centimetre, but for three whose path was edited afterwards (LEVEL_FORMAT.md, "Path
// motion"): the shark of piratos-ostrovok, 3.2 units off, and two small sharks of piratos-po-krugy, 0.3.
constexpr float kOnPath = 0.02f;
constexpr float kMostOffPath = 3.5f;
constexpr std::size_t kOffPathKeys = 3;
// The saved pose is a frame or two of travel behind its key, so it has turned a little less (0.04 at the birds' bends).
constexpr float kSavedTurn = 0.08f;
// The board is 40 wide and 44 deep and the water round it is flat; the paths stay within this.
constexpr float kBoardX = 25, kBoardZ = 25, kBoardY = 3;
// The speeds a shark or a bird may have, units a second: 40 units, the board's width, in 1.3 to 40 seconds.
constexpr double kSlowest = 1, kFastest = 30;
// How far the distance an actor covers in a thousandth of a second may be from its speed.
constexpr double kMostSpeedError = 0.01;

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

float degreesBetween(Vec3 a, Vec3 b) {
    return std::acos(std::clamp(dot(normalize(a), normalize(b)), -1.0f, 1.0f)) * 180.0f / kPi;
}

// The key of the file for an object id.
const PathKey* keyOf(const Level& level, std::uint32_t object) {
    for (const PathBinding& binding : level.pathBindings) {
        for (const PathTrack& track : binding.tracks) {
            for (const PathKey& key : track.keys) {
                if (key.object == object) {
                    return &key;
                }
            }
        }
    }
    return nullptr;
}

struct Totals {
    std::size_t levels = 0, movers = 0, offPath = 0;
    double worstSpeedError = 0;  // relative to the track's speed
    float farthestX = 0, farthestZ = 0, highest = 0;
};

void test(const Level& level, const MaterialLibrary& materials, const std::string& name, float seconds, bool verbose,
          Totals& total, int& failed) {
    const auto fail = [&](const std::string& what) {
        std::printf("FAIL %s: %s\n", name.c_str(), what.c_str());
        ++failed;
    };
    PhysicsScene scene(level, materials);
    const std::span<const PathMover> movers = scene.pathMovers();
    if (movers.empty()) {
        return;
    }
    ++total.levels;
    total.movers += movers.size();

    // The data and the actors agree: the actors that follow a path are the objects with the path flag, all kinematic.
    const std::vector<ActorRef> actors = flattenActors(level);
    std::size_t flagged = 0;
    for (const ActorRef& ref : actors) {
        flagged += ref.actor->object && (ref.actor->object->flags & 1) != 0;
    }
    if (flagged != movers.size() || scene.paths().unusedKeys() != 0 || scene.counts().pathMovers != movers.size()) {
        fail(std::format("{} objects have the path flag, {} actors follow a path, {} keys are unused", flagged,
                         movers.size(), scene.paths().unusedKeys()));
    }
    for (const PathMover& mover : movers) {
        const GameObject& object = *actors[mover.actor].actor->object;
        if ((object.flags & 1) == 0 || scene.body(mover.actor) == 0 ||
            scene.world().motionType(scene.body(mover.actor)) != MotionType::Kinematic) {
            fail(std::format("object {} follows a path but has no path flag or no kinematic body", object.id));
        }
    }

    // At the start an actor is where its key puts it (to a centimetre, for most), facing the way the key says; the saved pose
    // is the key a frame or two of travel behind.
    for (const PathMover& mover : movers) {
        const std::uint32_t id = actors[mover.actor].actor->object->id;
        const PathKey* key = keyOf(level, id);
        if (key == nullptr) {
            fail(std::format("object {} has no key", id));
            continue;
        }
        const PathFrame start = scene.paths().frame(mover, 0);
        const float off = length(start.position - key->position);
        if (off > kOnPath) {
            ++total.offPath;
            if (off > kMostOffPath) {
                fail(std::format("object {} starts {:.2f} from its key", id, static_cast<double>(off)));
            }
            continue;
        }
        if (degreesBetween(start.dir, key->dir) > 3.0f || degreesBetween(start.up, key->up) > 3.0f) {
            fail(std::format("object {} starts facing {:.1f} degrees off its key", id,
                             static_cast<double>(degreesBetween(start.dir, key->dir))));
        }
        const Mat4 saved = rigidTransform(actors[mover.actor].actor->pose);
        const float behind = length(saved.translationPart() - key->position);
        if (behind < 0.01f || behind > 0.2f || dot(saved.translationPart() - key->position, key->dir) > 0) {
            fail(std::format("object {} was saved {:.3f} from its key", id, static_cast<double>(behind)));
        }
        // The frame is the saved one's: its axes are the rows of the matrix, as the saved rotation has them.
        const Mat4 frame = scene.paths().pose(mover, 0);
        for (int axis = 0; axis < 3; ++axis) {
            const Vec3 mine{frame.m[axis][0], frame.m[axis][1], frame.m[axis][2]};
            const Vec3 theirs{saved.m[axis][0], saved.m[axis][1], saved.m[axis][2]};
            if (length(mine - theirs) > kSavedTurn) {
                fail(std::format("object {} has axis {} {:.3f} from the saved pose's", id, axis,
                                 static_cast<double>(length(mine - theirs))));
            }
        }
    }

    // Plausible speeds (the footage has not measured them yet).
    for (const PathMover& mover : movers) {
        if (std::fabs(mover.speed) < kSlowest || std::fabs(mover.speed) > kFastest) {
            fail(std::format("actor {} goes at {:.2f} units a second", mover.actor, mover.speed));
        }
    }

    // Once round a closed path an actor is back where it began.
    for (const PathMover& mover : movers) {
        const PathCurve& curve = scene.paths().curve(mover);
        const double lap = static_cast<double>(curve.totalLength()) / mover.speed;
        if (curve.closed() &&
            length(scene.paths().frame(mover, 0).position - scene.paths().frame(mover, lap).position) > 1e-3f) {
            fail(std::format("actor {} is not back where it began after one lap", mover.actor));
        }
    }

    // The path is walked at the track's speed: over a thousandth of a second an actor goes a thousandth of its speed, all the way
    // round (the sharks' paths have hairpins, where a coarse measure of distance would be out by a tenth).
    constexpr double kFine = 0.001;
    for (const PathMover& mover : movers) {
        const PathCurve& curve = scene.paths().curve(mover);
        const double lap = curve.closed() ? static_cast<double>(curve.totalLength()) / std::fabs(mover.speed) : 0.0;
        double worst = 0;
        for (double t = 0; t < lap; t += kFine) {
            const Vec3 from = scene.paths().frame(mover, t).position, to = scene.paths().frame(mover, t + kFine).position;
            const double moved = length(to - from);
            worst = std::max(worst, std::fabs(moved / (kFine * std::fabs(mover.speed)) - 1));
        }
        total.worstSpeedError = std::max(total.worstSpeedError, worst);
        if (worst > kMostSpeedError) {
            fail(std::format("actor {} goes {:.1f}% faster or slower than its track says somewhere on its lap", mover.actor,
                             worst * 100));
        }
    }

    // Run it: at the end of every fixed step the body is where its path has it, finite and near the board.
    const int steps = static_cast<int>(std::lround(seconds / kStep));
    std::vector<bool> reported(movers.size(), false);
    for (int s = 1; s <= steps; ++s) {
        scene.step(kStep);
        for (std::size_t k = 0; k < movers.size(); ++k) {
            const PathMover& mover = movers[k];
            const Mat4 pose = scene.actorPose(mover.actor);
            const Vec3 at = pose.translationPart();
            const std::string who = std::format("actor {}", mover.actor);
            if (!reported[k] && !finite(pose)) {
                reported[k] = true;
                fail(who + " has a pose that is not finite");
                continue;
            }
            total.farthestX = std::max(total.farthestX, std::fabs(at.x));
            total.farthestZ = std::max(total.farthestZ, std::fabs(at.z));
            total.highest = std::max(total.highest, std::fabs(at.y));
            if (!reported[k] && (std::fabs(at.x) > kBoardX || std::fabs(at.z) > kBoardZ || std::fabs(at.y) > kBoardY)) {
                reported[k] = true;
                fail(std::format("{} is off the board at ({:.1f}, {:.1f}, {:.1f}) after {:.1f} s", who,
                                 static_cast<double>(at.x), static_cast<double>(at.y), static_cast<double>(at.z),
                                 scene.pathTime()));
            }
            const Vec3 wanted = scene.paths().pose(mover, scene.pathTime()).translationPart();
            if (!reported[k] && length(at - wanted) > 2e-3f) {
                reported[k] = true;
                fail(std::format("{} is {:.3f} from its path after {:.1f} s", who, static_cast<double>(length(at - wanted)),
                                 scene.pathTime()));
            }
        }
        if (verbose && s % 300 == 0) {
            std::printf("%5.1f s:", scene.pathTime());
            for (const PathMover& mover : movers) {
                const Vec3 at = scene.actorPose(mover.actor).translationPart();
                std::printf(" (%.1f, %.1f)", static_cast<double>(at.x), static_cast<double>(at.z));
            }
            std::printf("\n");
        }
    }
    std::printf("%-28s %2zu path actors, %.1f to %.1f units a second\n", name.c_str(), movers.size(),
                std::ranges::min(movers, {}, &PathMover::speed).speed, std::ranges::max(movers, {}, &PathMover::speed).speed);
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
    float seconds = 40;
    const char* only = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            seconds = std::max(1.0f, static_cast<float>(std::atof(argv[++i])));
        } else if (argv[i][0] != '-') {
            only = argv[i];
        } else {
            std::printf("usage: mb_path_smoke [--seconds S] [level]\n");
            return 2;
        }
    }
    try {
        const MaterialLibrary materials = MaterialLibrary::load(materialsJson);
        const LevelCatalog catalog(assets / "Data" / "Levels");
        Totals total;
        int failed = 0;
        for (const LevelEntry& entry : catalog.levels()) {
            if (only && entry.name.find(only) == std::string::npos) {
                continue;
            }
            test(loadLevelJson(entry.path), materials, entry.name, seconds, only != nullptr, total, failed);
        }
        std::printf("%zu path actors in %zu levels, %zu keys off their path; the farthest from the middle of the board "
                    "x %.1f, z %.1f, y %.2f; the distance in a millisecond within %.1f%% of the tracks' speeds\n",
                    total.movers, total.levels, total.offPath, static_cast<double>(total.farthestX),
                    static_cast<double>(total.farthestZ), static_cast<double>(total.highest), total.worstSpeedError * 100);
        if (!only && (total.movers != kExpectedMovers || total.levels != kExpectedLevels || total.offPath != kOffPathKeys)) {
            std::printf("FAIL: %zu path actors in %zu levels, %zu of them off their path; expected %zu, %zu and %zu\n",
                        total.movers, total.levels, total.offPath, kExpectedMovers, kExpectedLevels, kOffPathKeys);
            ++failed;
        }
        return failed == 0 ? 0 : 1;
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }
}
