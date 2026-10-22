#pragma once

#include <filesystem>

namespace mb {

// Where converted game assets live. The first of these that exists wins:
//   1. the MB3_ASSETS environment variable (used as given, even if it does not exist),
//   2. Resources/assets inside the macOS app bundle (see MB3_BUNDLE_ASSETS in CMake),
//   3. ./assets in the working directory,
//   4. assets/ inside userDataDir(), where an app without bundled assets, started from the Finder, finds them,
//   5. CMake's MB3_ASSETS_DIR, in a build made with MB3_DEV_ASSETS (the default; packaged apps leave it out).
// If none exists, ./assets, so the error message names a place to put them.
std::filesystem::path assetRoot();

// The per-user folder for settings, progress and high scores, created on first use:
//   macOS    ~/Library/Application Support/MuggleBall3/
//   Windows  %APPDATA%\MuggleBall3\
//   Linux    $XDG_DATA_HOME/MuggleBall3/ (default ~/.local/share/MuggleBall3/)
// MB3_USER_DATA, if set, is used instead (macOS ignores $HOME here), which keeps tests and experiments
// away from the player's real saves.
// The usual "organisation" level is left out on purpose: the original is Alawar's and Dream Dale's, this
// is neither, and a folder named after them would suggest otherwise. Empty if the OS offers no such folder.
std::filesystem::path userDataDir();

}  // namespace mb
