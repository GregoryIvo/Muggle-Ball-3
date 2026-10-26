#pragma once

// The physics benchmark (docs/VITA_PLAN.md, M3b): plays levels with the Autopilot in fixed 1/60 s steps as fast as
// the machine can, timing every step, and writes one CSV line per level. The same code runs on the desktop
// (app/bench/main.cpp, the `mb_physics_bench` target) and on the PlayStation Vita (platform/vita/bench, title ID
// MUGB00004), so the per-level ratio of the two is the measured speed of the console against the Mac, which the
// plan's estimates (section 4.4) are replaced with. Nothing here draws the level: the bench measures the simulation
// and the rules only, which is what the frame budget of section 1 is about.

#include "Assets.h"
#include "mb/game/PhysicsProfile.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mb_app::bench {

// What one run measures, read from bench-physics.json:
//   {"levels": ["knight-miss-1457", ...],   required: level ids (the file stems in Data/Levels), any case
//    "seconds": 20,                          simulated seconds per level (default 20)
//    "profile": "vita",                      the PhysicsProfile the settings start from (default "desktop")
//    "velocitySteps": 100, "positionSteps": 1, "collisionSteps": 1, "threads": 0,   each optional: when given,
//                                            it replaces the profile's value (and its per-level override's)
//    "label": "pass1"}                       free text copied into every CSV line (default "")
// Unknown keys are ignored, so a file can carry notes.
struct BenchConfig {
    std::vector<std::string> levels;
    double seconds = 20;
    std::string profile = "desktop";
    std::optional<int> velocitySteps;
    std::optional<int> positionSteps;
    std::optional<int> collisionSteps;
    std::optional<int> threads;
    std::string label;
};

// Reads the file; nothing, with `error` saying why, if it cannot be read, is not JSON, names an unknown profile,
// has no levels, or has a value of the wrong type.
std::optional<BenchConfig> loadBenchConfig(const std::filesystem::path& path, std::string& error);

// The settings a level is simulated with under this config: the profile's (with the level's override), then the
// config's explicit values on top.
mb::PhysicsSettings settingsFor(const BenchConfig& config, std::string_view levelId);

// What one level's run produced. Times are milliseconds of wall-clock on the machine running the bench.
struct LevelResult {
    std::string level;              // the id as the catalogue has it
    double simulatedSeconds = 0;    // how long the Autopilot played (less than asked when the level ended)
    int steps = 0;                  // fixed steps taken
    std::string endedBy;            // "time", "complete", "over" or "stopped"
    bool stopRequested = false;     // a Progress callback returned false at some point (see Progress below);
                                     // independent of endedBy, so runAll can stop the whole run on it even when
                                     // this level's own endedBy stayed "time", "complete" or "over"
    double loadMs = 0;              // reading and parsing the level's JSON
    double buildMs = 0;             // the GameSession's construction: the physics world and the bodies
    // Per fixed step: one GameSession::update() of exactly one step, which is the physics step plus the rules
    // (the bat, the ball, the hits), what a frame spends on the simulation.
    double stepMeanMs = 0;
    double stepP95Ms = 0;
    double stepP99Ms = 0;
    double stepMaxMs = 0;
    int bodies = 0;                 // PhysicsWorld::bodyCount() after the build
    int peakAwake = 0;              // the most awake bodies seen after a step
    double meanAwake = 0;           // averaged over the steps
    std::uint64_t droppedSteps = 0; // GameSession::droppedSteps() at the end (zero: the bench steps one at a time)
    std::size_t tempAllocatorPeakBytes = 0;
    std::int64_t score = 0;
    double wallSeconds = 0;         // the whole run of this level, loads included
};

// Called every so often while a level runs (about ten times a second of wall-clock, and at the end): steps done of
// the steps planned, for a progress display. Returning false asks the run to stop: LevelResult.stopRequested
// becomes true either way, and if the level still had steps left, endedBy becomes "stopped" too. A level that was
// already finishing on that very call (its last planned step, or just reaching Complete/GameOver) keeps its true
// endedBy ("time", "complete" or "over") instead -- stopRequested is what a caller checks to learn a stop was
// asked, so it is never mislabelled as an early stop, or lost because it landed on the same call as the end.
using Progress = std::function<bool(int stepsDone, int stepsPlanned)>;

// Plays one level with the Autopilot: loads it from `assets`, builds a GameSession with settingsFor(), then steps
// it in fixed 1/60 s frames (Autopilot::decide, GameSession::update of exactly one step) until the simulated
// seconds are up, the level is complete or the game is over, timing each update with steady_clock on the host and
// the kernel's own process-time counter on the Vita (PhysicsBench.cpp: steady_clock there resolves to
// gettimeofday under this SDK's libstdc++, which has no CLOCK_MONOTONIC, so it is a wall clock, not guaranteed
// monotonic). Hearts are plenty (99), as in the gameplay smoke, so a lost ball does not end the run. Nothing (and
// a log line) if the level is not in the catalogue or cannot be loaded.
std::optional<LevelResult> runLevel(const Assets& assets, const BenchConfig& config, std::string_view levelId,
                                    const Progress& progress = {});

// The CSV. One header line, then one line per level, every value quoted or numeric, so a spreadsheet and
// tools/bench_ratio.py read it. Appended to, so an interrupted run keeps its data; the header is written only
// when the file is new or empty.
//   platform,label,level,profile,velocitySteps,positionSteps,collisionSteps,threads,simulatedSeconds,steps,endedBy,
//   loadMs,buildMs,stepMeanMs,stepP95Ms,stepP99Ms,stepMaxMs,bodies,peakAwake,meanAwake,droppedSteps,
//   tempAllocatorPeakBytes,score,wallSeconds
std::string csvHeader();
std::string csvLine(std::string_view platform, const BenchConfig& config, const LevelResult& result);
// Appends `line` (and the header first when needed); false, with a log line, if the file cannot be written.
bool appendCsv(const std::filesystem::path& path, const std::string& line);

// Runs every level of the config in order, appending to the CSV as each finishes, with `progress` as above
// (stepsDone/stepsPlanned are the current level's). A level whose CSV line could not be written does not count
// as done and does not reach `onLevel` either, so a caller's count and summary never claim a level landed
// anywhere when it did not; checked once, before the first level, so an unwritable path fails fast rather than
// after every level has already run. Stops the whole run (not just the current level) as soon as a level's
// result carries stopRequested. Returns how many levels produced a line.
int runAll(const Assets& assets, const BenchConfig& config, std::string_view platform, const std::filesystem::path& csv,
           const Progress& progress = {}, const std::function<void(const LevelResult&)>& onLevel = {});

}  // namespace mb_app::bench
