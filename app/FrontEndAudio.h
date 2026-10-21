#pragma once

#include "FrontEnd.h"
#include "GameAudio.h"

namespace mb_app {

// Plays the front end through `audio`: the menus' hover and press sounds, the music of the menus and of each level
// as it starts, the Options volumes (live, and from the saved settings at the start), and the pause. Replaces the
// hooks and events it uses; wrap them afterwards to watch. `audio` must outlive the front end.
void connectAudio(FrontEndOptions& options, mb::GameAudio& audio);

}  // namespace mb_app
