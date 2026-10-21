// Plays the whole front end in a real window with the real game data in $MB3_ASSETS: a second thread clicks and types
// through SDL's event queue (main menu, stage selection, a level, pause, back, Exit) while runFrontEnd runs, and a
// stand-in for the scene code counts what the front end asks of it. Needs a window system and a GPU, and skips (exit
// code 77) without them or without the data. The window is on screen for a few seconds.

#include "FrontEnd.h"
#include "RealData.h"
#include "mb/engine/Window.h"
#include "mb/game/UiLayout.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

std::atomic<int> failures{0};

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

// Stands in for the scene code: no scene, but it counts the calls and draws the overlay on a black window.
class FakeSession : public mb_app::LevelSession {
public:
    bool start(std::string_view levelName, const Progress& progress) override {
        progress(0.5f);
        {
            std::lock_guard lock(mutex);
            started = levelName;
        }
        ++starts;
        return true;
    }
    void update(const mb::InputEvents&, double, mb::GameHud& hud) override {
        hud.score += 10;
        ++updates;
    }
    void render(mb::Renderer& renderer, std::span<const mb::Sprite> overlay) override {
        overlaySprites = overlay.size();
        ++renders;
        renderer.renderToWindow({}, {}, {}, overlay);
    }

    std::atomic<int> starts{0}, updates{0}, renders{0};
    std::atomic<std::size_t> overlaySprites{0};
    std::mutex mutex;
    std::string started;
};

void push(SDL_Event e) {
    SDL_PushEvent(&e);
}

void click(mb::Vec2 at) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = at.x;
    e.motion.y = at.y;
    push(e);
    e = {};
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.down = true;
    e.button.x = at.x;
    e.button.y = at.y;
    push(e);
    e.type = SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.down = false;
    push(e);
}

void key(SDL_Keycode code) {
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = code;
    e.key.down = true;
    push(e);
}

template <class Condition>
bool waitFor(Condition condition, std::chrono::milliseconds limit = 5000ms) {
    const auto end = std::chrono::steady_clock::now() + limit;
    while (!condition()) {
        if (std::chrono::steady_clock::now() > end) {
            return false;
        }
        std::this_thread::sleep_for(10ms);
    }
    return true;
}

// Where a widget of a form is, in the window's points, for a window of this size.
mb::Vec2 centerOf(const fs::path& root, const mb::StringTable& strings, realdata::ImageSizes& sizes, const char* form, const char* widget, int width, int height) {
    mb::UiForm loaded = realdata::loadForm(root, strings, form);
    loaded.layout(mb::UiLayout::fit(width, height), [&](std::string_view name) { return sizes(name); });
    const mb::UiWidget* w = loaded.find(widget);
    check(w != nullptr, std::string(form) + " has " + widget);
    return w ? w->rect.center() : mb::Vec2{};
}

}  // namespace

int main() {
    const char* env = std::getenv("MB3_ASSETS");
    if (!env || !*env) {
        std::puts("SKIP: MB3_ASSETS is not set");
        return 77;
    }
    const fs::path root = env;
    if (!fs::is_regular_file(root / "Data" / "xml" / "levels.json") || !fs::is_directory(root / "Data" / "Menu")) {
        std::printf("SKIP: converted data not found under %s (run tools/convert_assets.py)\n", root.string().c_str());
        return 77;
    }

    std::unique_ptr<mb::Window> window;
    std::unique_ptr<mb::Renderer> renderer;
    try {
        window = std::make_unique<mb::Window>("Muggle Ball 3 front end test", 1024, 768);
        renderer = std::make_unique<mb::Renderer>(*window);
    } catch (const std::exception& e) {
        std::printf("SKIP: no window or GPU (%s)\n", e.what());
        return 77;
    }
    mb::InputEvents ignored;
    window->pollEvents(ignored);  // the window system may adjust the window before it is first shown
    int points = 0, pointsHigh = 0;
    SDL_GetWindowSize(window->handle(), &points, &pointsHigh);

    try {
        const mb::StringTable strings = mb::StringTable::load(root / "Data" / "xml" / "languages.json");
        realdata::ImageSizes sizes(root);
        const mb::Vec2 start = centerOf(root, strings, sizes, "MainMenu", "SelectStage", points, pointsHigh);
        const mb::Vec2 knights = centerOf(root, strings, sizes, "selectstage", "StageKnights", points, pointsHigh);
        const mb::Vec2 level = centerOf(root, strings, sizes, "stageknights", "70", points, pointsHigh);
        const mb::Vec2 exit = centerOf(root, strings, sizes, "MainMenu", "Exit", points, pointsHigh);

        FakeSession session;
        std::atomic<int> hoverSounds{0}, downSounds{0};
        mb_app::FrontEndOptions options;
        options.hooks.playSound = [&](std::string_view name) {
            hoverSounds += name == "menustonein";
            downSounds += name == "menustonedown";
        };

        std::atomic<bool> finished{false};
        std::atomic<int> updatesAtPause{-1}, updatesWhilePaused{-1}, updatesResumed{-1};
        std::thread player([&] {
            std::this_thread::sleep_for(800ms);
            click(start);
            std::this_thread::sleep_for(300ms);
            click(knights);
            std::this_thread::sleep_for(300ms);
            click(level);
            check(waitFor([&] { return session.starts == 1; }), "the level was built after a click on its button");
            check(waitFor([&] { return session.updates > 5; }), "the level plays");

            key(SDLK_ESCAPE);
            std::this_thread::sleep_for(300ms);
            updatesAtPause = session.updates.load();
            std::this_thread::sleep_for(300ms);
            updatesWhilePaused = session.updates.load();
            key(SDLK_SPACE);
            std::this_thread::sleep_for(300ms);
            updatesResumed = session.updates.load();

            key(SDLK_ESCAPE);
            std::this_thread::sleep_for(200ms);
            key(SDLK_ESCAPE);
            std::this_thread::sleep_for(500ms);
            click(exit);
            if (!waitFor([&] { return finished.load(); }, 3000ms)) {
                check(false, "Exit closed the window");
                SDL_Event quit{};
                quit.type = SDL_EVENT_QUIT;
                push(quit);
            }
        });

        const int result = mb_app::runFrontEnd(*window, *renderer, session, options);
        finished = true;
        player.join();

        check(result == 0, "runFrontEnd returns 0");
        {
            std::lock_guard lock(session.mutex);
            check(session.started == "knight-miss-1457", "the level started was knight-miss-1457, the eighth of the knights (button 70), not " + session.started);
        }
        check(session.starts == 1, "the level was built exactly once");
        check(updatesAtPause >= 0 && updatesWhilePaused == updatesAtPause, "the level stops updating while paused");
        check(updatesResumed > updatesWhilePaused, "any key resumes it");
        check(session.renders > 10 && session.overlaySprites > 20, "the level was drawn with the HUD (three hearts and two lines of text) over it");
        check(hoverSounds > 0 && downSounds > 0, "the menu sounds were asked for through the hook");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: exception: %s\n", e.what());
        return 1;
    }
    if (failures == 0) {
        std::puts("the front end played through");
    }
    return failures == 0 ? 0 : 1;
}
