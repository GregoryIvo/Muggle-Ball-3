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
#include <cstdio>
#include <exception>
#include <format>
#include <fstream>
#include <sstream>
#include <system_error>

namespace mb_app::bench {

namespace {

using nlohmann::json;
using clock_type = std::chrono::steady_clock;

// How often runLevel calls `progress` while a level is running: about ten times a wall-clock second, so a
// console's screen or a terminal gets a live number without the call itself costing anything worth measuring.
constexpr double kProgressIntervalMs = 100.0;

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

// Reads an optional integer key into `out`; false (with `error` set) if the key is present but not an integer.
bool readOptionalInt(const json& root, const char* key, const std::filesystem::path& path, std::optional<int>& out,
                     std::string& error) {
    if (!root.contains(key)) {
        return true;
    }
    if (!root[key].is_number_integer()) {
        error = std::format("{}: \"{}\" must be an integer", path.string(), key);
        return false;
    }
    out = root[key].get<int>();
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

    if (!readOptionalInt(root, "velocitySteps", path, config.velocitySteps, error) ||
        !readOptionalInt(root, "positionSteps", path, config.positionSteps, error) ||
        !readOptionalInt(root, "collisionSteps", path, config.collisionSteps, error) ||
        !readOptionalInt(root, "threads", path, config.threads, error)) {
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
    const int stepsPlanned = static_cast<int>(std::llround(config.seconds * 60.0));

    mb::Autopilot autopilot;
    std::vector<double> stepTimes;
    stepTimes.reserve(static_cast<std::size_t>(std::max(stepsPlanned, 0)));
    int peakAwake = 0;
    double awakeSum = 0;
    int steps = 0;
    std::string endedBy = "time";

    auto lastProgress = clock_type::now();
    bool stopped = false;
    for (int i = 0; i < stepsPlanned; ++i) {
        const mb::GameInput input = autopilot.decide(session);
        const auto stepStart = clock_type::now();
        session.update(fixedStep, input);
        const auto stepEnd = clock_type::now();
        stepTimes.push_back(millisBetween(stepStart, stepEnd));
        ++steps;

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
            if (!progress(steps, stepsPlanned) && !ended) {
                endedBy = "stopped";
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
    int done = 0;
    for (const std::string& levelId : config.levels) {
        const std::optional<LevelResult> result = runLevel(assets, config, levelId, progress);
        if (!result) {
            continue;
        }
        if (!appendCsv(csv, csvLine(platform, config, *result))) {
            mb::logError("bench: {}: ran but its CSV line could not be written", result->level);
        }
        if (onLevel) {
            onLevel(*result);
        }
        ++done;
        if (result->endedBy == "stopped") {
            // A caller's progress callback asked to stop: that is the whole run ending, not just this level,
            // so later levels in the config are left for a later run rather than started and immediately cut
            // short too (the Vita bench's "press a button to stop").
            break;
        }
    }
    return done;
}

}  // namespace mb_app::bench
