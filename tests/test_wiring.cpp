// The sound of the front end: connectAudio hooks the menus, the levels and the pause up to GameAudio. The real GameAudio
// runs on SDL's disk audio driver over a made-up sound list (tests/data's sine wave under the names the game uses), so
// no game data, window or speakers are needed: the test checks what the menus' hooks and the front end's events do to the
// mixer and the sound library.

#include "FrontEndAudio.h"
#include "mb/engine/Files.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
#include <thread>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

template <class Condition>
bool waitFor(Condition condition, std::chrono::milliseconds limit = 3000ms) {
    const auto end = std::chrono::steady_clock::now() + limit;
    while (!condition()) {
        if (std::chrono::steady_clock::now() > end) {
            return false;
        }
        std::this_thread::sleep_for(5ms);
    }
    return true;
}

class TempDir {
public:
    TempDir() : path(fs::temp_directory_path() / ("mb3-wiring-test-" + std::to_string(std::random_device{}()))) {
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

// An asset folder with the sounds the menus use and the five music tracks, all the same short sine wave.
void makeSoundData(const fs::path& root) {
    fs::create_directories(root / "Data" / "xml");
    fs::create_directories(root / "Sounds");
    fs::create_directories(root / "Musics");
    const fs::path sine = fs::path(MB3_TEST_DATA_DIR) / "sine440-stereo.ogg";
    fs::copy_file(sine, root / "Sounds" / "menustonein.ogg");
    mb::writeFileAtomic(root / "Data" / "xml" / "sounds.json",
                        R"({"version": 1, "sounds": [{"name": "MenuStoneIn", "samples": [{"file": "menustonein.ogg"}]}]})");
    std::string musics;
    for (const char* name : {"Menu", "Village", "Pirates", "Cowboys", "Knights"}) {
        fs::copy_file(sine, root / "Musics" / (std::string(name) + ".ogg"));
        musics += std::string(musics.empty() ? "" : ",") + R"({"name": ")" + name + R"(", "file": ")" + name + R"(.ogg"})";
    }
    mb::writeFileAtomic(root / "Data" / "xml" / "musics.json", R"({"version": 1, "musics": [)" + musics + "]}");
}

// Points SDL's audio at a file for as long as it lives: GameAudio then has a device on any machine.
class DiskAudio {
public:
    explicit DiskAudio(const fs::path& file) {
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "disk");
        SDL_SetHint(SDL_HINT_AUDIO_DISK_OUTPUT_FILE, file.string().c_str());
    }
    ~DiskAudio() {
        SDL_ResetHint(SDL_HINT_AUDIO_DRIVER);
        SDL_ResetHint(SDL_HINT_AUDIO_DISK_OUTPUT_FILE);
    }
};

void testWiring() {
    TempDir dir;
    makeSoundData(dir.path);
    DiskAudio disk(dir.path / "out.raw");
    mb::GameAudio audio(dir.path);
    mb::Mixer& mixer = audio.device().mixer();
    check(audio.device().available() && audio.sounds() != nullptr, "the sound opens over the made-up sound list");

    mb_app::FrontEndOptions options;
    mb_app::connectAudio(options, audio);
    check(options.hooks.playSound && options.hooks.settingsChanged && options.events.menuShown && options.events.levelStarted &&
              options.events.paused && options.events.frame,
          "the audio sets every hook and event it needs");

    // The volumes are the Options sliders': 0 to 1, music and sound effects each their own.
    mb::Settings settings;
    settings.musicVolume = 0.25f;
    settings.soundVolume = 0.5f;
    options.hooks.settingsChanged(settings);
    check(mixer.musicVolume() == 0.25f && mixer.sfxVolume() == 0.5f && mixer.masterVolume() == 1.0f, "settings set the music and sound volumes");
    settings.musicVolume = 0;
    options.hooks.settingsChanged(settings);
    check(mixer.musicVolume() == 0 && mixer.sfxVolume() == 0.5f, "and follow every change");

    // Music: the menus', each level's world, and the menus' again.
    options.events.menuShown();
    check(audio.sounds()->music() == "menu", "the menus play their music");
    check(waitFor([&] { return mixer.musicPlaying(); }), "and it reaches the mixer");
    options.events.levelStarted("Knight-Castle");
    check(audio.sounds()->music() == "knights", "a level starting plays its world's music (the name in any case)");
    options.events.levelStarted("piratos-ostrovok");
    check(audio.sounds()->music() == "pirates", "... Pirates");
    options.events.levelStarted("Village-Apple");
    check(audio.sounds()->music() == "village", "... Village");
    options.events.menuShown();
    check(audio.sounds()->music() == "menu", "coming back to the menus plays theirs again");

    // A click's sound, by the name the form gives it.
    options.hooks.playSound("menustonein");
    check(mixer.activeVoices() == 1, "a menu sound is asked of the mixer");
    check(waitFor([&] { return mixer.idle(); }), "and plays to its end");
    options.hooks.playSound("nosuchsound");
    check(mixer.activeVoices() == 0, "a name the sound list does not have plays nothing");

    // Pause: everything stands still, a sound asked for meanwhile waits, and the end of the pause lets it through.
    options.events.paused(true);
    check(mixer.paused(), "the pause screen pauses the mixer");
    options.hooks.playSound("menustonein");
    std::this_thread::sleep_for(400ms);
    check(!mixer.idle(), "a sound asked for during the pause waits for it to end (so the front end resumes before the menus play any)");
    options.events.paused(false);
    check(!mixer.paused() && waitFor([&] { return mixer.idle(); }), "resuming lets it play");

    // The way out of the pause screen to the menus, in the order the front end uses: resume first, then the menus' music
    // and their sounds.
    options.events.levelStarted("knight-castle");
    options.events.paused(true);
    options.events.paused(false);
    options.events.menuShown();
    options.hooks.playSound("menustonein");
    check(audio.sounds()->music() == "menu" && !mixer.paused() && waitFor([&] { return mixer.idle(); }),
          "after the pause the menus have their music and their sounds work");

    for (int i = 0; i < 10; ++i) {
        options.events.frame();
    }
    check(mixer.activeVoices() == 0, "frames only tick the sound's clock");
}

// Without the sound list the game goes on silently, and the volumes and the pause still answer.
void testWithoutSoundData() {
    TempDir dir;  // no Data/xml/sounds.json in it
    DiskAudio disk(dir.path / "out.raw");
    mb::GameAudio audio(dir.path);
    check(audio.sounds() == nullptr, "no sound list, no sound library");

    mb_app::FrontEndOptions options;
    mb_app::connectAudio(options, audio);
    mb::Settings settings;
    settings.musicVolume = 0.4f;
    options.hooks.settingsChanged(settings);
    options.hooks.playSound("menustonein");
    options.events.menuShown();
    options.events.levelStarted("knight-castle");
    options.events.paused(true);
    check(audio.device().mixer().paused() && audio.device().mixer().musicVolume() == 0.4f, "the pause and the volumes still work");
    options.events.paused(false);
    options.events.frame();
    check(!audio.device().mixer().paused(), "and so does the end of the pause");
}

}  // namespace

int main() {
    testWiring();
    testWithoutSoundData();
    if (failures == 0) {
        std::puts("all wiring tests passed");
    }
    return failures == 0 ? 0 : 1;
}
