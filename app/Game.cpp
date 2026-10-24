#include "Game.h"

#include "Assets.h"
#include "FrontEnd.h"
#include "FrontEndAudio.h"
#include "GameAudio.h"
#include "PlaySession.h"
#include "mb/engine/Log.h"
#include "mb/engine/Paths.h"
#include "mb/engine/Renderer.h"
#include "mb/engine/Window.h"
#include "mb/game/SaveData.h"

#include <filesystem>
#include <utility>

namespace mb_app {

namespace {

namespace fs = std::filesystem;

// The player's files: options, progress and scores, in the folder mb::userDataDir() names. --autoplay plays on
// nobody's behalf, so it neither reads nor writes them: it runs on the defaults, kept in memory (a SaveData with
// no folder saves nothing), and what it remembers is gone with the process.
mb::SaveData openSave(const GameOptions& options) {
    return mb::SaveData(options.autoplay ? fs::path() : mb::userDataDir());
}

// The window's size in points: the one given in options.windowSize, else the one the player left it at.
void windowSize(const GameOptions& options, const mb::SaveData& save, int& width, int& height) {
    width = options.windowSize ? options.windowSize->width : save.settings.windowWidth;
    height = options.windowSize ? options.windowSize->height : save.settings.windowHeight;
}

// Remembers how the player left the window. A size given instead of the saved one is not kept.
void saveWindowState(const mb::Window& window, mb::SaveData& save, bool keepSize) {
    save.settings.fullscreen = window.fullscreen();
    if (keepSize) {
        window.windowedSize(save.settings.windowWidth, save.settings.windowHeight);
    }
    save.saveSettings();
}

}  // namespace

void FrameStats::frame() {
    const Clock::time_point now = Clock::now();
    if (last_ != Clock::time_point{}) {
        totalMs_ += std::chrono::duration<double, std::milli>(now - last_).count();
        ++frames_;
    }
    last_ = now;
    if (totalMs_ >= kReportMs) {
        report();
    }
}

void FrameStats::report() {
    if (frames_ > 0) {
        mb::logInfo("average frame time {:.2f} ms ({:.0f} fps) over {} frames, {} {:.2f} ms", totalMs_ / frames_,
                    1000.0 * frames_ / totalMs_, frames_, work_, workMs_ / frames_);
    }
    totalMs_ = 0;
    workMs_ = 0;
    frames_ = 0;
}

// The game: the main menu on a window, levels played from it, with sound, and what the player keeps saved.
int runGame(const GameOptions& options) {
    Assets assets;
    mb::SaveData save = openSave(options);
    mb::logInfo("Save data: {}", options.autoplay ? "none (--autoplay)" : save.directory().string());
    int windowWidth = 0, windowHeight = 0;
    windowSize(options, save, windowWidth, windowHeight);
    mb::Window window("Muggle Ball 3", windowWidth, windowHeight, save.settings.fullscreen);
    mb::Renderer renderer(window);
    mb::logInfo("GPU backend: {}", renderer.backendName());
    mb::logInfo("Asset root: {}", assets.root.string());

    mb::GameAudio audio(assets.root);  // after the window, so the device closes before SDL quits
    PlaySession session(renderer, assets, save, &audio,
                        {.autoplay = options.autoplay, .prizes = options.prizes, .effects = options.effects,
                         .profile = options.physicsProfile});
    FrontEndOptions frontEnd;
    frontEnd.language = options.language;
    frontEnd.startLevel = options.level;
    frontEnd.hideCursor = !options.autoplay;  // nobody plays with the pointer
    connectAudio(frontEnd, audio);
    FrameStats stats("update");
    frontEnd.events.frame = [&, audioFrame = std::move(frontEnd.events.frame)] {
        audioFrame();
        stats.work(session.takeUpdateMillis());
        stats.frame();
    };
    const int result = runFrontEnd(window, renderer, session, save, frontEnd);
    stats.report();
    saveWindowState(window, save, !options.windowSize);
    return result;
}

}  // namespace mb_app
