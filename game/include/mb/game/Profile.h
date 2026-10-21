#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace mb {

// What the Options form edits. The values are the ones the menu shows; what each means for the game (volumes for
// the audio, shadows and mirrors for the renderer, the camera and the sky for the scene) is up to the code that
// reads them. The defaults are this port's: the original's are not in the data.
struct Settings {
    int musicVolume = 70;  // 0 to 100, in steps of 5 (the slider's)
    int soundVolume = 100;
    bool smartCamera = true;  // "Smart Camera"
    bool freezeSky = false;   // "Freeze Sky": stops the sky scrolling, in the menus too
    bool shadows = true;
    bool mirrors = true;
    bool fullscreen = false;
    int windowWidth = 1024;  // the video mode chosen in the list, in points; applied with the Main Menu button
    int windowHeight = 768;
    int bat = 0;  // the bat picked on the stage screens: 0 red, 1 blue, 2 yellow

    bool operator==(const Settings&) const = default;
};

// One line of the high-score table.
struct HighScore {
    std::string name;
    int score = 0;

    bool operator==(const HighScore&) const = default;
};

// Everything the player keeps between runs: the menus read it and change it, a save hook (MenuHooks) writes it out.
struct Profile {
    static constexpr std::size_t kMaxHighScores = 40;

    Settings settings;
    // Levels 0 up to this one (by index in the Campaign) can be started; the rest show as locked on the stage
    // screens. Nothing records a finished level yet, so every level is open until the game does.
    int unlockedLevels = std::numeric_limits<int>::max();
    std::vector<HighScore> highScores;   // best first, at most kMaxHighScores

    // Whether a score would make the table (a score of 0 never does).
    bool qualifies(int score) const;
    // Puts the score in its place, after any equal ones, and returns its index, or -1 if it did not make the table.
    int addHighScore(std::string name, int score);
};

}  // namespace mb
