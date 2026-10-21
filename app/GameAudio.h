#pragma once

#include "mb/engine/Audio.h"
#include "mb/game/SoundLibrary.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace mb {

// The audio side of the running game: the device, the sound library, the music of the menus and the levels,
// the volumes and the pause. Sound data that is missing or unreadable turns sound off with a warning, never
// stops the game; the volumes and the pause still work (on a device that plays nothing).
//
// Create it after the Window and let it go first: the audio device is closed in its destructor, and
// the window's SDL_Quit must come after that.
class GameAudio {
public:
    explicit GameAudio(const std::filesystem::path& assetRoot);

    // Call when a level has been shown: plays the music of its world (nothing changes if it is
    // already playing).
    void levelShown(std::string_view levelName);
    // Call when the menus are up: plays their music (nothing changes if it is already playing).
    void menuShown();

    // A sound the menus ask for by the name the forms give it ("menustonein"), at its own volume and centred.
    void playSound(std::string_view name);

    // The volumes of the Options sliders, 0 to 1: the music's, and the sound effects' (the menus' own sounds
    // are effects too).
    void setVolumes(float music, float sound);

    // Freezes every sound and the music, and carries on where they stopped. The mixer's pause holds
    // everything, including a sound asked for while it lasts, which then waits for the end of the pause: so
    // pause only while nothing of the player's own makes a sound (the pause screen has none), and resume
    // before the menus come back.
    void setPaused(bool paused);

    // Call once per frame; the clock behind replay delays and periodic sounds.
    void update();

    Audio& device() { return audio_; }
    // Null if the sound data could not be loaded. This is what game code plays its sounds through.
    SoundLibrary* sounds() { return sounds_ ? &*sounds_ : nullptr; }

private:
    Audio audio_;
    std::optional<SoundLibrary> sounds_;
    std::chrono::steady_clock::time_point last_ = std::chrono::steady_clock::now();
};

// --play-sound and --play-music: play one named sound / one pass of one music track, wait until it is
// over (or `maxSeconds` have passed, if above 0) and return the process exit code.
int playSoundAndWait(const std::filesystem::path& assetRoot, const std::string& name, double maxSeconds);
int playMusicAndWait(const std::filesystem::path& assetRoot, const std::string& name, double maxSeconds);

}  // namespace mb
