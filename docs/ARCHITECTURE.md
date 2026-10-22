# Architecture

Goal: native on macOS first, then other platforms without rewriting the game.

| Layer | Choice | Notes |
|---|---|---|
| Language and build | C++20, CMake | CMake also generates Xcode projects. |
| Window, input, audio device | SDL3 | Same API on macOS, Windows, Linux, iOS and consoles. |
| Rendering | SDL3 GPU API | Metal on macOS, Vulkan elsewhere. Shaders are GLSL, compiled ahead of time to SPIR-V and MSL by `tools/build_shaders.py` (glslang + SPIRV-Cross); the generated headers are committed, so building needs neither tool. |
| Physics | Jolt Physics | Replaces PhysX 2.x. Levels store PhysX boxes, spheres, capsules, convexes and joints, all of which map directly. `PhysicsWorld` is the PhysX model over Jolt, `PhysicsScene` runs a level in it; how, and why, is in `PHYSICS.md`. |
| Audio | stb_vorbis, a software mixer, SDL3 audio | All sounds and music are Ogg Vorbis. See [`AUDIO.md`](AUDIO.md). |
| XML | Converted offline | `materials.xml`, the sound and music lists, the menus and the string table, and the object and particle definitions are converted to JSON by `tools/materials.py`, `tools/sounds.py`, `tools/ui.py` and `tools/objects.py` (see `OBJECTS.md`). |
| Text | The original's pre-rendered font atlases | Five `.crd` + `.dds` atlases of `Bookworm.ttf` and `brushType-SemiBold.TTF` ship with the game and are drawn as sprites (`BitmapFont`). SDL3_ttf stays an option for sizes they lack. |
| 2D UI | Sprites on the renderer, forms in `game/` | See below. |
| Meshes | Offline `.x` → binary glTF, loaded with cgltf | 272 meshes, all `xof 0303bin 0032`. See `ASSET_FORMATS.md`. |
| Images | stb_image | JPEG and PNG; the 25 DDS files are converted to PNG offline. |
| Saves | JSON in the per-user folder | Settings, progress and high scores in three small versioned files, written atomically (`SAVES.md`). `SaveData` is the only model of them: the menus work on it directly. |
| macOS app | CMake bundle, `scripts/package_macos.sh` | Universal binary, icon, optional bundled assets, signing and notarization (`MACOS.md`). |

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
| `game/` `Menu` | The screens: owns the current form and the background behind it, turns pointer samples and keys into the original's behaviour (below), shows and changes the `SaveData` it is given (settings, progress, high scores) and writes it. No GPU. |
| `game/` `GameFlow` | Where the player is: menu, loading, playing, paused; and what a level's end leads to (the next level, Game Over, the ending), with the progress and the scores saved. No GPU or window, so the whole flow is tested. |
| `game/` `Hud`, `Campaign` | The in-game overlay and the pause screen; the level list in stages (`Data/xml/levels.json`). |
| `app/` `FrontEnd` | The game as the player meets it: the window loop that drives `GameFlow`, `LevelSession` (what the scene code provides: `PlaySession`), `FrontEndEvents` (menus, level start, pause: the music and the pause of the sound follow them, `FrontEndAudio`), the cursor (hidden while a level plays), and `HudOverlay` for `--hud`. |

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
- `Menu` does the work of the screens. It is given a form loader, the `Campaign`, the `SaveData` and `MenuHooks`, and each frame
  `update(input, dt, layout, imageSizes)` returns `StartLevel(index)`, `Quit` or nothing.
- `GameFlow` adds the level: choosing one starts a game (the hearts of a new game, no points), shows the Loading form
  for a frame, then asks the app to build the scene (`Step::loadLevel`; `loaded(ok)` answers), then plays with the HUD.
  Escape pauses; Escape again goes to the main menu, any other key or a mouse button resumes. A level that fails to
  build returns to its stage's form. When the level says it has ended (`levelEnded`): a level won is saved
  (`Progress::recordCompletion`) and the next level of the campaign is asked for at once, with the hearts and the points
  in the HUD carried on and no Loading form (the original has none); after the last level the ending forms
  (`Menu::openEnding`) open; a game lost goes to Game Over or the name entry (`Menu::openGameOver`), and its points are
  the level's best score.
- `FrontEnd` (in `app/`) owns the loop and the GPU. The scene code implements `LevelSession` (`start`, `update`,
  `render`): `PlaySession` for the game. `runFrontEnd` hides the system cursor while a level plays (`Window::setCursorHidden`,
  which also keeps the pointer in the window) and can open straight on a level (`FrontEndOptions::startLevel`).

What the forms do (`Menu::intentOf` says what a widget is; every button of every shipped form has one, and the smoke test
checks that):

| Form | Behaviour |
|---|---|
| `MainMenu` | Start Game, Info, Options, High Scores open their forms (the buttons' `Navigate` data; the button's name is the form's). Exit quits. |
| `selectstage` | The four stage buttons open `stagepiratos`, `stagefarm`, `stagecowboys`, `stageknights` (the first level of every stage is always open, so every stage can be entered); Quick Start starts level 0. The three bats are the player's choice: the one in use shows its "chosen" frame (the data's `disabled="yes"` on the red one is that), a click on another picks it. |
| `stage*` | One button per level, named by campaign index; a click starts it. Levels the saved `Progress` has not opened (`SAVES.md`) show the locked frame and do nothing, unless `Menu::setLevelsLocked(false)`, which the game uses while nothing can finish a level. Quick Start starts the stage's first level. The bats are repeated. Select Stage goes back. |
| `Options` | Music and sound volume sliders (0 to 100 in steps of 5, stored as volumes from 0 to 1: press or drag the knob, or the - and + buttons), Smart Camera, Freeze Sky, Shadows, Mirrors and Fullscreen check boxes, and a list of window sizes (- and +). Every change goes to `MenuHooks::settingsChanged`; the Main Menu button (or Escape) saves the settings and, if the window size or fullscreen changed, calls `applyVideo`. The sizes are the original's 4:3 shapes that fit the display; the form opens showing the window as it is (resized by dragging, or fullscreen with F11): the Fullscreen box as the window has it, the size as the nearest one of the list. |
| `highscores` | The local table on the map, eight lines a page with the arrows to turn it (the save data keeps five pages, 40 entries). The online buttons and the "downloading" text are hidden: that server is gone. |
| `EnterName`, `GameOver` | `Menu::openGameOver(score, level)` shows the first if the score makes the table, else the second. The name is typed into the field (twelve characters; Backspace; Enter or Apply enters it, with the level it was made on; an empty name is "Player"), then the table opens with the new line in green. |
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
| `playSound(name)` | Once as the pointer enters a widget that has `sound_mouse_in`, once as the button goes down on one with `sound_mouse_down`. The names are `Data/xml/sounds.xml` entries. The game plays them through `GameAudio` (`AUDIO.md`). |
| `settingsChanged(settings)` | Every change of a setting as it happens, to set a volume live; `FrontEnd` also calls it once at the start with the saved settings. |
| `applyVideo(settings)` | Options left with a changed window size or fullscreen. The app adds its own (it resizes the window) in front of the caller's. |
| `LevelSession::start(name, hud, progress)` | The campaign name of a level (`piratos-ostrovok`, any case) is to be built; blocks, may report progress. `hud` has the hearts and points the player brings in. |
| `LevelSession::update(input, dt, hud)` | Each frame of play, not while paused. The game changes `GameHud` here (lives, points, bonuses). Returns how the level ended (`LevelOutcome::Complete` or `GameOver`, once its fade out is over), else `Playing`. |
| `LevelSession::render(renderer, overlay)` | Draw the scene, then the overlay sprites (HUD, pause screen) over it. |

What the player keeps needs no hook: the `Menu` changes the `SaveData` it was made with and writes it (Options left, a bat picked, a score entered). `FrontEndEvents` (in `FrontEndOptions::events`) tell the program where the game is, for the sound:

| Event | When |
|---|---|
| `menuShown()` | At the start, and again whenever the menus come back (from a level, or after a level that could not be built). |
| `levelStarted(name)` | The Loading form has gone and the level plays; `name` is the campaign's. |
| `paused(bool)` | The pause screen came up, or went away: before the menus or the level go on. |
| `frame()` | Once per frame. |

## HUD

`drawHud(assets, GameHud, width, height, out)` appends the in-game overlay as sprites: a heart (`Icons/Heart.png`) for
each life down the left edge, "POINTS : n" bottom left and "LEVEL : n" bottom right (the string table's `Scores` and
`Level`, in the menus' bookworm font at size 22) over the purple band the levels end in, and an icon (`Icons/<name>.png`)
for each active bonus down the right edge. It is placed from measurements of the two original screenshots in
`reference/` (hearts 40 px at x 11, y 9, every 44 px; text 15 px from the edges, its line centred 18 px above the bottom;
all in 1024x768 units, scaled by the target's height) and anchored to the edges of the window, not of the menus' 4:3 area.
The bonus icons' place is a guess: no screenshot shows one. `drawPause` draws the pause screen from `Pause`,
`PressAnyKey` and `PressEscape`.

In the game the `GameHud` is the game's: `GameFlow` sets the hearts of a new game (`GameplayRules::startHearts`), no points
and the level's number counting from 1 when a game starts, `PlaySession` keeps the hearts and the points of the rules in
it, and it carries over to the next level. `MuggleBall3 --level <name> --hud [--hud-state 3,1529,1,Cannon]` shows
placeholders on the level viewer or a screenshot.

## Gameplay

`GameSession` (`game/Gameplay.h`) is one level being played: the rules of `GAMEPLAY.md` on top of a `PhysicsScene`. It
has no GPU, no window and no audio. The program gives it the time, the cursor and the click each frame, and reads back
what to draw and play:

```cpp
GameSession session(level, materials, {.hearts = 2, .score = 0});
...
session.update(dt, {.batTargetX = *batTargetX(mouseX, mouseY, w, h, camera), .launch = clicked});
for (const GameEvent& e : session.takeEvents()) { /* BrickHit, BrickDestroyed, BatHit, BallLost, LifeLost... */ }
// draw: session.scene().actorPose(i) for each actor not session.actor(i).removed; session.bat(); session.balls();
// the HUD: session.hearts(), session.score(); session.phase() says when the level is Complete or the game GameOver
```

| What | Where |
|---|---|
| Every number the rules use, each with its source in `GAMEPLAY.md` and how well it is known | `GameplayRules` |
| What happens, for the sound and the look | `GameEvent`: `BallLaunched`, `BatHit`, `BoardHit`, `BallsHit`, `BrickHit`, `BrickDestroyed`, `ActorFell`, the special objects' `DynamiteBlast`, `CannonFired`, `ShotHit`, `RocketLaunched`, `RocketBurst`, `BallEntered`, `BallEmerged`, then `BallLost`, `LifeLost`, `BatLeft`, `BatEntered`, `LevelComplete`, `GameOver` |
| Which bricks are hurt, and which must go for the level to end | `canBeDamaged`, `isTarget`, `TargetRule` (the default `Bricks`, every brick and tree whatever its motion, is the rule of the footage, `GAMEPLAY.md` "Rules settled by the second study"; `BrickType`, H-type of `OBJECTS.md`, is refuted and kept for comparing and for tests; chosen in `SessionOptions::levelEnd`); targets out of the ball's reach do not count (below) |
| The cursor as a bat position | `cursorOnPlane`, `batTargetX` |
| A player that needs no hands | `Autopilot`, `autoplay()` (returns the events; `mb_gameplay_smoke --log <level>` prints them) |

The sharks and birds that follow a path move without the rules doing anything: `PhysicsScene` owns a `PathPlayer`
(`game/PathPlayer.h`: the curve through a path's points, walked at a steady speed, and where each bound actor is at a
time) and sets the kinematic target of each path actor before every fixed step, so a session's ball and bricks meet
them where they are (`PhysicsScene::pathMovers()`, `pathTime()`). The player is pure (no physics, no GPU) and has its
own tests: `mb_path_tests` on made-up paths, `mb_path_smoke` on the real levels; the format is in `LEVEL_FORMAT.md`
("Path motion") and the rules in `GAMEPLAY.md` section 13.

The level's special objects (dynamite, interactive cannons, teleports, firework crates, springs, volcanoes, generators, metal, vibros and
flickers) are the rules of `SpecialObjects` (`game/SpecialObjects.h`, `GAMEPLAY.md` sections 14 and 15), a member of `GameSession` that is its friend and
is called at three places: `processContacts` (a ball with a blow met an object that the ball does not hurt: `ballHit`), `explodeDue` (a charge
of dynamite was destroyed: `destroyed`, its blast) and once a fixed step (`update`: what touched the cannons and the crates, the cannonballs,
the rockets, the balls in teleports, the vibros). It reports through the session's events and keeps the state the picture reads
(`GameSession::objects()`: `shots()`, `rockets()`, `flickers()`); a ball in a teleport has no body and is no lost ball
(`BallView::away`, `appear`). Its numbers are `SpecialObjectRules` in `GameplayRules::objects`. `canBeDamaged` says which objects something can
hurt (not the teleports), `SpecialObjects` which of them the ball does not (`GAMEPLAY.md` 15.1). It has its own tests: `mb_special_object_tests` on
made-up levels and `mb_special_object_smoke` on the real ones.

What falls off the board stops being a target and is taken out (`ActorFell`), with no points and no explosion
(`GameplayRules::fallLimitY`, `fallScores`): loose pieces are destroyed by weapons and blasts, not by falling.

A person or an animal (`shrinksAway`: a material of `exploseType` 5) is paid at `BrickDestroyed` and goes 0.35 s later
(`GameplayRules::shrinkTime`), shrinking away meanwhile (`ActorStatus::shrink`, 0 to 1): it is a target until it is gone, so
`LevelComplete` comes after the shrink, and nothing hurts it. The other bricks are gone with their score.

Which targets count: whatever the `TargetRule`, a target does not once the ball cannot get at it
(`SessionOptions::ignoreUnreachable`, on). `BallReach` cuts the plane the ball moves on into a grid and fills it from the bat's end of the
board around the immovable parts of the plan (walls, platforms, pinned buildings: static bodies that are no brick), a
ball's radius from each; a target within the ball's height with no such ground within a ball's radius of it (behind a wall,
past the board's edge, in a slot narrower than the ball), or above it and pinned or over no such ground, is out of play. It is
looked at when the level starts and twice a second at the targets that are loose bodies, and stays out
(`ActorStatus::outOfPlay`, `SessionStats::targetsOutOfPlay`). What the look cannot see (a piece that stays up where the physics left it) is
let go when balls have flown for 90 s without touching a brick and few targets are left (`GameplayRules::stallSeconds`,
`SessionStats::targetsLetGo`). No level can then be impossible for want of such a target (`GAMEPLAY.md` section 12 has what the autopilot
makes of it).

Tests: `mb_gameplay_tests` on made-up levels (the bat's law, the life sequence, damage and explosions, the end of the
level under each reading and the required set, the targets out of the ball's reach, bricks that wake what they held, the cursor); `mb_gameplay_smoke`
plays all 83 levels with the autopilot against the real data and checks only that nothing breaks and that the ball keeps its speed
and its plane.

## Playing

`PlaySession` (`app/`) is the game's `LevelSession`: it builds a level's `GameSession` and its picture, feeds the rules the mouse and
the click each frame, and turns what they report into what is seen and heard.

| Part | What it does |
|---|---|
| `GameSession` + `Autopilot` | The rules; with `--autoplay` the autopilot's input replaces the mouse's. `PlayerInput` (`app/`, the keys are SDL's) turns the mouse into the bat's target through `batTargetX` and the drawable size; a click (the button going down, not held) or Space launches. The click or key that ends the pause is not given to it: `GameFlow::Step::resumed` makes the front end call `LevelSession::resume()` instead of `update()` for that frame, which starts `PlayerInput` over. |
| `PlayScene` (`game/`) | The picture of a session as a list of draws: `LevelScene` for the level (actors at their physics poses, what exploded or fell hidden, a hit brick retextured by its damage state, a person or an animal that was destroyed scaled down about its centre while it shrinks, `LevelScene::setScale`), the bat (the frames of `bat.x` that make the default bat, with the texture of `Settings::bat`), the balls (rolling by the distance over the radius, a soft shadow, shrinking away over 0.25 s when lost), and the flash and sparks at each `BrickDestroyed` (not for a person or an animal, who shrinks instead). Reads the session and its events; no window. |
| `ObjectEffects` (`game/`, drawn by `PlayScene`) | What the special objects look like: the flash and sparks of a blast, a cannonball (`Yadro.x`) with its shadow and the puff at the muzzle, a rocket (`Fire-rocket.x`) with its trail and its burst, the ring where a ball goes into a teleport and comes out, a spring that gives (`LevelScene::setScale`), the glow of a flicker. Billboards and two meshes from the session's events and `objects()`; no rules, no window. The full particle systems come later. |
| `PlaySounds` (`game/`) | An event as its sound through the `SoundLibrary`, panned by x (the table is in `AUDIO.md`). |
| the fades | A level comes up out of the dark in 0.6 s; when the last target is gone the picture holds for 0.1 s (0.45 s when the game is lost) and fades out in 0.8 s, under the HUD; then `update` reports the outcome. The level cannot be paused meanwhile (`LevelSession::ending()` to `GameFlow::setLevelEnding`): leaving from the pause would lose the outcome, the win never recorded or the game over never reaching the name entry. |

The session steps by 1/60 s; the picture carries the balls and the bat on by their velocities for the time the frame is ahead of the
last step, so a display at 120 Hz moves smoothly (a ball held on the bat goes on with the bat, `ballPosition`). `--autoplay` plays on
nobody's behalf: its `SaveData` has no folder, so it reads and writes none of the player's files, and the front end leaves the system
cursor alone (`FrontEndOptions::hideCursor`). Tests: `mb_play_smoke` plays real levels on the GPU without a window (a crate shows
its damage textures, a brick leaves the picture, the autopilot wins a level with the right hold and its fade out is reported, the click
that ends a pause launches nothing), `mb_frontend_smoke` drives the front end in a window and has a stand-in level win, then lose, to see
the next level start with the hearts and points carried and the files written, and that Escape does not pause a level that is fading out,
`test_menu` does the same flow without a window.

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
