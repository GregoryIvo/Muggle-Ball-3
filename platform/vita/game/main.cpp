// The PlayStation Vita's own entry point for the game (M5b): sets the clocks, opens the log, checks the
// asset pack is where it should be, then hands straight off to mb_app::runGame(), exactly as the desktop's
// main.cpp does at its "the game itself" call (app/Game.cpp, M5a) but with none of that file's 767-line
// developer harness around it (the level viewer, screenshots, the sound tests: none of it ships to the
// console). Window.cpp (engine/src/Window.cpp, MB3_PLATFORM_VITA) does the rest of what makes this a Vita
// build rather than a desktop one: the SDL hints, the joystick and touch, the enter-button setting, and the
// vitaGL renderer picked up through the usual Renderer seam.
#include "Game.h"
#include "mb/engine/Log.h"
#include "mb/engine/Paths.h"

#include <SDL3/SDL.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>

#include <exception>
#include <filesystem>
#include <system_error>

namespace {

constexpr const char* kLogPath = "ux0:data/MuggleBall3/log.txt";

// The hello's clocks (docs/VITA.md): 444 MHz ARM, 222 MHz bus and GPU, 166 MHz the GPU's crossbar.
void setClocks() {
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    mb::logInfo("clocks: arm {} MHz, bus {}, gpu {}, xbar {}", scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(),
                scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency());
}

// mb::assetRoot() already prefers app0:/assets (a personal MB3_VITA_BUNDLE_ASSETS build) over
// ux0:data/MuggleBall3/assets (the development copy tools/vita_pack.py fills in); this just says plainly
// whether either one actually has the game's data, since a silent empty campaign is a confusing first run.
void checkAssets() {
    const std::filesystem::path root = mb::assetRoot();
    mb::logInfo("Asset root: {}", root.string());
    std::error_code ec;
    if (!std::filesystem::exists(root / "Data" / "Levels", ec)) {
        mb::logError(
            "no Data/Levels under {} (checked app0:/assets and ux0:data/MuggleBall3/assets): copy the converted "
            "asset pack there with tools/vita_pack.py before the game has anything to play",
            root.string());
    }
}

}  // namespace

int main(int, char**) {
    setClocks();
    mb::setLogFile(kLogPath);
    mb::logInfo("Muggle Ball 3 (Vita). SDL {}.{}.{} compiled, {}.{}.{} linked", SDL_MAJOR_VERSION, SDL_MINOR_VERSION,
                SDL_MICRO_VERSION, SDL_VERSIONNUM_MAJOR(SDL_GetVersion()), SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
                SDL_VERSIONNUM_MICRO(SDL_GetVersion()));
    // vitaGL has no version of its own to query; its GL_VERSION/GL_RENDERER/GLSL string (the pinned commit is
    // in docs/VITA.md) is logged by the renderer's own start-up, right after this, when runGame() opens the
    // window and the GL back end's Platform::init runs.
    checkAssets();

    int exitCode = 1;
    try {
        exitCode = mb_app::runGame(mb_app::GameOptions{});  // the main menu, the "vita" physics profile, no autoplay
    } catch (const std::exception& e) {
        mb::logError("uncaught exception: {}", e.what());
    }
    mb::logInfo("exiting ({})", exitCode);
    sceKernelExitProcess(exitCode);
    return exitCode;
}
