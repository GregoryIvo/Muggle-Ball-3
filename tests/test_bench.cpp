// The physics bench's core (app/bench/PhysicsBench.h, compiled straight from there: see tests/CMakeLists.txt):
// loadBenchConfig's validation, settingsFor's layering of a profile, its per-level overrides and a config's
// explicit values, and the CSV formatting and append-once behaviour. CPU only, no game data: runLevel and
// runAll need a real Assets and real levels, which belongs with the smokes (mb_gameplay_smoke and friends),
// not here.

#include "PhysicsBench.h"

#include "mb/game/PhysicsProfile.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <system_error>

namespace fs = std::filesystem;
using namespace mb_app::bench;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// A directory under the system temp folder, removed (and anything left in it) when it goes out of scope; the
// same pattern as tests/test_save.cpp's TempDir.
class TempDir {
public:
    TempDir() : path(fs::temp_directory_path() / ("mb3-bench-test-" + std::to_string(std::random_device{}()))) {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    fs::path path;
};

fs::path writeFile(const fs::path& dir, const char* name, std::string_view contents) {
    const fs::path path = dir / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << contents;
    return path;
}

// ---------------------------------------------------------------- loadBenchConfig

void testDefaults() {
    TempDir dir;
    const fs::path path = writeFile(dir.path, "config.json", R"({"levels": ["village-love"]})");
    std::string error;
    const std::optional<BenchConfig> config = loadBenchConfig(path, error);
    check(config.has_value(), "a minimal config (just levels) loads");
    check(error.empty(), "and leaves no error");
    if (!config) {
        return;
    }
    check(config->levels.size() == 1 && config->levels[0] == "village-love", "its one level is kept");
    check(config->seconds == 20, "seconds defaults to 20");
    check(config->profile == "desktop", "profile defaults to \"desktop\"");
    check(!config->velocitySteps && !config->positionSteps && !config->collisionSteps && !config->threads,
          "the four solver overrides default to nothing, meaning \"the profile's own value\"");
    check(config->label.empty(), "label defaults to empty");
}

void testEveryKey() {
    TempDir dir;
    const fs::path path = writeFile(dir.path, "config.json", R"({
        "levels": ["knight-garden", "Village-Dvorik"],
        "seconds": 45.5,
        "profile": "vita",
        "velocitySteps": 30, "positionSteps": 1, "collisionSteps": 2, "threads": 1,
        "label": "pass2-a",
        "notes": "ignored, so a file can carry documentation"
    })");
    std::string error;
    const std::optional<BenchConfig> config = loadBenchConfig(path, error);
    check(config.has_value(), "a config with every key loads");
    if (!config) {
        return;
    }
    check(config->levels.size() == 2 && config->levels[0] == "knight-garden" && config->levels[1] == "Village-Dvorik",
          "both levels are kept, in order, with their own spelling");
    check(config->seconds == 45.5, "seconds is read");
    check(config->profile == "vita", "profile is read");
    check(config->velocitySteps == 30 && config->positionSteps == 1 && config->collisionSteps == 2 &&
             config->threads == 1,
          "the four solver overrides are read");
    check(config->label == "pass2-a", "label is read");
}

void testWrongTypesAreErrors() {
    TempDir dir;
    const struct {
        const char* name;
        const char* json;
    } cases[] = {
        {"levels missing", R"({"seconds": 10})"},
        {"levels not an array", R"({"levels": "village-love"})"},
        {"levels empty", R"({"levels": []})"},
        {"a level is not a string", R"({"levels": [1]})"},
        {"seconds not a number", R"({"levels": ["village-love"], "seconds": "20"})"},
        {"profile not a string", R"({"levels": ["village-love"], "profile": 1})"},
        {"velocitySteps not an integer", R"({"levels": ["village-love"], "velocitySteps": "100"})"},
        {"velocitySteps not an integer (a float)", R"({"levels": ["village-love"], "velocitySteps": 1.5})"},
        {"label not a string", R"({"levels": ["village-love"], "label": 7})"},
        {"not an object", R"(["village-love"])"},
        {"not JSON at all", R"(not json)"},
    };
    for (const auto& c : cases) {
        const fs::path path = writeFile(dir.path, "config.json", c.json);
        std::string error;
        const std::optional<BenchConfig> config = loadBenchConfig(path, error);
        check(!config.has_value(), (std::string("rejected: ") + c.name).c_str());
        check(!error.empty(), (std::string("names an error: ") + c.name).c_str());
    }
}

void testUnknownProfileNamesTheKnownOnes() {
    TempDir dir;
    const fs::path path = writeFile(dir.path, "config.json", R"({"levels": ["village-love"], "profile": "nope"})");
    std::string error;
    const std::optional<BenchConfig> config = loadBenchConfig(path, error);
    check(!config.has_value(), "an unknown profile is rejected");
    check(error.find("nope") != std::string::npos, "the error names the profile that was asked for");
    check(error.find("desktop") != std::string::npos && error.find("vita") != std::string::npos,
          "the error lists the known profiles (physicsProfileNames())");
}

void testMissingFileIsAnError() {
    std::string error;
    const std::optional<BenchConfig> config = loadBenchConfig("/no/such/file/bench-physics.json", error);
    check(!config.has_value(), "a missing file is rejected");
    check(!error.empty(), "with a non-empty error");
}

// ---------------------------------------------------------------- settingsFor

void testSettingsForWithoutOverrides() {
    BenchConfig config;
    config.profile = "desktop";
    const mb::PhysicsSettings settings = settingsFor(config, "village-love");
    const mb::PhysicsSettings plain{};
    check(settings.velocitySteps == plain.velocitySteps && settings.fixedStep == plain.fixedStep &&
             settings.maxStepsPerCall == plain.maxStepsPerCall,
          "with no config overrides and no per-level one, settingsFor() gives the profile's own settings");
}

void testSettingsForLevelOverrideUnderVita() {
    BenchConfig config;
    config.profile = "vita";
    // village-dinner is one of the four windmill levels pinned at 100 velocity iterations (PhysicsProfile.cpp);
    // a plain level gets the vita profile's own settings instead.
    const mb::PhysicsSettings windmill = settingsFor(config, "village-dinner");
    const mb::PhysicsSettings plain = settingsFor(config, "knight-garden");
    check(windmill.velocitySteps == 100, "the windmill level's override is picked up, any case normalised inside it");
    check(settingsFor(config, "VILLAGE-DINNER").velocitySteps == 100, "...matched without regard to case");
    check(plain.velocitySteps == mb::physicsProfile("vita")->settings.velocitySteps,
          "a level with no override gets the vita profile's own settings");
}

void testSettingsForConfigOverridesWinOverTheProfile() {
    BenchConfig config;
    config.profile = "vita";
    config.velocitySteps = 7;
    config.positionSteps = 3;
    config.collisionSteps = 2;
    config.threads = 1;
    // Even the windmill override (100 velocity iterations) is replaced: the config's explicit values are
    // layered on top of whatever the profile (and its per-level override) would otherwise give.
    const mb::PhysicsSettings settings = settingsFor(config, "village-dinner");
    check(settings.velocitySteps == 7, "velocitySteps: the config wins over the per-level override");
    check(settings.positionSteps == 3, "positionSteps: the config wins");
    check(settings.collisionSteps == 2, "collisionSteps: the config wins");
    check(settings.threads == 1, "threads: the config wins");
    // Whatever the config does not mention stays the profile's (fixedStep, maxStepsPerCall, the world sizes).
    check(settings.fixedStep == mb::physicsProfile("vita")->settings.fixedStep,
          "a setting the config does not mention keeps the profile's value");
}

// ---------------------------------------------------------------- csvHeader / csvLine

void testCsvHeaderHasEveryColumn() {
    const std::string header = csvHeader();
    for (const char* column : {"platform", "label", "level", "profile", "velocitySteps", "positionSteps",
                               "collisionSteps", "threads", "simulatedSeconds", "steps", "endedBy", "loadMs", "buildMs",
                               "stepMeanMs", "stepP95Ms", "stepP99Ms", "stepMaxMs", "bodies", "peakAwake", "meanAwake",
                               "droppedSteps", "tempAllocatorPeakBytes", "score", "wallSeconds"}) {
        check(header.find(column) != std::string::npos, (std::string("header has ") + column).c_str());
    }
    check(csvHeader() == csvHeader(), "csvHeader() is the same every time it is called");
}

void testCsvLineFormatting() {
    BenchConfig config;
    config.profile = "vita";
    config.label = R"(has "quotes" in it)";
    LevelResult result;
    result.level = "knight-garden";
    result.simulatedSeconds = 19.95;
    result.steps = 1197;
    result.endedBy = "time";
    result.loadMs = 9.7;
    result.buildMs = 2.87123;  // more than 3 decimals: csvLine must round, not truncate or print as given
    result.stepMeanMs = 0.0211;
    result.stepP95Ms = 0.092;
    result.stepP99Ms = 0.11;
    result.stepMaxMs = 0.1101;
    result.bodies = 257;
    result.peakAwake = 2;
    result.meanAwake = 0.971;
    result.droppedSteps = 0;
    result.tempAllocatorPeakBytes = 2138480;
    result.score = 1234;
    result.wallSeconds = 0.015;

    const std::string line = csvLine("host", config, result);
    check(line.find(R"("has ""quotes"" in it")") != std::string::npos, "an inner quote in a quoted field is doubled");
    check(line.find("\"host\"") != std::string::npos, "the platform is quoted");
    check(line.find("\"knight-garden\"") != std::string::npos, "the level is quoted");
    check(line.find("\"vita\"") != std::string::npos, "the profile is quoted");
    check(line.find("\"time\"") != std::string::npos, "endedBy is quoted");
    check(line.find(",1197,") != std::string::npos, "an integer column is printed plain");
    check(line.find("19.95") != std::string::npos, "simulatedSeconds keeps 2 decimals");
    check(line.find("0.97") != std::string::npos, "meanAwake keeps 2 decimals");
    check(line.find("9.700") != std::string::npos, "loadMs keeps 3 decimals");
    check(line.find("2.871") != std::string::npos, "buildMs is rounded to 3 decimals (2.87123 -> 2.871)");
    check(line.find("0.021") != std::string::npos, "stepMeanMs keeps 3 decimals");
    // The velocity/position/collision/threads columns are the settings this level was actually simulated
    // with (settingsFor(config, result.level)), not whatever the config's own optionals happen to hold.
    const mb::PhysicsSettings settings = settingsFor(config, result.level);
    check(line.find("," + std::to_string(settings.velocitySteps) + ",") != std::string::npos,
          "the velocitySteps column is the level's actual, effective setting");

    // Splitting on commas inside the quoted fields would misparse the row, so count fields the way a CSV
    // reader must: a comma only counts outside a quoted span.
    int fields = 1;
    bool inQuotes = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') {
            inQuotes = !inQuotes;
        } else if (line[i] == ',' && !inQuotes) {
            ++fields;
        }
    }
    int headerFields = 1;
    for (const char c : csvHeader()) {
        headerFields += c == ',';
    }
    check(fields == headerFields, "csvLine() has exactly as many fields as csvHeader()");
}

// ---------------------------------------------------------------- appendCsv

void testAppendCsvWritesHeaderOnce() {
    TempDir dir;
    const fs::path path = dir.path / "out.csv";
    check(appendCsv(path, "line one"), "the first append succeeds (the file is new)");
    check(appendCsv(path, "line two"), "a second append succeeds too");

    std::ifstream in(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    check(text.find(csvHeader()) == 0, "the header is the very first thing written");
    std::size_t firstHeader = text.find(csvHeader());
    std::size_t secondHeader = text.find(csvHeader(), firstHeader + 1);
    check(secondHeader == std::string::npos, "the header is written only once, not once per line");
    check(text.find("line one") != std::string::npos && text.find("line two") != std::string::npos,
          "both lines made it into the file");
}

void testAppendCsvHeaderOnceMoreForAnEmptyExistingFile() {
    TempDir dir;
    const fs::path path = dir.path / "out.csv";
    { std::ofstream(path, std::ios::binary); }  // an existing, empty file (as an interrupted run might leave)
    check(appendCsv(path, "line one"), "appending to an existing empty file succeeds");
    std::ifstream in(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    check(text.find(csvHeader()) == 0, "an empty existing file still gets the header, same as a brand new one");
}

}  // namespace

int main() {
    testDefaults();
    testEveryKey();
    testWrongTypesAreErrors();
    testUnknownProfileNamesTheKnownOnes();
    testMissingFileIsAnError();
    testSettingsForWithoutOverrides();
    testSettingsForLevelOverrideUnderVita();
    testSettingsForConfigOverridesWinOverTheProfile();
    testCsvHeaderHasEveryColumn();
    testCsvLineFormatting();
    testAppendCsvWritesHeaderOnce();
    testAppendCsvHeaderOnceMoreForAnEmptyExistingFile();
    if (failures > 0) {
        std::fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    std::puts("bench tests passed");
    return 0;
}
