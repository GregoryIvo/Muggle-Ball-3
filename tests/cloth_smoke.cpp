// Runs the sails of every converted level in the physics world (needs the game data: skips with exit code 77 when MB3_ASSETS is
// not set), no GPU.
//
//   mb_cloth_smoke [--seconds S] [--print] [level]
//        each level with cloth is built the way the game builds it and left alone for S seconds (default 10): every particle must
//        stay finite and within 3 units of where it was saved, no spring may stretch past an eighth of its length, and every pinned
//        particle must be where its holder has it. Then a ball is shot at the middle of each sail, and the sail's touch must
//        be reported by the lead actor's id; a holder that is taken away lets its pins go, and a sail that is taken away
//        leaves the world. The table has, per level, the sails, their particles and pins, the furthest a particle got from where
//        it was saved, the most a spring stretched, and the time of a fixed step; the summary says what the pins are tied to (the mass
//        of the holders: GAMEPLAY.md 20.1). --print lists the particles after the time.

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
#include <filesystem>
#include <format>
#include <limits>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace mb;

namespace {

constexpr float kStep = 1.0f / 60.0f;
constexpr float kRunSeconds = 10.0f;
constexpr float kMostDrift = 3.0f;      // a particle this far from where the level saved it has blown away (2.31 at most, GAMEPLAY.md 20.6)
constexpr float kMostStretch = 0.12f;   // of a spring's length at rest (8 % at most)
constexpr float kPinSlack = 0.05f;      // how near a pinned particle is to the place its holder has it
constexpr float kLowSail = 5.0f;        // a sail whose middle hangs lower than this already lies on a deck or a boat
// The sails of the shipped levels: 22 of them, with 431 segments besides their own actors (453 particles), held by 84 joints.
constexpr std::size_t kSails = 22, kParticles = 453, kPins = 84;
// What the 84 pins are tied to: yards, masts and boats that weigh 1.07 to 67.99 (GAMEPLAY.md 20.1), not the 100,000 of a ship's hull: so it
// is not a ratio of masses that rules a joint out (Jolt has no constraint that reaches a soft body's particle).
constexpr float kLightestHolder = 1.0f, kHeaviestHolder = 70.0f;

bool finite(Vec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

// The most a spring along the grid or across a cell is longer or shorter than at rest, as a share of its rest length.
float mostStretch(const ClothPlan& cloth, const std::vector<Vec3>& now) {
    float most = 0;
    for (int r = 0; r < cloth.height; ++r) {
        for (int c = 0; c < cloth.width; ++c) {
            for (const auto [dr, dc] : {std::pair{0, 1}, std::pair{1, 0}, std::pair{1, 1}}) {
                if (r + dr >= cloth.height || c + dc >= cloth.width) {
                    continue;
                }
                const auto a = static_cast<std::size_t>(r * cloth.width + c);
                const auto b = static_cast<std::size_t>((r + dr) * cloth.width + c + dc);
                const float rest = length(cloth.rest[a] - cloth.rest[b]);
                if (rest > 1e-3f) {
                    most = std::max(most, std::fabs(length(now[a] - now[b]) - rest) / rest);
                }
            }
        }
    }
    return most;
}

struct Report {
    std::size_t sails = 0, particles = 0, pins = 0, onWorld = 0, onFixed = 0;
    float drift = 0, stretch = 0;
    float lightest = std::numeric_limits<float>::infinity(), heaviest = 0;  // the mass of what the dynamic holders are
    double stepMs = 0;
};

// One level standing alone: false (with the reason) if a sail misbehaves.
bool standAlone(const Level& level, const MaterialLibrary& materials, float seconds, bool print, Report& report, std::string& why) {
    PhysicsScene scene(level, materials);
    const ClothSet& cloths = scene.cloths();
    report.sails = cloths.size();
    if (scene.plan().unsupportedCloths > 0) {
        why = std::format("{} cloths the plan could not use", scene.plan().unsupportedCloths);
        return false;
    }
    for (std::size_t i = 0; i < cloths.size(); ++i) {
        report.particles += cloths.plan(i).actors.size();
        report.pins += cloths.plan(i).pins.size();
        for (const ClothPin& pin : cloths.plan(i).pins) {
            if (pin.holder == kNoActor) {
                ++report.onWorld;
            } else if (scene.plan().actors[pin.holder].motion != MotionType::Dynamic) {
                ++report.onFixed;
            } else if (const BodyId body = scene.body(pin.holder); body != 0) {
                report.lightest = std::min(report.lightest, scene.world().mass(body));
                report.heaviest = std::max(report.heaviest, scene.world().mass(body));
            }
        }
        if (!cloths.alive(i)) {
            why = std::format("sail {} has no body", i);
            return false;
        }
    }
    const auto begin = std::chrono::steady_clock::now();
    const int steps = static_cast<int>(std::lround(seconds / kStep));
    for (int s = 0; s < steps; ++s) {
        scene.step(kStep);
    }
    report.stepMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() / steps;

    for (std::size_t i = 0; i < cloths.size(); ++i) {
        const ClothPlan& cloth = cloths.plan(i);
        const std::vector<Vec3> now = cloths.positions(i);
        if (now.size() != cloth.rest.size() || !std::ranges::all_of(now, finite)) {
            why = std::format("sail {} has a particle that is not finite", i);
            return false;
        }
        for (std::size_t k = 0; k < now.size(); ++k) {
            report.drift = std::max(report.drift, length(now[k] - cloth.rest[k]));
        }
        if (scene.world().clothResets(cloths.body(i)) > 0) {
            why = std::format("the solver sent the particles of sail {} off beyond reach", i);
            return false;
        }
        const float stretch = mostStretch(cloth, now);
        report.stretch = std::max(report.stretch, stretch);
        if (stretch > kMostStretch) {
            why = std::format("sail {} stretches a spring by {:.0f} %", i, static_cast<double>(stretch) * 100);
            return false;
        }
        for (const ClothPin& pin : cloth.pins) {
            const Vec3 held = pin.holder == kNoActor ? pin.local : transformPoint(pin.local, scene.actorPose(pin.holder));
            if (length(now[static_cast<std::size_t>(pin.particle)] - held) > kPinSlack) {
                why = std::format("sail {}: particle {} is {:.2f} from where its holder has it", i, pin.particle,
                                  static_cast<double>(length(now[static_cast<std::size_t>(pin.particle)] - held)));
                return false;
            }
        }
        if (print) {
            std::printf("  sail %zu (%dx%d, lead actor %u, wind %.1f %.1f %.1f, %zu pins):\n", i, cloth.width, cloth.height, cloth.lead,
                        static_cast<double>(cloth.wind.x), static_cast<double>(cloth.wind.y), static_cast<double>(cloth.wind.z),
                        cloth.pins.size());
            for (int r = cloth.height - 1; r >= 0; --r) {
                std::printf("   ");
                for (int c = 0; c < cloth.width; ++c) {
                    const Vec3 p = now[static_cast<std::size_t>(r * cloth.width + c)];
                    std::printf(" (%6.2f %6.2f %6.2f)", static_cast<double>(p.x), static_cast<double>(p.y), static_cast<double>(p.z));
                }
                std::printf("\n");
            }
        }
    }
    if (report.drift > kMostDrift) {
        why = std::format("a particle is {:.1f} from where it was saved", static_cast<double>(report.drift));
        return false;
    }
    return true;
}

// A ball put on the middle of each sail: the touch is reported, by the lead actor.
bool ballTouches(const Level& level, const MaterialLibrary& materials, std::string& why) {
    PhysicsScene scene(level, materials);
    const ClothSet& cloths = scene.cloths();
    for (std::size_t i = 0; i < cloths.size(); ++i) {
        const ClothPlan& cloth = cloths.plan(i);
        const std::size_t middle = static_cast<std::size_t>((cloth.height / 2) * cloth.width + cloth.width / 2);
        for (int s = 0; s < 30; ++s) {  // let it settle a half second
            scene.step(kStep);
        }
        scene.launchBall(cloths.positions(i)[middle], {});
        bool touched = false;
        for (int s = 0; s < 10 && !touched; ++s) {
            scene.step(kStep);
            for (const ContactEvent& e : scene.contacts()) {
                touched = touched || e.userDataA == cloth.lead || e.userDataB == cloth.lead;
            }
        }
        if (!touched) {
            why = std::format("a ball put on the middle of sail {} is not reported as touching it", i);
            return false;
        }
    }
    return true;
}

// Taking the holders away lets the pins go and the sail fall, and taking a sail away takes it out of the world.
bool letsGoAndLeaves(const Level& level, const MaterialLibrary& materials, std::string& why) {
    PhysicsScene scene(level, materials);
    const ClothSet& cloths = scene.cloths();
    if (cloths.size() == 0) {
        return true;
    }
    const ClothPlan& first = cloths.plan(0);
    const Vec3 before = cloths.centre(0);
    for (const ClothPin& pin : first.pins) {
        if (pin.holder != kNoActor) {
            scene.removeActor(pin.holder);
        }
    }
    if (cloths.pinsHolding(0) != static_cast<std::size_t>(std::ranges::count(first.pins, kNoActor, &ClothPin::holder))) {
        why = std::format("sail 0 still has {} pins after its holders were taken away", cloths.pinsHolding(0));
        return false;
    }
    for (int s = 0; s < 240; ++s) {
        scene.step(kStep);
    }
    // (It may lie on a deck just under where it hung.)
    if (cloths.centre(0).y > before.y - 0.3f && before.y > kLowSail) {
        why = std::format("sail 0 hangs at {:.1f} (from {:.1f}) with nothing to hold it", static_cast<double>(cloths.centre(0).y),
                          static_cast<double>(before.y));
        return false;
    }
    const std::size_t bodies = scene.world().bodyCount();
    scene.removeActor(first.lead);
    if (cloths.alive(0) || scene.body(first.lead) != 0 || scene.world().bodyCount() != bodies - 1 || !cloths.positions(0).empty()) {
        why = "a sail that was taken out of the scene is still in it";
        return false;
    }
    for (int s = 0; s < 30; ++s) {
        scene.step(kStep);
    }
    return true;
}

int run(const MaterialLibrary& materials, const LevelCatalog& catalog, float seconds, bool print, const char* only) {
    int failed = 0;
    std::size_t sails = 0, particles = 0, pins = 0, levels = 0, onWorld = 0, onFixed = 0;
    double stepMs = 0;
    float drift = 0, stretch = 0, lightest = std::numeric_limits<float>::infinity(), heaviest = 0;
    std::printf("%-26s %5s %9s %5s %7s %8s %8s\n", "level", "sails", "particles", "pins", "drift", "stretch", "ms/step");
    for (const LevelEntry& entry : catalog.levels()) {
        if (only && entry.name.find(only) == std::string::npos) {
            continue;
        }
        const Level level = loadLevelJson(entry.path);
        if (level.countObjects(ObjectType::Cloth) == 0) {
            continue;
        }
        Report report;
        std::string why;
        const std::string header = std::format("{:<26}", entry.name);
        const bool ok = standAlone(level, materials, seconds, print, report, why) && ballTouches(level, materials, why) &&
                        letsGoAndLeaves(level, materials, why);
        std::printf("%s %5zu %9zu %5zu %7.2f %8.3f %8.3f\n", header.c_str(), report.sails, report.particles, report.pins,
                    static_cast<double>(report.drift), static_cast<double>(report.stretch), report.stepMs);
        if (!ok) {
            std::printf("FAIL %s: %s\n", entry.name.c_str(), why.c_str());
            ++failed;
        }
        ++levels;
        sails += report.sails;
        particles += report.particles;
        pins += report.pins;
        onWorld += report.onWorld;
        onFixed += report.onFixed;
        lightest = std::min(lightest, report.lightest);
        heaviest = std::max(heaviest, report.heaviest);
        stepMs += report.stepMs;
        drift = std::max(drift, report.drift);
        stretch = std::max(stretch, report.stretch);
    }
    std::printf("%zu sails with %zu particles, held by %zu pins; the furthest a particle got from where it was saved is %.2f, the most a "
                "spring stretched is %.0f %% of its length; a fixed step took %.3f ms on average over the levels, with the sails in them\n",
                sails, particles, pins, static_cast<double>(drift), static_cast<double>(stretch) * 100,
                stepMs / static_cast<double>(std::max<std::size_t>(levels, 1)));
    std::printf("the pins are on the world (%zu), on bodies that are not dynamic (%zu) and on dynamic ones of %.2f to %.2f in mass\n", onWorld,
                onFixed, static_cast<double>(lightest), static_cast<double>(heaviest));
    if (!only && !(lightest >= kLightestHolder && heaviest <= kHeaviestHolder)) {
        std::printf("FAIL all: the pins' dynamic holders weigh %.2f to %.2f, not %.1f to %.1f\n", static_cast<double>(lightest),
                    static_cast<double>(heaviest), static_cast<double>(kLightestHolder), static_cast<double>(kHeaviestHolder));
        ++failed;
    }
    if (!only && (sails != kSails || particles != kParticles || pins != kPins)) {
        std::printf("FAIL all: the shipped levels have %zu sails of %zu particles held by %zu pins, not %zu of %zu held by %zu\n",
                    kSails, kParticles, kPins, sails, particles, pins);
        ++failed;
    }
    return failed == 0 ? 0 : 1;
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
    float seconds = kRunSeconds;
    bool print = false;
    const char* only = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            seconds = std::max(1.0f, static_cast<float>(std::atof(argv[++i])));
        } else if (std::strcmp(argv[i], "--print") == 0) {
            print = true;
        } else if (argv[i][0] != '-') {
            only = argv[i];
        } else {
            std::printf("usage: mb_cloth_smoke [--seconds S] [--print] [level]\n");
            return 2;
        }
    }
    try {
        const MaterialLibrary materials = MaterialLibrary::load(materialsJson);
        const LevelCatalog catalog(assets / "Data" / "Levels");
        return run(materials, catalog, seconds, print, only);
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }
}
