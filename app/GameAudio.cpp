#include "GameAudio.h"

#include "mb/engine/Log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <exception>

namespace mb {

namespace {

using Clock = std::chrono::steady_clock;

// A stall (loading a level) must not fire every periodic sound at once afterwards.
constexpr double kLongestFrameSeconds = 0.25;

// Waits for `done`, for at most `maxSeconds` if that is above 0 (and at most a couple of minutes
// otherwise, so a stuck device cannot hang a script).
template <class Done>
void waitUntil(Done done, double maxSeconds) {
    const auto start = Clock::now();
    const double limit = maxSeconds > 0 ? maxSeconds : 180.0;
    while (!done() && std::chrono::duration<double>(Clock::now() - start).count() < limit) {
        SDL_Delay(10);
    }
}

}  // namespace

GameAudio::GameAudio(const std::filesystem::path& assetRoot) {
    try {
        sounds_.emplace(audio_, SoundCatalog::load(assetRoot));
        logInfo("Sound: {} sounds, {} music tracks", sounds_->catalog().sounds().size(),
                sounds_->catalog().musicCount());
    } catch (const std::exception& e) {
        logWarn("sound is off: {}", e.what());
    }
}

void GameAudio::levelShown(std::string_view levelName) {
    if (sounds_) {
        sounds_->playMusicForLevel(levelName);
    }
}

void GameAudio::menuShown() {
    if (sounds_) {
        sounds_->playMusic("Menu");
    }
}

void GameAudio::playSound(std::string_view name) {
    if (sounds_) {
        sounds_->play(name);
    }
}

void GameAudio::setVolumes(float music, float sound) {
    audio_.mixer().setMusicVolume(music);
    audio_.mixer().setSfxVolume(sound);
}

void GameAudio::setPaused(bool paused) {
    audio_.mixer().setPaused(paused);
}

void GameAudio::update() {
    const Clock::time_point now = Clock::now();
    const double elapsed = std::chrono::duration<double>(now - last_).count();
    last_ = now;
    if (sounds_) {
        sounds_->update(std::min(elapsed, kLongestFrameSeconds));
    }
}

int playSoundAndWait(const std::filesystem::path& assetRoot, const std::string& name, double maxSeconds) {
    GameAudio game(assetRoot);
    SoundLibrary* sounds = game.sounds();
    if (!sounds) {
        return 1;
    }
    const SoundDef* def = sounds->catalog().findSound(name);
    if (!def) {
        logError("unknown sound \"{}\"", name);
        return 1;
    }
    if (def->samples.empty()) {
        logWarn("\"{}\" has no sound file in the game's data, so it is silent", def->name);
        return 0;
    }
    if (!game.device().available()) {
        logError("no audio device");
        return 1;
    }
    if (!sounds->play(name)) {
        logError("cannot play \"{}\"", def->name);
        return 1;
    }
    logInfo("playing \"{}\" ({} sample{})", def->name, def->samples.size(), def->samples.size() == 1 ? "" : "s");
    waitUntil([&] { return game.device().mixer().idle(); }, maxSeconds);
    return 0;
}

int playMusicAndWait(const std::filesystem::path& assetRoot, const std::string& name, double maxSeconds) {
    GameAudio game(assetRoot);
    SoundLibrary* sounds = game.sounds();
    if (!sounds) {
        return 1;
    }
    const std::filesystem::path* file = sounds->catalog().findMusic(name);
    if (!file) {
        logError("unknown music \"{}\" (Menu, Village, Pirates, Cowboys, Knights)", name);
        return 1;
    }
    if (!game.device().available()) {
        logError("no audio device");
        return 1;
    }
    logInfo("playing music \"{}\" ({})", name, file->filename().string());
    game.device().playMusic(*file, 0, false);
    waitUntil([&] { return !game.device().mixer().musicPlaying(); }, maxSeconds);
    game.device().stopMusic(0.2f);
    waitUntil([&] { return !game.device().mixer().musicPlaying(); }, 1);
    SDL_Delay(300);  // let the fade reach the speakers
    return 0;
}

}  // namespace mb
