#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace mb {

// What the player keeps between runs: options, how far they got, the best scores. Three small JSON files in
// one folder (normally mb::userDataDir()), each holding {"version": kSaveFormatVersion, ...}; docs/SAVES.md
// describes them. Nothing here knows about the window, the menus or the level files: those use the API below.

inline constexpr int kSaveFormatVersion = 1;

inline constexpr int kBatCount = 3;  // the bats the stage screens offer: 0 red, 1 blue, 2 yellow

struct Settings {
    float musicVolume = 0.7f;  // 0..1 (the original's sliders count 0..100 in steps of 5)
    float soundVolume = 0.8f;
    bool smartCamera = false;  // the original's Options: a camera that follows the action
    bool freezeSky = false;    // sky and clouds stand still
    bool mirrors = true;       // reflections
    bool shadows = true;
    bool fullscreen = false;
    int windowWidth = 1024;  // windowed size in points, kept while the window is fullscreen
    int windowHeight = 768;
    int bat = 0;  // the bat picked on the stage screens, 0 to kBatCount - 1; optional in the file, which older builds did not write
};

struct StageProgress {
    int unlockedLevels = 1;          // how many of the stage's levels, in play order, can be started
    std::set<std::string> completed; // names of the finished levels, lower case
};

// A stage is any stable id the menus pick (they use Stage::name of the Campaign: "pirates", "village", ...); a level is
// its catalog name (LevelEntry::name) in any case. Scores are what the game shows.
struct Progress {
    std::map<std::string, StageProgress, std::less<>> stages;
    std::map<std::string, std::int64_t, std::less<>> bestScores;  // by level name, lower case

    // 1 for a stage nothing was recorded for: the first level is always open.
    int unlockedLevels(std::string_view stage) const;
    bool isCompleted(std::string_view stage, std::string_view level) const;
    // 0 for a level never scored.
    std::int64_t bestScore(std::string_view level) const;

    // Marks the level (number `indexInStage` of its stage, counting from 0) completed, opens the next one
    // and keeps the better score. Returns whether `score` is a new best for the level.
    bool recordCompletion(std::string_view stage, int indexInStage, std::string_view level, std::int64_t score);
    // Keeps the better score for a level that was played but not completed. Returns whether it is a new best.
    bool recordScore(std::string_view level, std::int64_t score);
};

struct HighScore {
    std::string name;
    std::int64_t score = 0;
    std::string level;  // catalog name of the level it was made on
};

// The high-score form shows eight lines on a page and turns pages with arrows. A whole number of pages fills the last
// one when the table is full (ten would end on a page of two lines); five is plenty to browse.
inline constexpr std::size_t kMaxHighScores = 40;
// What the name entry accepts, in characters (the bytes depend on the script).
inline constexpr std::size_t kMaxNameChars = 12;

// The best scores, best first. Entries with the same score keep the order they were added in.
struct HighScores {
    std::vector<HighScore> entries;

    // Whether `score` would make the table: above zero, and either there is a free place or it beats the last.
    bool qualifies(std::int64_t score) const;
    // Adds an entry if it qualifies and returns its place (0 = first). The name loses surrounding
    // spaces and control characters and is cut to kMaxNameChars UTF-8 characters; one that ends up empty
    // becomes "Player".
    std::optional<std::size_t> add(std::string_view name, std::int64_t score, std::string_view level);
};

class SaveData {
public:
    // Reads the three files from `directory`. Never throws and never fails: a file that is missing gives
    // defaults; one that cannot be used (not JSON, not an object, no version, a version newer than this
    // build knows) is moved to "<file>.bak" and gives defaults; single fields of the wrong type or out of
    // range fall back to their default or are clamped, with a warning. Nothing is written until a save.
    // An empty `directory` keeps everything in memory (saves return false).
    explicit SaveData(std::filesystem::path directory);

    Settings settings;
    Progress progress;
    HighScores highScores;

    // Write one part, atomically (see writeFileAtomic). False, with the reason logged, if that failed.
    bool saveSettings() const;
    bool saveProgress() const;
    bool saveHighScores() const;

    const std::filesystem::path& directory() const { return directory_; }

private:
    std::filesystem::path directory_;
};

}  // namespace mb
