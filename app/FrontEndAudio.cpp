#include "FrontEndAudio.h"

namespace mb_app {

void connectAudio(FrontEndOptions& options, mb::GameAudio& audio) {
    options.hooks.playSound = [&audio](std::string_view name) { audio.playSound(name); };
    options.hooks.settingsChanged = [&audio](const mb::Settings& settings) {
        audio.setVolumes(settings.musicVolume, settings.soundVolume);
    };
    options.events.menuShown = [&audio] { audio.menuShown(); };
    options.events.levelStarted = [&audio](std::string_view levelName) { audio.levelShown(levelName); };
    options.events.paused = [&audio](bool paused) { audio.setPaused(paused); };
    options.events.frame = [&audio] { audio.update(); };
}

}  // namespace mb_app
