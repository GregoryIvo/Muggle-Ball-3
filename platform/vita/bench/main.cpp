// The physics benchmark VPK for the PlayStation Vita (docs/VITA_PLAN.md M3b, title ID MUGB00004): runs
// bench/PhysicsBench.cpp's runAll over ux0:data/MuggleBall3/bench-physics.json with the Autopilot, in fixed
// 1/60 s steps, as fast as the device can (not in real time), and appends one CSV line per level to
// ux0:data/MuggleBall3/bench-physics.csv, so the same JSON run on the Mac (app/bench, mb_physics_bench) and
// here line up level by level (tools/bench_ratio.py). It draws its progress with SDL's own GXM renderer
// (SDL_render_vita_gxm): this VPK never touches vitaGL, because nothing here needs a picture of the game,
// only the simulation and the rules.

#include "Assets.h"
#include "PhysicsBench.h"

#include "mb/engine/Log.h"

#include <SDL3/SDL.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Same values and comment as platform/vita/hello/main.cpp: newlib's heap is one fixed block taken at
// start-up, and thread stacks (the default pthread one included; without it a std::thread gets a 4 KiB
// stack, which corrupts) come from outside it. A shared file for the three of them arrives with a later
// milestone; defined here for now, the way the hello does.
extern "C" {
unsigned int _newlib_heap_size_user = 160 * 1024 * 1024;
unsigned int sceUserMainThreadStackSize = 1024 * 1024;
unsigned int _pthread_stack_default_user = 512 * 1024;
}

namespace {

namespace fs = std::filesystem;
using namespace mb_app::bench;
using clock_type = std::chrono::steady_clock;

constexpr int kScreenWidth = 960;
constexpr int kScreenHeight = 544;
constexpr float kTextScale = 2.0f;  // SDL_RenderDebugText's 8x8 glyphs, doubled so they read at arm's length
// In the coordinates SDL_RenderDebugText takes: SDL_SetRenderScale already multiplies up what it draws, so
// a margin or a line spacing chosen here is in the same units whatever kTextScale is; 10 leaves a couple of
// pixels of breathing room below the glyph's own 8.
constexpr float kMarginX = 4.0f;
constexpr float kLineHeight = 10.0f;
constexpr const char* kConfigPath = "ux0:data/MuggleBall3/bench-physics.json";
constexpr const char* kCsvPath = "ux0:data/MuggleBall3/bench-physics.csv";
constexpr const char* kLogPath = "ux0:data/MuggleBall3/bench.log";

void setClocks() {
    const int rcArm = scePowerSetArmClockFrequency(444);
    const int rcBus = scePowerSetBusClockFrequency(222);
    const int rcGpu = scePowerSetGpuClockFrequency(222);
    const int rcXbar = scePowerSetGpuXbarClockFrequency(166);
    mb::logInfo("bench: clocks set rc 0x{:08x} 0x{:08x} 0x{:08x} 0x{:08x}; now arm {} MHz, bus {}, gpu {}, xbar {}",
               static_cast<unsigned>(rcArm), static_cast<unsigned>(rcBus), static_cast<unsigned>(rcGpu),
               static_cast<unsigned>(rcXbar), scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(),
               scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency());
}

// Whether a button went down since the last call (and drains the event queue either way, so SDL keeps
// pumping and the console does not decide the app has stopped responding and put itself to sleep).
bool pollButtonDown() {
    bool pressed = false;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN) {
            pressed = true;
        }
    }
    return pressed;
}

void drawLines(SDL_Renderer* renderer, const std::vector<std::string>& lines) {
    SDL_SetRenderDrawColor(renderer, 13, 13, 26, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255);
    SDL_SetRenderScale(renderer, kTextScale, kTextScale);
    float y = kMarginX;
    for (const std::string& line : lines) {
        SDL_RenderDebugText(renderer, kMarginX, y, line.c_str());
        y += kLineHeight;
    }
    SDL_SetRenderScale(renderer, 1, 1);
    SDL_RenderPresent(renderer);
}

// Draws `lines` and pumps events until a button is pressed (used before the run, when the JSON or the data
// is missing, and after it, to show the finished table).
void waitForButton(SDL_Renderer* renderer, const std::vector<std::string>& lines) {
    while (!pollButtonDown()) {
        drawLines(renderer, lines);
    }
}

std::string secondsText(clock_type::time_point since) {
    return std::format("{:.0f} s elapsed", std::chrono::duration<double>(clock_type::now() - since).count());
}

// ASCII-lowercase, matching PhysicsBench.cpp's own catalogue lookup (LevelCatalog, PhysicsProfile): used only
// to pre-check bench-physics.json's level ids against the catalogue before runAll, below.
std::string lowerAscii(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool levelInCatalog(const mb_app::Assets& assets, std::string_view levelId) {
    const std::string wanted = lowerAscii(levelId);
    return std::ranges::any_of(assets.catalog.levels(),
                               [&](const mb::LevelEntry& entry) { return lowerAscii(entry.name) == wanted; });
}

// Clock sanity, logged once before any level runs: PhysicsBench.cpp's own step timer is the kernel's
// process-time counter on this build (sceKernelGetProcessTimeWide, confirmed monotonic by
// platform/vita/hello/main.cpp's probe), not steady_clock -- steady_clock resolves to gettimeofday under
// this SDK's libstdc++ (no CLOCK_MONOTONIC), a wall clock of unverified resolution on real hardware. Logged
// here, against the same 50 ms delay the hello's own probe uses, so bench.log validates its own timings
// instead of taking PhysicsBench.h's word for it; neither number here is used by the bench itself.
void probeClocks() {
    using steady = std::chrono::steady_clock;
    mb::logInfo("bench: steady_clock period {}/{}", static_cast<long long>(steady::period::num),
               static_cast<long long>(steady::period::den));
    const std::uint64_t k0 = sceKernelGetProcessTimeWide();
    const auto s0 = steady::now();
    sceKernelDelayThread(50 * 1000);
    const double kernelMs = (sceKernelGetProcessTimeWide() - k0) / 1000.0;
    const double steadyMs = std::chrono::duration<double, std::milli>(steady::now() - s0).count();
    mb::logInfo("bench: 50 ms delay: process time {:.2f} ms, steady_clock {:.2f} ms", kernelMs, steadyMs);
}

}  // namespace

int main(int, char**) {
    mb::setLogFile(kLogPath);
    setClocks();
    mb::logInfo("bench: heap {} MiB, main stack {} KiB, pthread default stack {} KiB", _newlib_heap_size_user >> 20,
               sceUserMainThreadStackSize >> 10, _pthread_stack_default_user >> 10);
    probeClocks();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK)) {
        mb::logError("bench: SDL_Init failed: {}", SDL_GetError());
        sceKernelExitProcess(1);
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("MB3 Physics Bench", kScreenWidth, kScreenHeight, 0);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!window || !renderer) {
        mb::logError("bench: SDL_CreateWindow/SDL_CreateRenderer failed: {}", SDL_GetError());
        sceKernelExitProcess(1);
        return 1;
    }
    mb::logInfo("bench: SDL video driver '{}', renderer '{}'", SDL_GetCurrentVideoDriver(),
               SDL_GetRendererName(renderer));
    int joystickCount = 0;
    if (SDL_JoystickID* ids = SDL_GetJoysticks(&joystickCount)) {
        if (joystickCount > 0) {
            SDL_OpenJoystick(ids[0]);
        }
        SDL_free(ids);
    }

    int done = 0;
    std::size_t levelCount = 0;
    std::vector<LevelResult> results;
    std::vector<std::string> missing;  // requested level ids not in the catalogue; shown on the final screen

    if (!fs::is_regular_file(kConfigPath)) {
        mb::logError("bench: {} is missing", kConfigPath);
        waitForButton(renderer, {"put bench-physics.json in ux0:data/MuggleBall3/",
                                 "(see platform/vita/bench/bench-physics.pass1.json)", "press a button to exit"});
    } else {
        std::string error;
        const std::optional<BenchConfig> config = loadBenchConfig(kConfigPath, error);
        if (!config) {
            mb::logError("bench: {}", error);
            waitForButton(renderer, {error, "press a button to exit"});
        } else {
            try {
                const mb_app::Assets assets;
                if (assets.catalog.levels().empty()) {
                    mb::logError("bench: the level catalogue under {} is empty", assets.root.string());
                    waitForButton(renderer, {std::format("no levels found under {}", assets.root.string()),
                                             "copy the asset pack there (see docs/VITA.md)", "press a button to exit"});
                } else {
                    // Pre-checked against the catalogue before any run: runAll only calls onLevel for a
                    // level that produced a result, so a missing id (a typo in bench-physics.json, an
                    // incomplete asset pack) would otherwise leave currentIndex -- and the on-screen
                    // "level i/N" -- one behind for every level after it, with no visible sign why. Filtering
                    // here, the same case-insensitive match runLevel itself uses, keeps the index honest and
                    // runs every level that is actually there.
                    BenchConfig toRun = *config;
                    toRun.levels.clear();
                    for (const std::string& id : config->levels) {
                        if (levelInCatalog(assets, id)) {
                            toRun.levels.push_back(id);
                        } else {
                            missing.push_back(id);
                            mb::logError("bench: \"{}\" is not in the level catalogue; skipping it", id);
                        }
                    }

                    if (toRun.levels.empty()) {
                        waitForButton(renderer, {"none of bench-physics.json's levels are in the catalogue",
                                                 "see bench.log for which ids", "press a button to exit"});
                    } else {
                        levelCount = toRun.levels.size();
                        const auto started = clock_type::now();
                        std::size_t currentIndex = 0;
                        bool haveLast = false;
                        LevelResult last;

                        // runAll calls onLevel right after a level finishes and before the next one's first
                        // progress call, so incrementing the index there (not in progress) keeps it matching
                        // the level actually running while every progress call during level 0 still sees
                        // index 0 -- true now because toRun.levels (not config->levels) holds only the
                        // levels runAll will actually be called with.
                        const Progress progress = [&](int stepsDone, int stepsPlanned) {
                            const bool more = pollButtonDown();
                            const std::string name =
                                currentIndex < toRun.levels.size() ? toRun.levels[currentIndex] : "?";
                            std::vector<std::string> lines = {
                                std::format("{}  |  level {}/{}: {}", toRun.label, currentIndex + 1,
                                           toRun.levels.size(), name),
                                std::format("steps {}/{}", stepsDone, stepsPlanned),
                                haveLast ? std::format("last: {} mean {:.3f} ms p95 {:.3f} ms", last.level,
                                                       last.stepMeanMs, last.stepP95Ms)
                                         : std::string("last: (none yet)"),
                                secondsText(started) + "  |  press a button to stop",
                            };
                            drawLines(renderer, lines);
                            return !more;
                        };
                        const auto onLevel = [&](const LevelResult& result) {
                            last = result;
                            haveLast = true;
                            results.push_back(result);
                            ++currentIndex;
                        };

                        done = runAll(assets, toRun, "vita", kCsvPath, progress, onLevel);
                        mb::logInfo("bench: {} of {} levels ran ({} missing from the catalogue)", done, levelCount,
                                   missing.size());
                    }
                }
            } catch (const std::exception& e) {
                mb::logError("bench: {}", e.what());
                waitForButton(renderer, {std::format("error: {}", e.what()), "press a button to exit"});
            }
        }
    }

    if (levelCount > 0) {
        std::vector<std::string> summary;
        summary.push_back(std::format("done: {} of {} levels, see bench-physics.csv", done, levelCount));
        for (const LevelResult& result : results) {
            summary.push_back(std::format("{}: mean {:.3f} ms p95 {:.3f} ms, {} dropped", result.level,
                                          result.stepMeanMs, result.stepP95Ms, result.droppedSteps));
        }
        for (const std::string& id : missing) {
            summary.push_back("skipped (not in catalogue): " + id);
        }
        summary.push_back("press a button to exit");
        waitForButton(renderer, summary);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    mb::logInfo("bench: exiting");
    sceKernelExitProcess(0);
    return 0;
}
