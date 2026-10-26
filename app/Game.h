#pragma once

// The game as an application: the main menu on a window, levels played from it with sound, and what the player keeps
// saved. main.cpp's developer harness (the level viewer, the form viewer, screenshots, the sound tests) calls runGame()
// for a plain `MuggleBall3` run; the PlayStation Vita's own entry point calls nothing else (docs/VITA_PLAN.md, M5a/M5b).
// Nothing here knows about the harness's options.

#include "PlaySession.h"
#include "mb/game/PhysicsProfile.h"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace mb_app {

// The window the game opens when there is no saved size yet (the original's 4:3 layout).
inline constexpr int kDefaultWindowWidth = 1024;
inline constexpr int kDefaultWindowHeight = 768;

struct WindowSize {
    int width = kDefaultWindowWidth;
    int height = kDefaultWindowHeight;
};

struct GameOptions {
    std::string level;     // a campaign level to open on, in any case; empty opens the main menu
    std::string language;  // a language of the string table; empty keeps the game's default
    // The Autopilot plays: no save data is read or written, and the pointer stays the player's (nobody plays with it).
    bool autoplay = false;
    std::vector<PrizeCue> prizes;    // given to every level that is played (--drop-prize, --catch-prize)
    std::vector<EffectCue> effects;  // likewise (--effect)
    // Every level's PhysicsSettings; the platform's own profile unless the command line says otherwise.
    mb::PhysicsProfile physicsProfile = *mb::physicsProfile(mb::defaultPhysicsProfileName());
    // A window size in points to open with instead of the saved one (--size). A size given here is not saved back.
    std::optional<WindowSize> windowSize;
};

// Runs the game until the player quits: the save data (none with autoplay), the window at the saved or the given
// size and the saved fullscreen state, the renderer, the audio, the front end; the frame statistics are logged
// every few seconds and at the end, and the window's state is written back to the settings. Returns the process
// exit code. Throws what the engine throws when the window, the renderer or the data cannot be had; main() reports it.
int runGame(const GameOptions& options);

// Average time per frame, and how much of it `work` took (the physics, the update of the rules), logged every few
// seconds and when the window closes. The game's loop feeds it; the Vita's benchmark modes build on it (M6b).
class FrameStats {
public:
    explicit FrameStats(const char* work) : work_(work) {}

    void work(double ms) { workMs_ += ms; }
    // Call once per presented frame; the first call starts the clock.
    void frame();
    // Logs the averages since the last report and starts over.
    void report();

private:
    using Clock = std::chrono::steady_clock;
    static constexpr double kReportMs = 5000;
    const char* work_;
    Clock::time_point last_{};
    double totalMs_ = 0;
    double workMs_ = 0;
    int frames_ = 0;
};

}  // namespace mb_app
