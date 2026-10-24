// Tests for the file helpers (mb/engine/Files.h) and the save data (mb/game/SaveData.h), in a temporary
// folder. No game data and no display needed.

#include "mb/engine/Files.h"
#include "mb/game/SaveData.h"

#include <SDL3/SDL_log.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Counts the warnings logged while alive.
class WarningCapture {
public:
    WarningCapture() {
        SDL_GetLogOutputFunction(&previous_, &previousData_);
        SDL_SetLogOutputFunction(&WarningCapture::onLog, this);
    }
    ~WarningCapture() { SDL_SetLogOutputFunction(previous_, previousData_); }
    WarningCapture(const WarningCapture&) = delete;
    WarningCapture& operator=(const WarningCapture&) = delete;

    int count = 0;

private:
    static void onLog(void* self, int, SDL_LogPriority priority, const char*) {
        if (priority == SDL_LOG_PRIORITY_WARN) {
            ++static_cast<WarningCapture*>(self)->count;
        }
    }

    SDL_LogOutputFunction previous_ = nullptr;
    void* previousData_ = nullptr;
};

// A fresh folder that is removed again.
class TempDir {
public:
    TempDir() : path(fs::temp_directory_path() / ("mb3-save-test-" + std::to_string(std::random_device{}()))) {
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

void writeText(const fs::path& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}

std::string readText(const fs::path& path) {
    return mb::readTextFile(path).value_or("<missing>");
}

// --- File helpers -------------------------------------------------------------

void testFileHelpers() {
    TempDir dir;
    const fs::path file = dir.path / "nested" / "data.txt";

    check(!mb::readTextFile(file), "a missing file reads as nothing");
    check(mb::writeFileAtomic(file, "one"), "write creates the folders");
    check(readText(file) == "one", "written contents");
    check(mb::writeFileAtomic(file, std::string("two\0three", 9)) && readText(file) == std::string("two\0three", 9),
          "overwrite keeps binary contents");
    check(!fs::exists(fs::path(file) += ".tmp"), "no temp file is left after a write");

    // A target that cannot be replaced (a folder) fails cleanly and leaves nothing behind: the fallback only
    // moves aside a regular file (it would otherwise destroy the user's directory on a .bak collision), so a
    // folder in the way still blocks the write exactly as a plain rename onto it always has.
    const fs::path blocked = dir.path / "blocked";
    fs::create_directories(blocked / "inside");
    WarningCapture quiet;
    check(!mb::writeFileAtomic(blocked, "x"), "replacing a folder fails");
    check(fs::is_directory(blocked) && !fs::exists(fs::path(blocked) += ".tmp"), "failure leaves the target and no temp file");

    // A stale temp file from a crash does not matter.
    writeText(fs::path(file) += ".tmp", "half a fi");
    check(mb::writeFileAtomic(file, "three") && readText(file) == "three", "a stale temp file is overwritten");

    // The fallback's own regular-file path (rename refused, so the old file is moved to .bak and the rename
    // retried) is not provoked here: on this host, `rename` replaces an existing regular file outright, so
    // writeFileAtomic's normal path above already covers overwriting one. keepAsBackup() itself, which the
    // fallback calls, is tested directly instead.
    check(mb::keepAsBackup(file) && !fs::exists(file) && readText(fs::path(file) += ".bak") == "three", "kept as .bak");
    writeText(file, "newer");
    check(mb::keepAsBackup(file) && readText(fs::path(file) += ".bak") == "newer", "an older .bak is replaced");
}

// --- Defaults, round trips ----------------------------------------------------

void testMissingFilesGiveDefaults() {
    TempDir dir;
    WarningCapture warnings;
    const mb::SaveData save(dir.path / "not-created-yet");
    const mb::Settings defaults;
    check(save.settings.musicVolume == defaults.musicVolume && save.settings.windowWidth == 1024 &&
              save.settings.windowHeight == 768 && !save.settings.fullscreen && save.settings.mirrors && save.settings.shadows &&
              save.settings.bat == 0,
          "default settings, with the red bat");
    check(save.progress.stages.empty() && save.progress.bestScores.empty() && save.highScores.entries.empty(),
          "empty progress and scores");
    check(warnings.count == 0, "a first run is not worth a warning");
    check(!fs::exists(dir.path / "not-created-yet"), "loading writes nothing");
}

void testNoFolderKeepsEverythingInMemory() {
    mb::SaveData save{fs::path()};
    save.settings.fullscreen = true;
    check(!save.saveSettings() && !save.saveProgress() && !save.saveHighScores(), "saving without a folder returns false");
    check(save.settings.fullscreen, "the values stay");
}

void testSettingsRoundTrip() {
    TempDir dir;
    {
        mb::SaveData save(dir.path);
        save.settings.musicVolume = 0.35f;
        save.settings.soundVolume = 0.0f;
        save.settings.smartCamera = true;
        save.settings.freezeSky = true;
        save.settings.mirrors = false;
        save.settings.shadows = false;
        save.settings.fullscreen = true;
        save.settings.windowWidth = 1280;
        save.settings.windowHeight = 720;
        save.settings.bat = 2;
        check(save.saveSettings(), "settings save");
    }
    const std::string text = readText(dir.path / "settings.json");
    check(text.find("\"version\": 1") != std::string::npos && text.find("\"musicVolume\": 0.35") != std::string::npos &&
              text.find("\"bat\": 2") != std::string::npos && text.back() == '\n',
          "settings file is readable JSON with a version and short decimals");
    const mb::SaveData loaded(dir.path);
    const mb::Settings& s = loaded.settings;
    check(s.musicVolume == 0.35f && s.soundVolume == 0.0f && s.smartCamera && s.freezeSky && !s.mirrors && !s.shadows &&
              s.fullscreen && s.windowWidth == 1280 && s.windowHeight == 720 && s.bat == 2,
          "settings round trip");
    check(!fs::exists(dir.path / "progress.json") && !fs::exists(dir.path / "highscores.json"),
          "saving settings leaves the other files alone");
}

void testProgressRoundTrip() {
    TempDir dir;
    {
        mb::SaveData save(dir.path);
        save.progress.recordCompletion("Piratos", 0, "Piratos-Ostrovok", 1200);
        save.progress.recordCompletion("piratos", 1, "piratos-sos", 800);
        save.progress.recordCompletion("cowboys", 4, "cowboys-napadenie", 50);
        save.progress.recordScore("knight-castle", 300);
        check(save.saveProgress(), "progress saves");
    }
    const mb::SaveData loaded(dir.path);
    const mb::Progress& p = loaded.progress;
    check(p.unlockedLevels("piratos") == 3 && p.unlockedLevels("COWBOYS") == 6 && p.unlockedLevels("village") == 1,
          "unlocked levels survive");
    check(p.isCompleted("piratos", "PIRATOS-OSTROVOK") && p.isCompleted("piratos", "piratos-sos") &&
              !p.isCompleted("piratos", "piratos-zasada") && !p.isCompleted("village", "piratos-sos"),
          "completed levels survive, by stage");
    check(p.bestScore("piratos-ostrovok") == 1200 && p.bestScore("Knight-Castle") == 300 && p.bestScore("nothing") == 0,
          "best scores survive");
}

void testHighScoresRoundTrip() {
    TempDir dir;
    {
        mb::SaveData save(dir.path);
        save.highScores.add("Greg", 500, "piratos-sos");
        save.highScores.add("Ann", 900, "knight-castle");
        check(save.saveHighScores(), "high scores save");
    }
    const mb::SaveData loaded(dir.path);
    const auto& e = loaded.highScores.entries;
    check(e.size() == 2 && e[0].name == "Ann" && e[0].score == 900 && e[0].level == "knight-castle" && e[1].name == "Greg",
          "high scores round trip, best first");
}

// --- Damaged files ------------------------------------------------------------

// Each of these must be set aside as .bak and give defaults, with a warning.
void testUnusableFilesAreKeptAsBackup() {
    struct Case {
        const char* what;
        std::string text;
    };
    const Case cases[] = {
        {"truncated JSON", R"({"version": 1, "musicVolume": 0.)"},
        {"empty file", ""},
        {"binary junk", std::string("\x00\xff\xfe\x01 junk", 9)},
        {"an array", "[1, 2, 3]"},
        {"no version", R"({"musicVolume": 0.1})"},
        {"version 0", R"({"version": 0, "musicVolume": 0.1})"},
        {"version as text", R"({"version": "1", "musicVolume": 0.1})"},
        {"a newer version", R"({"version": 2, "musicVolume": 0.1})"},
    };
    for (const Case& c : cases) {
        TempDir dir;
        writeText(dir.path / "settings.json", c.text);
        WarningCapture warnings;
        mb::SaveData save(dir.path);
        const std::string label = std::string("settings: ") + c.what;
        check(save.settings.musicVolume == mb::Settings{}.musicVolume, (label + " gives the defaults").c_str());
        check(warnings.count == 1, (label + " warns once").c_str());
        check(!fs::exists(dir.path / "settings.json") && readText(dir.path / "settings.json.bak") == c.text,
              (label + " is kept as .bak").c_str());

        check(save.saveSettings() && readText(dir.path / "settings.json.bak") == c.text, (label + ": a save leaves the .bak").c_str());
        const mb::SaveData again(dir.path);
        check(again.settings.musicVolume == mb::Settings{}.musicVolume, (label + ": the fresh file loads").c_str());
    }
}

void testEachFileIsIndependent() {
    TempDir dir;
    writeText(dir.path / "progress.json", "not json");
    WarningCapture warnings;
    mb::SaveData first(dir.path);
    check(warnings.count == 1 && fs::exists(dir.path / "progress.json.bak"), "the broken progress file is set aside");
    first.settings.fullscreen = true;
    first.highScores.add("A", 10, "x");
    check(first.saveSettings() && first.saveHighScores(), "the other files save");
    const mb::SaveData again(dir.path);
    check(again.settings.fullscreen && again.highScores.entries.size() == 1 && again.progress.stages.empty(),
          "a broken progress file does not touch settings or scores");
    check(warnings.count == 1, "and nothing more is reported the second time");
}

void testBadFieldsFallBackOneByOne() {
    TempDir dir;
    writeText(dir.path / "settings.json", R"({
        "version": 1,
        "musicVolume": 7,
        "soundVolume": "loud",
        "smartCamera": 1,
        "freezeSky": true,
        "windowWidth": 10,
        "windowHeight": 99999999999,
        "fullscreen": null,
        "somethingNew": [1, 2]
    })");
    WarningCapture warnings;
    const mb::SaveData save(dir.path);
    const mb::Settings defaults;
    const mb::Settings& s = save.settings;
    check(s.musicVolume == 1.0f, "an out-of-range volume is clamped");
    check(s.soundVolume == defaults.soundVolume, "a volume that is text keeps the default");
    check(s.smartCamera == defaults.smartCamera && s.fullscreen == defaults.fullscreen, "a flag that is not a boolean keeps the default");
    check(s.freezeSky, "a good field is still read");
    check(s.windowWidth == 320 && s.windowHeight == 16384, "window sizes are clamped");
    check(warnings.count == 6, "one warning per bad field (null is not a boolean), none for unknown keys");
    check(fs::exists(dir.path / "settings.json") && !fs::exists(dir.path / "settings.json.bak"), "the file stays in place");
}

void testBatField() {
    TempDir dir;
    // A file written before the bat was kept: still version 1, and the bat is the default.
    writeText(dir.path / "settings.json", R"({"version": 1, "musicVolume": 0.2, "windowWidth": 1280})");
    {
        WarningCapture warnings;
        mb::SaveData save(dir.path);
        check(save.settings.bat == 0 && save.settings.musicVolume == 0.2f && save.settings.windowWidth == 1280 && warnings.count == 0,
              "a settings file without a bat loads quietly, with the red bat");
        save.settings.bat = 1;
        check(save.saveSettings() && mb::SaveData(dir.path).settings.bat == 1, "and keeps the bat chosen after that");
    }
    const struct {
        const char* json;
        int bat;
        int warnings;
        const char* what;
    } cases[] = {
        {R"({"version": 1, "bat": 2})", 2, 0, "the last bat is read"},
        {R"({"version": 1, "bat": 3})", 2, 1, "a bat past the last is clamped to it, with a warning"},
        {R"({"version": 1, "bat": -4})", 0, 1, "a negative bat is clamped to the first"},
        {R"({"version": 1, "bat": "blue"})", 0, 1, "a bat that is text keeps the default"},
        {R"({"version": 1, "bat": 1.5})", 0, 1, "a bat that is not a whole number keeps the default"},
    };
    for (const auto& c : cases) {
        writeText(dir.path / "settings.json", c.json);
        WarningCapture warnings;
        const mb::SaveData save(dir.path);
        check(save.settings.bat == c.bat && warnings.count == c.warnings, c.what);
    }
}

void testBadProgressAndScoreEntries() {
    TempDir dir;
    writeText(dir.path / "progress.json", R"({
        "version": 1,
        "stages": {"Piratos": {"unlockedLevels": 0, "completed": ["A-B", 3, "c"]}, "broken": 5, "far": {"unlockedLevels": 99999999}},
        "bestScores": {"x": 10, "y": -5, "z": "many", "W": 7}
    })");
    writeText(dir.path / "highscores.json", R"({
        "version": 1,
        "entries": [
            {"name": "low", "score": 10, "level": "a"},
            {"name": "high", "score": 99, "level": "b"},
            {"name": "zero", "score": 0},
            {"score": 50},
            {"name": "negative", "score": -3},
            {"name": "  padded  ", "score": 40},
            "text"
        ]
    })");
    WarningCapture warnings;
    const mb::SaveData save(dir.path);
    const mb::Progress& p = save.progress;
    check(p.unlockedLevels("piratos") == 1 && p.isCompleted("piratos", "a-b") && p.isCompleted("piratos", "c"),
          "stage ids and level names are lower-cased, bad values skipped");
    check(p.stages.count("broken") == 0 && p.unlockedLevels("far") == 10000, "a stage that is not an object is dropped, a huge count clamped");
    check(p.bestScore("x") == 10 && p.bestScore("w") == 7 && p.bestScore("y") == 0 && p.bestScore("z") == 0, "bad scores are dropped");
    const auto& e = save.highScores.entries;
    check(e.size() == 3 && e[0].name == "high" && e[1].name == "padded" && e[2].name == "low", "entries are cleaned and put in order");
    check(warnings.count == 5 + 4, "each bad progress value and score entry is reported");
}

void testOverlongHighScoreFileIsCut() {
    TempDir dir;
    const int count = static_cast<int>(mb::kMaxHighScores) + 5;
    std::string entries;
    for (int i = 1; i <= count; ++i) {
        entries += std::string(i > 1 ? "," : "") + R"({"name": "n)" + std::to_string(i) + R"(", "score": )" + std::to_string(i * 10) + "}";
    }
    writeText(dir.path / "highscores.json", R"({"version": 1, "entries": [)" + entries + "]}");
    const mb::SaveData save(dir.path);
    const auto& e = save.highScores.entries;
    check(e.size() == mb::kMaxHighScores && e.front().score == count * 10 && e.back().score == 60, "the best forty, best first");
}

// --- Rules --------------------------------------------------------------------

void testProgressRules() {
    mb::Progress p;
    check(p.unlockedLevels("piratos") == 1 && p.bestScore("a") == 0 && !p.isCompleted("piratos", "a"), "a new player has the first level only");
    check(p.recordCompletion("piratos", 0, "a", 100), "the first score is a new best");
    check(p.unlockedLevels("piratos") == 2 && p.isCompleted("piratos", "a"), "finishing level 0 opens level 1");
    check(!p.recordCompletion("piratos", 0, "A", 80) && p.bestScore("a") == 100, "a worse score changes nothing");
    check(p.unlockedLevels("piratos") == 2, "replaying does not unlock more");
    check(p.recordCompletion("piratos", 0, "a", 120) && p.bestScore("a") == 120, "a better score replaces the best");
    p.recordCompletion("piratos", 5, "f", 1);
    check(p.unlockedLevels("piratos") == 7, "finishing a later level opens the one after it");
    p.recordCompletion("piratos", 2, "c", 1);
    check(p.unlockedLevels("piratos") == 7, "finishing an earlier level never locks anything");
    p.recordCompletion("piratos", -4, "neg", 1);
    check(p.unlockedLevels("piratos") == 7, "a bad index is treated as the first level");
    check(!p.recordScore("g", 0) && !p.recordScore("g", -5) && p.bestScore("g") == 0, "a score of zero is no record");
    check(p.recordScore("g", 40) && !p.isCompleted("piratos", "g"), "a score without completion");
}

void testHighScoreRules() {
    mb::HighScores t;
    check(!t.qualifies(0) && !t.qualifies(-1) && t.qualifies(1), "any positive score qualifies for an empty table");
    check(!t.add("x", 0, "l").has_value() && t.entries.empty(), "zero is not listed");
    auto place = [&t](const char* who, std::int64_t score) {
        const auto rank = t.add(who, score, "l");
        return rank ? static_cast<int>(*rank) : -1;
    };
    check(place("b", 200) == 0 && place("a", 300) == 0 && place("c", 100) == 2, "places");
    check(place("b2", 200) == 2, "a tie ranks below the earlier entry");
    check(t.entries[1].name == "b" && t.entries[2].name == "b2" && t.entries[3].name == "c", "order after the tie");
    for (std::size_t i = 0, free = mb::kMaxHighScores - t.entries.size(); i < free; ++i) {
        t.add("fill", 50 + static_cast<std::int64_t>(i), "l");  // 50 up, all below the 100 of "c"
    }
    const int last = static_cast<int>(mb::kMaxHighScores) - 1;
    check(t.entries.size() == mb::kMaxHighScores && t.entries.back().score == 50, "the table is full");
    check(!t.qualifies(50) && place("late", 50) == -1, "equal to the last place does not qualify");
    check(t.qualifies(51) && place("new", 51) == last && t.entries.size() == mb::kMaxHighScores && t.entries.back().name == "new",
          "a better score pushes the last one out");
    check(place("top", 1000) == 0 && t.entries.size() == mb::kMaxHighScores && t.entries.back().score == 51, "the worst falls off");
}

void testNameCleaning() {
    mb::HighScores t;
    auto name = [&t](std::string_view raw) {
        t.entries.clear();
        t.add(raw, 5, "l");
        return t.entries.front().name;
    };
    check(name("  Greg  ") == "Greg", "spaces are trimmed");
    check(name("Gr\neg\t\x7f") == "Greg", "control characters are dropped");
    check(name("") == "Player" && name("   ") == "Player" && name("\n\n") == "Player", "an empty name becomes Player");
    const std::size_t limit = mb::kMaxNameChars;
    check(name(std::string(40, 'x')) == std::string(limit, 'x'), "long names are cut to the characters the name entry accepts");
    check(name(std::string(limit - 1, 'x') + "\xC3\xA9tail") == std::string(limit - 1, 'x') + "\xC3\xA9", "a two-byte character that fits is kept whole");
    std::string cyrillic, chinese, emoji;
    for (std::size_t i = 0; i < limit + 3; ++i) {
        cyrillic += "\xD0\x96";          // two bytes each
        chinese += "\xE4\xB8\xAD";      // three
        emoji += "\xF0\x9F\x98\x80";  // four
    }
    check(name(cyrillic) == cyrillic.substr(0, 2 * limit) && name(chinese) == chinese.substr(0, 3 * limit) &&
              name(emoji) == emoji.substr(0, 4 * limit),
          "the limit counts characters, not bytes, and never splits one");
    check(name(std::string(limit - 4, 'x') + "    " + "yyyy") == std::string(limit - 4, 'x'), "spaces left at the cut are trimmed");
    check(name("Ren\xC3\xA9") == "Ren\xC3\xA9", "UTF-8 passes through");

    // A name that is not valid UTF-8 must not stop the file from being written.
    TempDir dir;
    mb::SaveData save(dir.path);
    save.highScores.add("bad\xFF" "name", 10, "l");
    check(save.saveHighScores(), "invalid UTF-8 in a name still saves");
    check(mb::SaveData(dir.path).highScores.entries.size() == 1, "and loads again");
}

}  // namespace

int main() {
    testFileHelpers();
    testMissingFilesGiveDefaults();
    testNoFolderKeepsEverythingInMemory();
    testSettingsRoundTrip();
    testProgressRoundTrip();
    testHighScoresRoundTrip();
    testUnusableFilesAreKeptAsBackup();
    testEachFileIsIndependent();
    testBadFieldsFallBackOneByOne();
    testBatField();
    testBadProgressAndScoreEntries();
    testOverlongHighScoreFileIsCut();
    testProgressRules();
    testHighScoreRules();
    testNameCleaning();
    if (failures == 0) {
        std::puts("all save tests passed");
    }
    return failures == 0 ? 0 : 1;
}
