# Architecture

Goal: native on macOS first, then other platforms without rewriting the game.

| Layer | Choice | Notes |
|---|---|---|
| Language and build | C++20, CMake | CMake also generates Xcode projects. |
| Window, input, audio device | SDL3 | Same API on macOS, Windows, Linux, iOS and consoles. |
| Rendering | SDL3 GPU API | Metal on macOS, Vulkan elsewhere. Shaders are GLSL, compiled ahead of time to SPIR-V and MSL by `tools/build_shaders.py` (glslang + SPIRV-Cross); the generated headers are committed, so building needs neither tool. |
| Physics | Jolt Physics (planned) | Replaces PhysX 2.x. Levels store PhysX boxes, spheres, capsules, convexes and joints, all of which map directly. |
| Audio decode | stb_vorbis (planned) | All sounds and music are Ogg Vorbis. |
| XML | Converted offline (planned for objects, sounds) | `materials.xml` is converted to JSON by `tools/materials.py`; the menus and the string table by `tools/ui.py`. |
| Text | The original's pre-rendered font atlases | Five `.crd` + `.dds` atlases of `Bookworm.ttf` and `brushType-SemiBold.TTF` ship with the game and are drawn as sprites (`BitmapFont`). SDL3_ttf stays an option for sizes they lack. |
| 2D UI | Sprites on the renderer, forms in `game/` | See below. |
| Meshes | Offline `.x` → binary glTF, loaded with cgltf | 272 meshes, all `xof 0303bin 0032`. See `ASSET_FORMATS.md`. |
| Images | stb_image | JPEG and PNG; the 25 DDS files are converted to PNG offline. |

## 2D UI

Menus, text and (later) the HUD are drawn as screen-space quads after the 3D scene:

| Layer | What |
|---|---|
| `engine/` `Sprite` | A textured, tinted, optionally rotated quad in target pixels. `Renderer::renderToWindow` and `renderToImage` take a sprite list after the debug lines; it is drawn alpha-blended without depth, in submission order, with or without a scene (`engine/shaders/sprite.*`). |
| `game/` `BitmapFont` | A font atlas and its glyph table (`tools/ui.py` output): measures, word-wraps, aligns and turns a string into sprites, with an optional drop shadow. |
| `game/` `StringTable` | The text of every language (`languages.json`). |
| `game/` `UiForm` | A menu form (`Data/Menu/*.json`): a tree of widgets with their data, state and pixel rectangles; maps the original's 4:3 layout onto any target (`UiLayout`, pillarboxed or letterboxed); hit testing and pointer events (hover, press, click). |
| `game/` `UiAssets` | Finds and uploads the images, fonts and strings below the assets folder; loads forms by name. |
| `game/` `UiDraw` | `drawForm`: a form to sprites, with each widget's state frame, shadows, hover growth, press offset, text and fade; the scrolling sky and a text field's caret. |
| `game/` `Menu` | The screens: owns the current form and the background behind it, turns pointer samples and keys into the original's behaviour (below), keeps the `Profile` (settings, high scores, unlocked levels). No GPU. |
| `game/` `GameFlow` | Where the player is: menu, loading, playing, paused. No GPU or window, so the whole flow is tested. |
| `game/` `Hud`, `Campaign` | The in-game overlay and the pause screen; the level list in stages (`Data/xml/levels.json`). |
| `app/` `FrontEnd` | The game as the player meets it: the window loop that drives `GameFlow`, `LevelSession` (what the scene code provides), and `HudOverlay` for `--hud`. |

Below the foundation, one form on its own (this is the model `Menu` is built on):

```cpp
UiAssets assets(renderer, mb::assetRoot());           // once
UiForm form = assets.loadForm("MainMenu");            // per screen
...
int w, h; renderer.drawableSize(w, h);
form.layout(UiLayout::fit(w, h), assets.imageSizes());
PointerEvents e = form.updatePointer({input.mouseX, input.mouseY}, input.leftButton);
// react to e.entered / e.pressed / e.clicked (widget->userData says what a button does)
std::vector<Sprite> sprites;
drawForm(form, assets, sprites, {.fade = fade});
renderer.renderToWindow(frame, items, lines, sprites);  // items and lines are empty for a menu
```

## Menus and screens

`MuggleBall3` with no arguments opens the main menu over the `background` form. The program is three layers, each
tested without the one above it:

```
Window::pollEvents ──InputEvents──► toUiInput ──UiInput──► GameFlow ──► Menu ──► forms (UiForm)
                                                    │
                                                    └─ Step{loadLevel} ──► LevelSession (the scene code)
```

- `Window` reports every mouse move and left-button change with its position (`InputEvents::pointer`), so a click made
  inside one frame is still a press and a release; keys (Escape is one, once `setEscapeQuits(false)`), typed text
  (`setTextInput`), and the window's size, fullscreen state and display size.
- `Menu` does the work of the screens. It is given a form loader, the `Campaign` and `MenuHooks`, and each frame
  `update(input, dt, layout, imageSizes)` returns `StartLevel(index)`, `Quit` or nothing.
- `GameFlow` adds the level: choosing one shows the Loading form for a frame, then asks the app to build the scene
  (`Step::loadLevel`; `loaded(ok)` answers), then plays with the HUD. Escape pauses; Escape again goes to the main menu,
  any other key or a mouse button resumes. A level that fails to build returns to its stage's form.
- `FrontEnd` (in `app/`) owns the loop and the GPU. The scene code implements `LevelSession` (`start`, `update`,
  `render`); in this tree that is a small adapter over the level viewer in `main.cpp`.

What the forms do (`Menu::intentOf` says what a widget is; every button of every shipped form has one, and the smoke test
checks that):

| Form | Behaviour |
|---|---|
| `MainMenu` | Start Game, Info, Options, High Scores open their forms (the buttons' `Navigate` data; the button's name is the form's). Exit quits. |
| `selectstage` | The four stage buttons open `stagepiratos`, `stagefarm`, `stagecowboys`, `stageknights`; Quick Start starts level 0; a stage with no unlocked level is disabled. The three bats are the player's choice: the one in use shows its "chosen" frame (the data's `disabled="yes"` on the red one is that), a click on another picks it. |
| `stage*` | One button per level, named by campaign index; a click starts it. Levels past `Profile::unlockedLevels` show the locked frame and do nothing. Quick Start starts the stage's first level. The bats are repeated. Select Stage goes back. |
| `Options` | Music and sound volume sliders (0 to 100 in steps of 5: press or drag the knob, or the - and + buttons), Smart Camera, Freeze Sky, Shadows, Mirrors and Fullscreen check boxes, and a list of window sizes (- and +). Every change goes to `MenuHooks::settingsChanged`; the Main Menu button (or Escape) saves the profile and, if the window size or fullscreen changed, calls `applyVideo`. |
| `highscores` | The local table on the map, eight lines a page with the arrows to turn it. The online buttons and the "downloading" text are hidden: that server is gone. |
| `EnterName`, `GameOver` | `Menu::openGameOver(score)` shows the first if the score makes the table, else the second. The name is typed into the field (twelve characters; Backspace; Enter or Apply enters it; an empty name is "Player"), then the table opens with the new line in green. |
| `endgame`, `endgame2`, `endgame3` | The story; the arrows move between them; The End goes to the score (`Menu::openEnding(score)`). Escape does not skip them. |
| `info`, `info2`, `info3`, `credits` | The arrows move between them; Main Menu goes back. |
| `Loading` | A progress bar from `Menu::setProgress`, no background form; stays up for at least its `show_time_msec`. |

Escape on a form goes one step back: to `selectstage` from a stage, to the main menu from every other form (Options
saves first); it does nothing on the main menu, the story forms, the name entry and Loading. The old viewer modes
(`--level`) still quit on Escape.

Forms fade in over their `fade_time_msec` (400) when opened; the form they replace goes at once. The background form's
sky scrolls by its `img_translation` and stops with Freeze Sky. A new form opens with the left button's state
ignored until it is released, so a button held through a change presses nothing on the new form.

### Hooks for the rest of the program

Everything outside the screens plugs in through `MenuHooks` (set in `FrontEndOptions::hooks`) and `LevelSession`:

| Hook | When |
|---|---|
| `playSound(name)` | Once as the pointer enters a widget that has `sound_mouse_in`, once as the button goes down on one with `sound_mouse_down`. The names are `Data/xml/sounds.xml` entries. Not set by the app: the sound system is another part. |
| `loadProfile()` | Once, when the menus are made: the saved `Profile` (settings, `unlockedLevels`, high scores). |
| `saveProfile(profile)` | Options left, a bat picked, a score entered. |
| `settingsChanged(settings)` | Every change of a setting as it happens, to set a volume live. |
| `applyVideo(settings)` | Options left with a changed window size or fullscreen. The app adds its own (it resizes the window) in front of the caller's. |
| `LevelSession::start(name, progress)` | The campaign name of a level (`piratos-ostrovok`, any case) is to be built; blocks, may report progress. |
| `LevelSession::update(input, dt, hud)` | Each frame of play, not while paused. The game changes `GameHud` here (lives, points, bonuses). |
| `LevelSession::render(renderer, overlay)` | Draw the scene, then the overlay sprites (HUD, pause screen) over it. |

## HUD

`drawHud(assets, GameHud, width, height, out)` appends the in-game overlay as sprites: a heart (`Icons/Heart.png`) for
each life down the left edge, "POINTS : n" bottom left and "LEVEL : n" bottom right (the string table's `Scores` and
`Level`, in the menus' bookworm font at size 22) over the purple band the levels end in, and an icon (`Icons/<name>.png`)
for each active bonus down the right edge. It is placed from measurements of the two original screenshots in
`reference/` (hearts 40 px at x 11, y 9, every 44 px; text 15 px from the edges, its line centred 18 px above the bottom;
all in 1024x768 units, scaled by the target's height) and anchored to the edges of the window, not of the menus' 4:3 area.
The bonus icons' place is a guess: no screenshot shows one. `drawPause` draws the pause screen from `Pause`,
`PressAnyKey` and `PressEscape`.

`GameHud` is filled with placeholders (three lives, no points, the level's number counting from 1, no bonuses) when a
level starts, until the game updates it. `MuggleBall3 --level <name> --hud [--hud-state 3,1529,1,Cannon]` shows it on the
level viewer or a screenshot.

## Rules

- `engine/` and `game/` never include platform headers. Anything OS-specific
  goes behind an engine interface or into `platform/<os>/`.
- The runtime reads converted assets only. Original formats are handled by
  the Python tools, so the runtime stays small and portable.
- Game data is never committed. `tools/convert_assets.py` builds it from the
  player's own copy.

## Original game, for reference

| Original | Replacement |
|---|---|
| Direct3D 8 | SDL3 GPU (Metal) |
| Ageia PhysX 2.x (`NxPhysics.dll`) | Jolt |
| BASS audio | SDL3 audio + stb_vorbis |
| HTMLayout (trial wrapper) | dropped |
| ddScores.dll (online scores, dead server) | local high scores |
