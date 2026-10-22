# Saves

What the player keeps between runs lives in three small JSON files in one folder. The code is
`engine/include/mb/engine/Files.h` (atomic file writes, `Paths.h` for the folder) and
`game/include/mb/game/SaveData.h` (the data and the files); `tests/test_save.cpp` covers it in a temporary
folder. `SaveData` is the one model of this data: the game's menus, the level viewer and the window code all read and
change its `Settings`, `Progress` and `HighScores`, and there is no second copy of them elsewhere.

## Where

`mb::userDataDir()`:

| OS | Folder |
|---|---|
| macOS | `~/Library/Application Support/MuggleBall3/` |
| Windows | `%APPDATA%\MuggleBall3\` |
| Linux | `$XDG_DATA_HOME/MuggleBall3/`, default `~/.local/share/MuggleBall3/` |

It comes from `SDL_GetPrefPath("", "MuggleBall3")`. The usual organisation level is empty on purpose: the
suggested "Alawar" would put the original publisher's name on a folder of this project's files, and this
project is neither Alawar's nor Dream Dale's. The folder is created on first use. `MB3_USER_DATA` replaces
it (macOS ignores `$HOME` here, so this is how to keep experiments and tests away from real saves; the front
end's smoke test, `tests/frontend_smoke.cpp`, sets it to a temporary folder and reads the files back).

An app without bundled assets, started from the Finder, also looks for the converted assets in `assets/`
inside this folder (after `MB3_ASSETS`, the bundle and `./assets`; a build of your own then tries the
repository's `assets/`, see `MACOS.md`).

## Files

Each file is an object with `"version": 1` first. `kSaveFormatVersion` in `SaveData.h` is the version of all
three; raise it when a file written by an older build would be misread, and add the conversion for the old
version to the loader (`loadDocument` in `SaveData.cpp` rejects every version above the current one, so an
old build never reads a newer file).

A field added to version 1 later is optional: a file without it loads with the field's default and no warning, and
a build that does not know the field ignores it (and leaves it out when it next saves that file). That is how `bat`
came (below); it needs no new version, because no file is misread. A version is raised only for a change that
would be: a field that changes meaning, or one that must be there.

`settings.json`

```json
{
  "version": 1,
  "musicVolume": 0.7, "soundVolume": 0.8,
  "smartCamera": false, "freezeSky": false, "mirrors": true, "shadows": true,
  "fullscreen": false, "windowWidth": 1024, "windowHeight": 768,
  "bat": 0
}
```

Volumes are 0 to 1 (the original's sliders count 0 to 100 in steps of 5: the Options form shows a volume as the
nearest whole percent, and a slider position of 35 is stored as 0.35). `smartCamera`, `freezeSky`, `mirrors` and
`shadows` are the original's Options checkboxes. The window size is the windowed size in points (not pixels: a Retina
display shows twice as many), and is kept while the window is fullscreen. `bat` is the bat picked on the stage
screens, 0 red, 1 blue, 2 yellow (`kBatCount` bats; a number outside that is clamped, with a warning). The defaults
are mine, not the original's; they are `Settings`' and the menus show no others.

`progress.json`

```json
{
  "version": 1,
  "stages": {"pirates": {"unlockedLevels": 3, "completed": ["piratos-ostrovok", "piratos-sos"]}},
  "bestScores": {"piratos-ostrovok": 12000}
}
```

The menus name a stage by its `Stage::name` in the campaign (`Data/xml/levels.json`), in lower case: `pirates`, `village`,
`cowboys`, `knights`. The campaign numbers its 83 levels 0 to 82 across the stages; `unlockedLevels` counts within
a stage, so level `i` of the campaign, number `i - Stage::first` in its stage, can be started when that is below
`unlockedLevels`. The first level of every stage is always open, so every stage can be entered from the start. A level
is finished when its last target is gone (`GAMEPLAY.md`): `GameFlow` then calls `recordCompletion` and writes the file, which
opens the next level of the stage, and the stage screens lock the levels that are not open yet (`Menu::setLevelsLocked`,
on). `bestScores` are the points made on the level itself (what the HUD gained from the level's start to its end, not the
total the game was carrying), kept when it is finished and, as `recordScore`, when the game was lost on it.

`highscores.json`

```json
{
  "version": 1,
  "entries": [{"name": "Gregory", "score": 12000, "level": "piratos-ostrovok"}]
}
```

The table keeps the best 40 (`kMaxHighScores`). The high-score form shows eight lines on a page and turns pages with
two arrows, so the length that suits it is a whole number of pages, which leaves no half-empty last page when the
table is full: five pages are plenty to browse, and a top ten would end on a page of two lines. A name is at most 12
characters (`kMaxNameChars`, what the name entry accepts), whatever their size in bytes.

## Writes

`writeFileAtomic` writes `<file>.tmp` and renames it over the file, so a crash leaves the old file or the
new one. It does not fsync, so a power cut can still lose the latest save, and a half-written file is then
handled as below. Each part is saved on its own (`saveSettings`, `saveProgress`, `saveHighScores`), and
loading never writes: a first run creates no files.

## Damaged files

`SaveData`'s constructor never throws.

- A **missing** file gives the defaults, quietly.
- A file that is **not JSON, not an object, has no integer `version` of at least 1, or has a version newer
  than this build knows** is moved to `<file>.bak` (replacing an older `.bak`), a warning is logged and the
  defaults are used. The next save writes a fresh file; the `.bak` stays until the next such file replaces it.
  A newer file is kept this way too, so going back to an older build does not destroy it: rename the `.bak`
  back to use it again with a newer build.
- A **single field** of the wrong type or out of range keeps its default or is clamped, with a warning, and
  the rest of the file is used. Unknown keys are ignored. A high score entry without a name or with a score
  of zero or less is dropped; the list is sorted and cut to the best 40.
- An empty directory path (`userDataDir()` found no folder) keeps everything in memory, and the saves
  return false.

## Using it

The game (`runGame` in `app/main.cpp`) loads one `SaveData`, opens the window at its size and fullscreen state, and
hands it to the front end, whose `Menu` shows it and changes it: the Options form edits `settings` (the sliders,
check boxes and video list), the stage screens read `progress` and `settings.bat`, the high-score form shows
`highScores`. The `Menu` writes what it changed when the player leaves Options (settings), picks a bat (settings) and
enters a score (high scores); `GameFlow` writes `progress` when a level ends, won or lost, and `PlaySession` reads
`settings.bat` for the bat's colour whenever a level starts; the front end keeps `settings` in step with the window while the
player resizes it or goes fullscreen (not while Options is up, which holds choices not applied yet), and
`runGame`, like the level viewer's `runWindow`, saves the window state when the window closes. The form viewer
(`--form`) gives the menus a `SaveData` with no folder instead, so it never touches the real files, and so does `--autoplay`
(`openSave` in `app/main.cpp`): the autopilot plays on nobody's behalf, so it runs on the defaults, kept in memory, and what it
finishes or scores is gone when it quits.

```cpp
mb::SaveData save(mb::userDataDir());     // once, at start-up
mb::Menu menu(loadForm, campaign, save, hooks);   // the menus work on it directly

// The menus' own use, for reference: Options
save.settings.musicVolume = 0.5f;         // a slider at 50
save.saveSettings();

// Stage and level select: the stage is the campaign's Stage::name, lower case, and levels are named as in the
// catalog (LevelEntry::name), any case.
int open = std::min(save.progress.unlockedLevels("pirates"), levelsInStage);
bool done = save.progress.isCompleted("pirates", "piratos-sos");
std::int64_t best = save.progress.bestScore("piratos-sos");

// Level cleared: the level's number within its stage counts from 0, so this opens number 3.
bool record = save.progress.recordCompletion("pirates", 2, "piratos-sos", score);
save.saveProgress();
// Game over on a level: keep the best score without completing.
save.progress.recordScore("piratos-sos", score);

// Game over: ask for a name only if it makes the table (Menu::openGameOver(score, level) does this).
if (save.highScores.qualifies(score)) {
    std::optional<std::size_t> place = save.highScores.add(name, score, "piratos-sos");  // 0 = first
    save.saveHighScores();
}
for (const mb::HighScore& e : save.highScores.entries) { /* best first, at most 40 */ }
```

Names lose surrounding spaces and control characters and are cut to 12 UTF-8 characters; an empty one becomes
"Player". Equal scores keep the order they were added in. Stage ids and level names are compared without regard to
case (ASCII). The original's online scores (`ddScores.dll`) are gone; these are local only.
