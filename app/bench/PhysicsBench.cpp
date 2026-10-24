#include "PhysicsBench.h"

#include "mb/engine/Log.h"
#include "mb/game/Autopilot.h"
#include "mb/game/Gameplay.h"
#include "mb/game/LevelCatalog.h"
#include "mb/game/LevelLoader.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <format>
#include <fstream>
#include <limits>
#include <ratio>
#include <sstream>
#include <system_error>

#if defined(__vita__)
#include <psp2/kernel/processmgr.h>
#endif

namespace mb_app::bench {

namespace {

using nlohmann::json;

#if defined(__vita__)
// This SDK's libstdc++ has no CLOCK_MONOTONIC (arm-vita-eabi/bits/c++config.h leaves
// _GLIBCXX_USE_CLOCK_MONOTONIC undefined and sets _GLIBCXX_USE_GETTIMEOFDAY), so std::chrono::steady_clock
// falls back to gettimeofday: a wall clock, not guaranteed monotonic, and of unverified resolution on real
// hardware -- not what a step timer wants. The kernel's own process-time counter is what
// platform/vita/hello/main.cpp's probe confirmed to be monotonic, in whole microseconds; wrapping it in a
// tiny Clock keeps every clock_type::now()/millisBetween() below the same on both platforms.
struct VitaProcessClock {
    using rep = std::int64_t;
    using period = std::micro;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<VitaProcessClock>;
    static constexpr bool is_steady = true;

    static time_point now() noexcept {
        return time_point(duration(static_cast<rep>(sceKernelGetProcessTimeWide())));
    }
};
using clock_type = VitaProcessClock;
#else
using clock_type = std::chrono::steady_clock;
#endif

// How often runLevel calls `progress` while a level is running: about ten times a wall-clock second, so a
// console's screen or a terminal gets a live number without the call itself costing anything worth measuring.
constexpr double kProgressIntervalMs = 100.0;

// bench-physics.json's "seconds" must stay positive and this side of overflowing the int stepsPlanned is cast
// to (static_cast<int>(std::llround(seconds / fixedStep))) and the step-time vector reserve() built from it.
// A day is far past anything the plan contemplates (the longest run discussed is a 240 s stability pass).
constexpr double kMaxBenchSeconds = 24.0 * 60.0 * 60.0;

double millisBetween(clock_type::time_point start, clock_type::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// ASCII-only lowercase, matching the catalogue's own comparisons (LevelCatalog, PhysicsProfile): the game's
// level and profile names are ASCII, so this is locale-independent and identical on every platform.
std::string lowerAscii(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// A CSV-quoted field: wrapped in double quotes, with any inner quote doubled (RFC 4180). Named csvQuote, not
// quoted: unqualified, that name loses every overload below taking a std::string argument to std::quoted
// (<iomanip>, found by argument-dependent lookup on std::string's own namespace), which is a stream
// manipulator, not a string, and nothing here would even notice until std::format refused it.
std::string csvQuote(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"') {
            out += '"';
        }
        out += c;
    }
    out += '"';
    return out;
}

std::string joinedProfileNames() {
    std::string names;
    for (const std::string_view name : mb::physicsProfileNames()) {
        names += (names.empty() ? "" : ", ") + std::string(name);
    }
    return names;
}

// Reads an optional integer key into `out`; false (with `error` set) if the key is present but not an integer,
// or an integer outside [minValue, INT_MAX]. The range check matters on its own: nlohmann's get<int>() on a
// value too big for int narrows it silently rather than erroring (confirmed: "velocitySteps": 100000000000
// becomes 1215752192 in the CSV), and minValue catches an in-range value that is nonsense for this bench (0 or
// negative solver steps, say).
bool readOptionalInt(const json& root, const char* key, const std::filesystem::path& path, int minValue,
                     std::optional<int>& out, std::string& error) {
    if (!root.contains(key)) {
        return true;
    }
    if (!root[key].is_number_integer()) {
        error = std::format("{}: \"{}\" must be an integer", path.string(), key);
        return false;
    }
    const std::int64_t wide = root[key].get<std::int64_t>();
    constexpr std::int64_t kIntMax = static_cast<std::int64_t>(std::numeric_limits<int>::max());
    if (wide < static_cast<std::int64_t>(minValue) || wide > kIntMax) {
        error = std::format("{}: \"{}\" must be an integer between {} and {}", path.string(), key, minValue, kIntMax);
        return false;
    }
    out = static_cast<int>(wide);
    return true;
}

}  // namespace

std::optional<BenchConfig> loadBenchConfig(const std::filesystem::path& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = std::format("{}: cannot be opened", path.string());
        return std::nullopt;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    json root;
    try {
        root = json::parse(buf.str());
    } catch (const json::exception& e) {
        error = std::format("{}: not valid JSON ({})", path.string(), e.what());
        return std::nullopt;
    }
    if (!root.is_object()) {
        error = std::format("{}: the top level must be a JSON object", path.string());
        return std::nullopt;
    }

    BenchConfig config;

    if (!root.contains("levels") || !root["levels"].is_array() || root["levels"].empty()) {
        error = std::format("{}: \"levels\" must be a non-empty array of level names", path.string());
        return std::nullopt;
    }
    for (const json& level : root["levels"]) {
        if (!level.is_string()) {
            error = std::format("{}: every entry of \"levels\" must be a string", path.string());
            return std::nullopt;
        }
        config.levels.push_back(level.get<std::string>());
    }

    if (root.contains("seconds")) {
        if (!root["seconds"].is_number()) {
            error = std::format("{}: \"seconds\" must be a number", path.string());
            return std::nullopt;
        }
        config.seconds = root["seconds"].get<double>();
    }
    // Zero, negative or absurdly large all produce a plausible-looking CSV row (0 steps and all-zero times
    // for the first two; an overflowing stepsPlanned for the last) rather than an obvious error.
    if (!(config.seconds > 0) || config.seconds > kMaxBenchSeconds) {
        error = std::format("{}: \"seconds\" must be greater than 0 and at most {} ({} given)", path.string(),
                           kMaxBenchSeconds, config.seconds);
        return std::nullopt;
    }

    if (root.contains("profile")) {
        if (!root["profile"].is_string()) {
            error = std::format("{}: \"profile\" must be a string", path.string());
            return std::nullopt;
        }
        config.profile = root["profile"].get<std::string>();
    }
    if (!mb::physicsProfile(config.profile)) {
        error = std::format("{}: unknown physics profile \"{}\"; known profiles: {}", path.string(), config.profile,
                           joinedProfileNames());
        return std::nullopt;
    }

    // velocitySteps/positionSteps/collisionSteps are iteration counts: at least 1 or there is no solver left
    // to run. threads follows PhysicsSettings' own convention (engine/include/mb/engine/Physics.h): -1 means
    // "pick for me", 0 means "on the caller", and up from there is an explicit worker count.
    if (!readOptionalInt(root, "velocitySteps", path, 1, config.velocitySteps, error) ||
        !readOptionalInt(root, "positionSteps", path, 1, config.positionSteps, error) ||
        !readOptionalInt(root, "collisionSteps", path, 1, config.collisionSteps, error) ||
        !readOptionalInt(root, "threads", path, -1, config.threads, error)) {
        return std::nullopt;
    }

    if (root.contains("label")) {
        if (!root["label"].is_string()) {
            error = std::format("{}: \"label\" must be a string", path.string());
            return std::nullopt;
        }
        config.label = root["label"].get<std::string>();
    }

    return config;
}

mb::PhysicsSettings settingsFor(const BenchConfig& config, std::string_view levelId) {
    mb::PhysicsSettings settings = mb::physicsSettingsFor(*mb::physicsProfile(config.profile), levelId);
    if (config.velocitySteps) {
        settings.velocitySteps = *config.velocitySteps;
    }
    if (config.positionSteps) {
        settings.positionSteps = *config.positionSteps;
    }
    if (config.collisionSteps) {
        settings.collisionSteps = *config.collisionSteps;
    }
    if (config.threads) {
        settings.threads = *config.threads;
    }
    return settings;
}

std::optional<LevelResult> runLevel(const Assets& assets, const BenchConfig& config, std::string_view levelId,
                                    const Progress& progress) {
    const auto runStart = clock_type::now();

    // Catalogue lookup, any case: assets is const (the level catalogue is shared across every level of the
    // run), so this looks the entries up itself rather than calling LevelCatalog::find(), which may append to
    // the catalogue and so needs a non-const one.
    const std::string wanted = lowerAscii(levelId);
    const mb::LevelEntry* entry = nullptr;
    for (const mb::LevelEntry& candidate : assets.catalog.levels()) {
        if (lowerAscii(candidate.name) == wanted) {
            entry = &candidate;
            break;
        }
    }
    if (!entry) {
        mb::logError("bench: \"{}\" is not in the level catalogue", levelId);
        return std::nullopt;
    }

    const auto loadStart = clock_type::now();
    std::optional<mb::Level> level;
    try {
        level = mb::loadLevelJson(entry->path);
    } catch (const std::exception& e) {
        mb::logError("bench: {}: failed to load ({})", entry->name, e.what());
        return std::nullopt;
    }
    const double loadMs = millisBetween(loadStart, clock_type::now());

    mb::SessionOptions options;
    options.physicsSettings = settingsFor(config, entry->name);
    options.hearts = 99;

    const auto buildStart = clock_type::now();
    mb::GameSession session(*level, assets.materials, options);
    const double buildMs = millisBetween(buildStart, clock_type::now());

    const int bodies = static_cast<int>(session.scene().world().bodyCount());
    const float fixedStep = options.physicsSettings.fixedStep;
    // Derived from this level's own fixedStep, not a hardcoded 60 Hz: the "vita" profile keeps 1/60 s today,
    // but a future fallback to 30 Hz physics (docs/VITA_PLAN.md section 5.5) must not silently double how
    // long a level actually runs against what bench-physics.json's "seconds" asked for.
    const int stepsPlanned = static_cast<int>(std::llround(config.seconds / fixedStep));

    mb::Autopilot autopilot;
    std::vector<double> stepTimes;
    stepTimes.reserve(static_cast<std::size_t>(std::max(stepsPlanned, 0)));
    int peakAwake = 0;
    double awakeSum = 0;
    int steps = 0;
    std::string endedBy = "time";
    bool stopRequested = false;

    auto lastProgress = clock_type::now();
    for (int i = 0; i < stepsPlanned; ++i) {
        const mb::GameInput input = autopilot.decide(session);
        const auto stepStart = clock_type::now();
        session.update(fixedStep, input);
        const auto stepEnd = clock_type::now();
        stepTimes.push_back(millisBetween(stepStart, stepEnd));
        ++steps;
        // Drained every step, outside the timed window above, the same as autoplay() and the game itself
        // (game/src/Autopilot.cpp; GameSession::emit(), game/src/Gameplay.cpp, caps the queue at 16384 and
        // warns once): otherwise a long run (the 240 s stability-style passes the plan contemplates) does
        // more and more per-step bookkeeping as the queue grows, instead of the small, constant amount of
        // work a frame actually does under the game.
        (void)session.takeEvents();

        const int awake = static_cast<int>(session.scene().world().activeBodyCount());
        peakAwake = std::max(peakAwake, awake);
        awakeSum += awake;

        if (session.phase() == mb::SessionPhase::Complete) {
            endedBy = "complete";
        } else if (session.phase() == mb::SessionPhase::GameOver) {
            endedBy = "over";
        }
        const bool ended = endedBy != "time";
        const bool last = i + 1 == stepsPlanned;

        if (progress && (ended || last || millisBetween(lastProgress, stepEnd) >= kProgressIntervalMs)) {
            lastProgress = stepEnd;
            if (!progress(steps, stepsPlanned)) {
                // stopRequested is tracked whether or not this call also happens to be the level's own
                // natural end: a level that just reached its last planned step, or Complete/GameOver, on
                // this very call keeps its true endedBy below, but the stop request itself must still reach
                // the caller (runAll breaks the whole run on stopRequested, not on endedBy) -- otherwise a
                // button press that coincides with the level ending is either mislabelled as an early
                // "stopped" run that in fact went the full distance, or silently swallowed.
                stopRequested = true;
                if (!ended && !last) {
                    endedBy = "stopped";
                }
            }
        }
        if (endedBy != "time") {
            break;
        }
    }
    if (stepsPlanned <= 0 && progress) {
        progress(0, 0);
    }

    std::sort(stepTimes.begin(), stepTimes.end());
    const auto percentile = [&](double fraction) {
        return stepTimes.empty() ? 0.0 : stepTimes[std::min(stepTimes.size() - 1,
                                                            static_cast<std::size_t>(fraction * stepTimes.size()))];
    };
    double sum = 0;
    for (const double ms : stepTimes) {
        sum += ms;
    }

    LevelResult result;
    result.level = entry->name;
    result.simulatedSeconds = steps * fixedStep;
    result.steps = steps;
    result.endedBy = endedBy;
    result.stopRequested = stopRequested;
    result.loadMs = loadMs;
    result.buildMs = buildMs;
    result.stepMeanMs = stepTimes.empty() ? 0.0 : sum / stepTimes.size();
    result.stepP95Ms = percentile(0.95);
    result.stepP99Ms = percentile(0.99);
    result.stepMaxMs = stepTimes.empty() ? 0.0 : stepTimes.back();
    result.bodies = bodies;
    result.peakAwake = peakAwake;
    result.meanAwake = steps > 0 ? awakeSum / steps : 0.0;
    result.droppedSteps = session.droppedSteps();
    result.tempAllocatorPeakBytes = session.scene().world().tempAllocatorPeakBytes();
    result.score = session.score();
    result.wallSeconds = std::chrono::duration<double>(clock_type::now() - runStart).count();

    mb::logInfo("bench: {}: {} steps, ended by {}, mean {:.3f} ms p95 {:.3f} p99 {:.3f} max {:.3f}, peak {} awake, "
               "{} bodies, {} dropped, {:.2f} s wall",
               result.level, result.steps, result.endedBy, result.stepMeanMs, result.stepP95Ms, result.stepP99Ms,
               result.stepMaxMs, result.peakAwake, result.bodies, result.droppedSteps, result.wallSeconds);
    return result;
}

std::string csvHeader() {
    return "platform,label,level,profile,velocitySteps,positionSteps,collisionSteps,threads,simulatedSeconds,steps,"
          "endedBy,loadMs,buildMs,stepMeanMs,stepP95Ms,stepP99Ms,stepMaxMs,bodies,peakAwake,meanAwake,droppedSteps,"
          "tempAllocatorPeakBytes,score,wallSeconds";
}

std::string csvLine(std::string_view platform, const BenchConfig& config, const LevelResult& result) {
    const mb::PhysicsSettings settings = settingsFor(config, result.level);
    return std::format(
        "{},{},{},{},{},{},{},{},{:.2f},{},{},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{},{},{:.2f},{},{},{},{:.2f}",
        csvQuote(platform), csvQuote(config.label), csvQuote(result.level), csvQuote(config.profile),
        settings.velocitySteps, settings.positionSteps, settings.collisionSteps, settings.threads,
        result.simulatedSeconds, result.steps,
        csvQuote(result.endedBy), result.loadMs, result.buildMs, result.stepMeanMs, result.stepP95Ms, result.stepP99Ms,
        result.stepMaxMs, result.bodies, result.peakAwake, result.meanAwake, result.droppedSteps,
        result.tempAllocatorPeakBytes, result.score, result.wallSeconds);
}

bool appendCsv(const std::filesystem::path& path, const std::string& line) {
    std::error_code ec;
    const bool needsHeader = !std::filesystem::exists(path, ec) || std::filesystem::file_size(path, ec) == 0;
    std::FILE* file = std::fopen(path.string().c_str(), "a");
    if (!file) {
        mb::logError("bench: cannot open {} for appending", path.string());
        return false;
    }
    if (needsHeader) {
        const std::string header = csvHeader();
        std::fprintf(file, "%s\n", header.c_str());
    }
    std::fprintf(file, "%s\n", line.c_str());
    std::fflush(file);
    std::fclose(file);
    return true;
}

int runAll(const Assets& assets, const BenchConfig& config, std::string_view platform, const std::filesystem::path& csv,
          const Progress& progress, const std::function<void(const LevelResult&)>& onLevel) {
    // Opened and closed without writing anything, before any level runs: appendCsv() itself is only tried
    // once a level has already spent up to `seconds` running, so a bad --out (or, on the Vita, a full or
    // read-only ux0:) would otherwise only surface after every level's real work was already thrown away.
    // Leaves a new path still missing and an existing file untouched either way.
    if (!config.levels.empty()) {
        std::FILE* probe = std::fopen(csv.string().c_str(), "a");
        if (!probe) {
            mb::logError("bench: cannot open {} for appending; no level will be run", csv.string());
            return 0;
        }
        std::fclose(probe);
    }

    int done = 0;
    for (const std::string& levelId : config.levels) {
        const std::optional<LevelResult> result = runLevel(assets, config, levelId, progress);
        if (!result) {
            continue;
        }
        if (!appendCsv(csv, csvLine(platform, config, *result))) {
            // Ran, but produced nothing a caller can see: not "done" by this file's own contract (see
            // PhysicsBench.h), so neither the count below nor onLevel claims the level's work landed
            // anywhere -- the CLI's exit code and the Vita's "done: N of N" both stay honest instead of
            // reporting success for a CSV line that was never written.
            mb::logError("bench: {}: ran but its CSV line could not be written", result->level);
            continue;
        }
        if (onLevel) {
            onLevel(*result);
        }
        ++done;
        if (result->stopRequested) {
            // A caller's progress callback asked to stop at some point during this level (the Vita's "press
            // a button to stop"): that is the whole run ending, not just this level, so later levels in the
            // config are left for a later run rather than started and immediately cut short too. Checked on
            // stopRequested, not endedBy, so a stop that coincided with the level's own natural end (its
            // last planned step, or Complete/GameOver) is still honoured here even though that level's
            // endedBy correctly stayed "time"/"complete"/"over".
            break;
        }
    }
    return done;
}

}  // namespace mb_app::bench
