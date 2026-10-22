// Plays the whole front end in a real window with the real game data in $MB3_ASSETS: a second thread clicks and types
// through SDL's event queue (Options, the main menu, stage selection, a level, pause, back, Exit) while runFrontEnd
// runs, and a stand-in for the scene code counts what the front end asks of it. The sound is the real one (the game's
// GameAudio, wired as the game wires it) on SDL's disk audio driver, and the player's files are the real ones too, in a
// folder of their own (MB3_USER_DATA): the test checks that sounds were asked for and music chosen for the menus and the
// level, that the saved volumes were set at the start and follow the slider, that the pause freezes the sound and the
// way back to the menus unfreezes it, and that settings changed in Options are in the files afterwards. Needs a window
// system and a GPU, and skips (exit code 77) without them or without the data. The window is on screen for a few seconds.

#include "FrontEnd.h"
#include "FrontEndAudio.h"
#include "GameAudio.h"
#include "RealData.h"
#include "mb/engine/Files.h"
#include "mb/engine/Paths.h"
#include "mb/engine/Window.h"
#include "mb/game/SaveData.h"
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
#include <random>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

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

// The folder the player's files go to for this test.
class UserData {
public:
    UserData() : path(fs::temp_directory_path() / ("mb3-frontend-smoke-" + std::to_string(std::random_device{}()))) {
        fs::remove_all(path);
        fs::create_directories(path);
        SDL_setenv_unsafe("MB3_USER_DATA", path.string().c_str(), 1);
    }
    ~UserData() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    UserData(const UserData&) = delete;
    UserData& operator=(const UserData&) = delete;

    fs::path path;
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

// Where the "-" or "+" button of the slider is.
mb::Vec2 stepperOf(const fs::path& root, const mb::StringTable& strings, realdata::ImageSizes& sizes, const char* form, const char* slider, const char* which, int width, int height) {
    mb::UiForm loaded = realdata::loadForm(root, strings, form);
    loaded.layout(mb::UiLayout::fit(width, height), [&](std::string_view name) { return sizes(name); });
    const mb::UiWidget* owner = loaded.find(slider);
    check(owner != nullptr, std::string(form) + " has " + slider);
    for (const mb::UiWidget& w : loaded.widgets()) {
        if (owner && w.name == which && w.parent == static_cast<int>(owner - loaded.widgets().data())) {
            return w.rect.center();
        }
    }
    check(false, std::string(slider) + " has a " + which + " button");
    return {};
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

    UserData userData;
    // What the player left behind last time: quieter music than the default, the red bat.
    mb::writeFileAtomic(userData.path / "settings.json",
                        R"({"version": 1, "musicVolume": 0.3, "soundVolume": 0.6, "smartCamera": false, "bat": 0})");

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
        const mb::Vec2 settingsButton = centerOf(root, strings, sizes, "MainMenu", "Options", points, pointsHigh);
        const mb::Vec2 musicUp = stepperOf(root, strings, sizes, "Options", "MusicVolume", "+", points, pointsHigh);
        const mb::Vec2 camera = centerOf(root, strings, sizes, "Options", "Camera", points, pointsHigh);
        const mb::Vec2 back = centerOf(root, strings, sizes, "Options", "ApplyVideo", points, pointsHigh);
        const mb::Vec2 start = centerOf(root, strings, sizes, "MainMenu", "SelectStage", points, pointsHigh);
        const mb::Vec2 blueBat = centerOf(root, strings, sizes, "selectstage", "Metall-Raketka-Blue.jpg", points, pointsHigh);
        const mb::Vec2 knights = centerOf(root, strings, sizes, "selectstage", "StageKnights", points, pointsHigh);
        const mb::Vec2 level = centerOf(root, strings, sizes, "stageknights", "70", points, pointsHigh);
        const mb::Vec2 exit = centerOf(root, strings, sizes, "MainMenu", "Exit", points, pointsHigh);

        // The player's files, and the sound as the game has it: the real GameAudio, wired by the code the game uses,
        // on a disk "audio device" so that it works on a machine without speakers. After the window, as in the game.
        mb::SaveData save(mb::userDataDir());
        check(fs::equivalent(save.directory(), userData.path), "MB3_USER_DATA sets the folder of the player's files");
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "disk");
        SDL_SetHint(SDL_HINT_AUDIO_DISK_OUTPUT_FILE, (userData.path / "audio.raw").string().c_str());
        mb::GameAudio audio(root);
        check(audio.device().available() && audio.sounds() != nullptr, "the sound opened: a device and the game's sound list");
        mb::Mixer& mixer = audio.device().mixer();

        FakeSession session;
        std::atomic<int> hoverSounds{0}, downSounds{0};
        mb_app::FrontEndOptions options;
        options.lockLevels = false;
        mb_app::connectAudio(options, audio);

        // Watch what the front end tells the sound, on the front end's own thread, and let every call through.
        std::vector<std::string> events;  // written by the main thread only, read after it is done
        const auto music = [&] { return audio.sounds() ? audio.sounds()->music() : std::string("?"); };
        const auto flag = [](bool on) { return std::string(on ? "1" : "0"); };
        float musicAtStart = -1, soundAtStart = -1;
        {
            const auto playSound = options.hooks.playSound;
            options.hooks.playSound = [&, playSound](std::string_view name) {
                hoverSounds += name == "menustonein";
                downSounds += name == "menustonedown";
                playSound(name);
            };
            const auto menuShown = options.events.menuShown;
            options.events.menuShown = [&, menuShown] {
                menuShown();
                if (events.empty()) {
                    musicAtStart = mixer.musicVolume();
                    soundAtStart = mixer.sfxVolume();
                }
                events.push_back("menu music=" + music() + " paused=" + flag(mixer.paused()));
            };
            const auto levelStarted = options.events.levelStarted;
            options.events.levelStarted = [&, levelStarted](std::string_view name) {
                levelStarted(name);
                events.push_back("level " + std::string(name) + " music=" + music());
            };
            const auto paused = options.events.paused;
            options.events.paused = [&, paused](bool on) {
                paused(on);
                events.push_back("pause " + flag(on) + " mixer paused=" + flag(mixer.paused()));
            };
        }

        std::atomic<bool> finished{false};
        std::atomic<int> peakVoices{0}, updatesAtPause{-1}, updatesWhilePaused{-1}, updatesResumed{-1};
        std::atomic<bool> musicFollowedSlider{false}, settingsWritten{false};
        std::thread sampler([&] {  // a click's sound is a voice of the mixer for a moment
            while (!finished) {
                peakVoices = std::max(peakVoices.load(), mixer.activeVoices());
                std::this_thread::sleep_for(1ms);
            }
        });
        std::thread player([&] {
            std::this_thread::sleep_for(800ms);
            // The player drags the window to another size (as the window system would tell us).
            SDL_Event resized{};
            resized.type = SDL_EVENT_WINDOW_RESIZED;
            resized.window.data1 = 900;
            resized.window.data2 = 700;
            push(resized);
            std::this_thread::sleep_for(200ms);

            click(settingsButton);
            std::this_thread::sleep_for(300ms);
            click(musicUp);
            musicFollowedSlider = waitFor([&] { return mixer.musicVolume() == 0.35f; }, 2000ms);
            click(camera);
            std::this_thread::sleep_for(200ms);
            click(back);
            settingsWritten = waitFor(
                [&] { return mb::readTextFile(userData.path / "settings.json").value_or("").find("\"smartCamera\": true") != std::string::npos; },
                3000ms);
            std::this_thread::sleep_for(300ms);

            click(start);
            std::this_thread::sleep_for(300ms);
            click(blueBat);
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

        const int result = mb_app::runFrontEnd(*window, *renderer, session, save, options);
        finished = true;
        player.join();
        sampler.join();

        check(result == 0, "runFrontEnd returns 0");
        {
            std::lock_guard lock(session.mutex);
            check(session.started == "knight-miss-1457", "the level started was knight-miss-1457, the eighth of the knights (button 70), not " + session.started);
        }
        check(session.starts == 1, "the level was built exactly once");
        check(updatesAtPause >= 0 && updatesWhilePaused == updatesAtPause, "the level stops updating while paused");
        check(updatesResumed > updatesWhilePaused, "any key resumes it");
        check(session.renders > 10 && session.overlaySprites > 20, "the level was drawn with the HUD (three hearts and two lines of text) over it");

        // Sound.
        check(hoverSounds > 0 && downSounds > 0, "the menu sounds were asked for through the hook");
        check(peakVoices >= 1, "and reached the mixer: a click's sound was playing for a moment");
        check(musicAtStart == 0.3f && soundAtStart == 0.6f, "the saved volumes were set when the menus came up");
        check(musicFollowedSlider, "the music volume followed the Options slider as it moved");
        check(mixer.musicVolume() == 0.35f && mixer.sfxVolume() == 0.6f, "and stays there");
        const std::vector<std::string> expected = {
            "menu music=menu paused=0",
            "level knight-miss-1457 music=knights",
            "pause 1 mixer paused=1",
            "pause 0 mixer paused=0",
            "pause 1 mixer paused=1",
            "pause 0 mixer paused=0",
            "menu music=menu paused=0",
        };
        check(events == expected, "the menu music, the level's music, the pause and its end, and the menu music again, in that order");
        if (events != expected) {
            for (const std::string& e : events) {
                std::fprintf(stderr, "  event: %s\n", e.c_str());
            }
        }
        check(!mixer.paused(), "the sound is not left paused");

        // What the player keeps.
        check(settingsWritten, "leaving Options wrote the settings file");
        check(save.settings.windowWidth == 900 && save.settings.windowHeight == 700, "the settings followed the window to its new size");
        const mb::SaveData kept(userData.path);
        check(kept.settings.musicVolume == 0.35f && kept.settings.soundVolume == 0.6f && kept.settings.smartCamera && kept.settings.bat == 1,
              "the files have the volume, the check box and the bat that were set");
        check(kept.settings.windowWidth == 900 && kept.settings.windowHeight == 700, "and the window size the player left");
        check(!fs::exists(userData.path / "highscores.json") && !fs::exists(userData.path / "progress.json"), "and nothing else was written");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: exception: %s\n", e.what());
        return 1;
    }
    if (failures == 0) {
        std::puts("the front end played through");
    }
    return failures == 0 ? 0 : 1;
}
