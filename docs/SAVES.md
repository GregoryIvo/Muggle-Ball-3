# Saves

What the player keeps between runs lives in three small JSON files in one folder. The code is
`engine/include/mb/engine/Files.h` (atomic file writes, `Paths.h` for the folder) and
`game/include/mb/game/SaveData.h` (the data and the files); `tests/test_save.cpp` covers it in a temporary
folder.

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
it (macOS ignores `$HOME` here, so this is how to keep experiments and tests away from real saves).

An app without bundled assets, started from the Finder, also looks for the converted assets in `assets/`
inside this folder (after `MB3_ASSETS`, the bundle and `./assets`).

## Files

Each file is an object with `"version": 1` first. `kSaveFormatVersion` in `SaveData.h` is the version of all
three; raise it when a file written by an older build would be misread, and add the conversion for the old
version to the loader (`loadDocument` in `SaveData.cpp` rejects every version above the current one, so an
old build never reads a newer file).

`settings.json`

```json
{
  "version": 1,
  "musicVolume": 0.7, "soundVolume": 0.8,
  "smartCamera": false, "freezeSky": false, "mirrors": true, "shadows": true,
  "fullscreen": false, "windowWidth": 1024, "windowHeight": 768
}
```

Volumes are 0 to 1 (the original's sliders count 0 to 100 in steps of 5). `smartCamera`, `freezeSky`,
`mirrors` and `shadows` are the original's Options checkboxes. The window size is the windowed size in points
(not pixels: a Retina display shows twice as many), and is kept while the window is fullscreen. The defaults
are mine, not the original's.

`progress.json`

```json
{
  "version": 1,
  "stages": {"piratos": {"unlockedLevels": 3, "completed": ["piratos-ostrovok", "piratos-sos"]}},
  "bestScores": {"piratos-ostrovok": 12000}
}
```

`highscores.json`

```json
{
  "version": 1,
  "entries": [{"name": "Gregory", "score": 12000, "level": "piratos-ostrovok"}]
}
```

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
  of zero or less is dropped; the list is sorted and cut to ten.
- An empty directory path (`userDataDir()` found no folder) keeps everything in memory, and the saves
  return false.

## Using it

The window code is wired in `app/main.cpp`: it loads the settings, opens the window at the saved size
and fullscreen state, and saves both when the window closes. The menu and gameplay code uses the same object:

```cpp
mb::SaveData save(mb::userDataDir());     // once, at start-up

// Options menu
save.settings.musicVolume = 0.5f;
save.saveSettings();

// Stage and level select: levels are named as in the catalog (LevelEntry::name), any case.
// A stage is whatever id the menu uses; the number of levels in it is the menu's business.
int open = std::min(save.progress.unlockedLevels("piratos"), levelsInStage);
bool done = save.progress.isCompleted("piratos", "piratos-sos");
std::int64_t best = save.progress.bestScore("piratos-sos");

// Level cleared: the level's number within its stage counts from 0, so this opens number 3.
bool record = save.progress.recordCompletion("piratos", 2, "piratos-sos", score);
save.saveProgress();
// Game over on a level: keep the best score without completing.
save.progress.recordScore("piratos-sos", score);

// Game over: ask for a name only if it makes the table.
if (save.highScores.qualifies(score)) {
    std::optional<std::size_t> place = save.highScores.add(name, score, "piratos-sos");  // 0 = first
    save.saveHighScores();
}
for (const mb::HighScore& e : save.highScores.entries) { /* best first, at most ten */ }
```

Names lose surrounding spaces and control characters and are cut to 24 bytes without splitting a UTF-8
character; an empty one becomes "Player". Equal scores keep the order they were added in. Stage ids and level
names are compared without regard to case (ASCII). The original's online scores (`ddScores.dll`) are gone;
these are local only.
