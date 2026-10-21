// Muggle Ball 3: entry point.
//
//   MuggleBall3                     open the game window
//   MuggleBall3 --check <level>     load a converted level JSON, print a summary, exit

#include "mb/engine/Log.h"
#include "mb/engine/Paths.h"
#include "mb/engine/Renderer.h"
#include "mb/engine/Window.h"
#include "mb/game/LevelLoader.h"

#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

#include <exception>
#include <string_view>

namespace {

int checkLevel(const char* path) {
    const mb::Level level = mb::loadLevelJson(path);
    mb::logInfo("{}: {} actors, {} paths", path, level.actors.size(), level.paths.size());
    for (int t = 1; t <= 26; ++t) {
        const auto type = static_cast<mb::ObjectType>(t);
        if (const std::size_t n = level.countObjects(type)) {
            mb::logInfo("  {:<14} {}", mb::objectTypeName(type), n);
        }
    }
    return 0;
}

int runGame() {
    mb::Window window("Muggle Ball 3", 1024, 768);
    mb::Renderer renderer(window);
    mb::logInfo("GPU backend: {}", renderer.backendName());
    mb::logInfo("Asset root: {}", mb::assetRoot().string());

    while (window.pollEvents()) {
        mb::FrameParams frame;
        frame.clear = {0.05f, 0.18f, 0.45f, 1.0f};
        renderer.renderToWindow(frame, {});
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    SDL_SetMainReady();
    try {
        if (argc >= 3 && std::string_view(argv[1]) == "--check") {
            return checkLevel(argv[2]);
        }
        return runGame();
    } catch (const std::exception& e) {
        mb::logError("fatal: {}", e.what());
        return 1;
    }
}
