// The desktop twin of the Vita physics bench VPK (platform/vita/bench, title ID MUGB00004; docs/VITA_PLAN.md
// M3b): reads the same bench-physics.json, plays every level with the Autopilot through PhysicsBench.cpp, and
// appends one CSV line per level. Running the same JSON on the Mac and on the console is the point: the
// per-level ratio of the two CSVs (tools/bench_ratio.py) is the measured speed of the device, replacing the
// plan's estimate (docs/VITA_PLAN.md section 4.4).
//
//   mb_physics_bench <bench-physics.json> [--out <csv>] [--assets <dir>] [--platform <name>]
//        --out: where to append the CSV (default: bench-physics.csv next to the JSON)
//        --assets: the converted game data (default: mb::assetRoot(), which also reads MB3_ASSETS)
//        --platform: the platform column written to every CSV line (default: "host")
//   Exit 0 if every level in the JSON produced a CSV line, 1 if any did not (a bad level name, say), 2 for a
//   bad command line (usage is printed).

#include "Assets.h"
#include "PhysicsBench.h"

#include "mb/engine/Log.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace mb_app::bench;

namespace {

void printUsage() {
    std::fprintf(stderr,
                 "usage: mb_physics_bench <bench-physics.json> [--out <csv>] [--assets <dir>] [--platform <name>]\n");
}

}  // namespace

int main(int argc, char** argv) {
    fs::path configPath;
    fs::path outPath;
    fs::path assetsOverride;
    std::string platform = "host";

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            outPath = argv[++i];
        } else if (arg == "--assets" && i + 1 < argc) {
            assetsOverride = argv[++i];
        } else if (arg == "--platform" && i + 1 < argc) {
            platform = argv[++i];
        } else if (!arg.empty() && arg[0] != '-' && configPath.empty()) {
            configPath = arg;
        } else {
            printUsage();
            return 2;
        }
    }
    if (configPath.empty()) {
        printUsage();
        return 2;
    }
    if (outPath.empty()) {
        outPath = configPath.parent_path() / "bench-physics.csv";
    }

    std::string error;
    const std::optional<BenchConfig> config = loadBenchConfig(configPath, error);
    if (!config) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 2;
    }

    // Overriding the asset root through the same environment variable mb::assetRoot() already reads keeps
    // Assets's member initializers (which build from it) doing the one thing they already know how to do,
    // rather than duplicating their loading here.
    if (!assetsOverride.empty()) {
#if defined(_WIN32)
        _putenv_s("MB3_ASSETS", assetsOverride.string().c_str());
#else
        setenv("MB3_ASSETS", assetsOverride.string().c_str(), 1);
#endif
    }

    try {
        const mb_app::Assets assets;
        std::error_code ec;
        if (!fs::is_directory(assets.root / "Data" / "Levels", ec)) {
            std::fprintf(stderr, "no converted levels under %s (set MB3_ASSETS, or pass --assets)\n",
                         assets.root.string().c_str());
            return 1;
        }

        const auto onLevel = [&](const LevelResult& result) {
            // The planned step count the same way PhysicsBench.cpp derives it (config->seconds / fixedStep,
            // not a hardcoded 60 Hz), so this line's "/N" still matches what actually ran if a profile ever
            // moves off 1/60 s (docs/VITA_PLAN.md section 5.5's 30 Hz fallback).
            const mb::PhysicsSettings settings = settingsFor(*config, result.level);
            const int stepsPlanned = static_cast<int>(std::llround(config->seconds / settings.fixedStep));
            std::printf("%s: %d/%d steps, ended by %s, mean %.3f ms p95 %.3f ms, %llu dropped\n", result.level.c_str(),
                       result.steps, stepsPlanned, result.endedBy.c_str(),
                       result.stepMeanMs, result.stepP95Ms, static_cast<unsigned long long>(result.droppedSteps));
            const std::string line = csvLine(platform, *config, result);
            std::printf("%s\n", line.c_str());
        };

        const int done = runAll(assets, *config, platform, outPath, /*progress=*/{}, onLevel);
        std::printf("%d of %zu levels ran; see %s\n", done, config->levels.size(), outPath.string().c_str());
        return done == static_cast<int>(config->levels.size()) ? 0 : 1;
    } catch (const std::exception& e) {
        mb::logError("bench: {}", e.what());
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
