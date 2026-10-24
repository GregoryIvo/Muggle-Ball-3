# Muggle Ball 3

A native, cross-platform re-implementation of **Magic Ball 3** (Dream Dale /
Alawar, 2006), built macOS-first. The runtime is new code; the game's art,
sound, music and levels are loaded from **your own installed copy** and are
never committed to this repository.

Status: early, but a level can be played. Step 2, drawing a level standing still, is done: the window opens
through the SDL3 GPU API (Metal on macOS) and shows any of the 83 converted
levels from the original's fixed camera, with models, textures (animated ones
included), sky and the level's own light, and wireframes of the physics shapes on
request. Step 3 puts the levels in a physics world (Jolt, standing in for PhysX):
every actor is a rigid body with its real shapes, mass, material and joints, the
level stands as designed until something hits it (the windmills turn), and a test
ball can be shot in to knock things over. The front end is in: the game starts at
the main menu, the original's menu screens work (stage and level selection,
options, high scores, name entry, info and credits), a level opens with the
in-game HUD, and Escape pauses it. Sound and music are in: the menus click and play
their music, each level plays the music of its world (the pause freezes it), and
the sound library is ready for gameplay to play hits (see
[`docs/AUDIO.md`](docs/AUDIO.md)). What the player sets is kept between runs: the
Options (volumes, check boxes, window size), the bat and the window itself (see
[`docs/SAVES.md`](docs/SAVES.md)). The rules of play are in as `GameSession` (the
bat, the ball, bricks that die and score, hearts, the end of a level; see
[`docs/GAMEPLAY.md`](docs/GAMEPLAY.md)), and a level is played with them: the mouse moves the
bat and a click launches the ball; the bat swoops in and out, the ball rolls and shrinks away
when it is lost, bricks that explode flash and leave (people and animals shrink away), damaged ones show their cracks, and every
hit has its sound. The hearts and points carry from level to level, a finished level opens the
next, a lost game goes to Game Over or the high scores, and `--autoplay` lets an autopilot play
(see below). The 35 sharks and birds that follow a path swim and dance along it, as kinematic bodies that push the ball
and the bricks they meet (see [`docs/LEVEL_FORMAT.md`](docs/LEVEL_FORMAT.md), "Path motion"). The 28 prizes (they drop from destroyed
objects, fall to the bat and change it, the ball or the board; `docs/GAMEPLAY.md` sections 15 and 16), the level's special objects (dynamite
chains, interactive cannons, teleports, firework crates, springs, volcanoes and metal; section 17), the bat's four weapons and the Thunderbolt and
meteor strikes (section 18), the particle systems of `Data/Particles` (section 19) and the 22 sails, which hang from their ships' yards, fill out in
the wind, fall when the ship breaks and score when they are hit (section 20), are in. Not there yet: reflections, the island that sinks and the other animation takes of the models (only the bat's weapon
barrels and the turn of the projectiles are played).

## Layout

| Path | What it is |
|---|---|
| `engine/` | Platform-neutral runtime: window and input, GPU renderer and shaders (back end chosen by `MB3_RENDERER`: `engine/src/render/sdlgpu/` for the SDL3 GPU API, `engine/src/render/none/` a stub for builds with no GPU API, shared code in `engine/src/render/RenderCommon.*`), model and image loading, physics (`PhysicsWorld` over Jolt), audio (Ogg Vorbis decoding, mixer, device), logging, paths |
| `game/` | Magic Ball rules: object types, level model and loader, materials, scene building, level physics (`PhysicsScene`), the rules of play (`GameSession`, `BallReach`, the autopilot `Autopilot`), the level's special objects (`SpecialObjects`), the prizes (`Prizes`, the session's `SessionPrizes.cpp`), the weapons and the strikes of the prizes (`Weapons`), the sails (`Cloth`, `ClothPlan`), objects that follow paths (`PathPlayer`), the particle systems (`ParticleDef`, `ParticleSystem`, `ParticleDraw`), what a session looks and sounds like (`PlayScene`, `ObjectEffects`, `BrickEffects`, `PrizeScene`, `PrizeHud`, `Billboard`, `BatBarrels`, `WeaponScene`, `WeaponEffects`, `ClothDraw`, `PlaySounds`), camera, sound library, save data; the 2D UI (bitmap fonts, string table, menu forms, the menu screens and their flow, the HUD) |
| `app/` | Executable entry point: `main.cpp` is a developer harness (the level viewer with physics and music, screenshots, the menu form viewer) around the game itself, `runGame` (`Game.cpp`). `runGame`'s own sources (`Game.cpp`, `FrontEnd`, `FrontEndAudio`, `GameAudio`, `PlayerInput`, `PlaySession`) are the static library `mb_app`, linked by the desktop `MuggleBall3` and by the PlayStation Vita's own entry point (`platform/vita/game/`) alike |
| `platform/macos/` | App bundle metadata |
| `platform/vita/` | PlayStation Vita: the hello that probes the console, a device render test, the game's own entry point and VPK (`platform/vita/game/`), Jolt's platform header, what newlib lacks, and the placeholder LiveArea set (see [`docs/VITA.md`](docs/VITA.md)) |
| `scripts/` | `package_macos.sh`: universal build, ad-hoc signing, Developer ID signing and notarization |
| `tools/` | Asset pipeline: `convert_assets.py` runs the converters for levels (`mb3level.py`), meshes (`x2glb.py`), materials, sounds and music (`sounds.py`), textures, the menus, strings and fonts (`ui.py`) and the level list (`campaign.py`), object and particle definitions (`objects.py`), and on macOS the app icon (`make_icon.py`); also the MoleBox dumper, the shader compiler (`build_shaders.py`, SDL GPU and GL), the Vita LiveArea placeholder (`vita_livearea.py`) and a PNG screenshot comparison (`image_diff.py`) |
| `docs/` | Architecture, level format, converted asset formats (`ASSET_FORMATS.md`), levels in the physics world (`PHYSICS.md`), sound and music (`AUDIO.md`), macOS packaging (`MACOS.md`), the PlayStation Vita port (`VITA.md`, its plan `VITA_PLAN.md`), save files (`SAVES.md`), what the object definitions say about gameplay (`OBJECTS.md`), the rules of play and the footage behind them (`GAMEPLAY.md`), how the data was recovered |
| `tests/` | C++ and Python tests; the ones that read the game data are skipped unless `MB3_ASSETS` is set |

## Build (macOS)

```sh
brew install cmake ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
python3 -m unittest discover -s tests -p 'test_*.py'
```

SDL3 and nlohmann/json come from Homebrew if installed (`brew install sdl3
nlohmann-json`) and are fetched by CMake otherwise; stb and cgltf are always
fetched. Use `-G Xcode` instead of Ninja to get an Xcode project. This build uses
Homebrew's SDL3, so it only runs on a Mac that has it, for its architecture only.
Adding `-DMB3_RENDERER=NONE` builds the renderer as a stub with no GPU API: the
`Renderer` constructor throws, and `mb_render_test` and the other GPU tests skip.
Adding `-DMB3_RENDERER=GL` builds the same renderer API against desktop OpenGL 2.1
through SDL instead of the SDL3 GPU API: the same back end `VITAGL` uses on the
Vita, so `mb_render_test` and a window exercise it here before any device run
(see [`docs/VITA.md`](docs/VITA.md), "GL back end").

### macOS app, universal binary, packaging

```sh
scripts/package_macos.sh --universal                  # arm64 + x86_64, SDL3 built from source, ad-hoc signed, in dist/
scripts/package_macos.sh --bundle-assets              # your own copy: the game's files inside the app, so it runs from the Finder
DEVELOPER_ID="Developer ID Application: ..." NOTARY_PROFILE=mb3-notary scripts/package_macos.sh --universal
```

The CMake options behind it are `MB3_UNIVERSAL`, `MB3_FETCH_SDL3`, `MB3_BUNDLE_ASSETS` and
`MB3_ASSETS_DIR`. [`docs/MACOS.md`](docs/MACOS.md) has the details and the steps for
signing and notarizing, which need your Apple Developer credentials. An app with the
game's files inside is for your own use only: they belong to Alawar and Dream Dale and must not be
redistributed. Packaged builds need macOS 13.3 or later.

The compiled shaders are committed (`engine/shaders/generated/`), so building
needs no shader tools. After editing a shader, run `python3 tools/build_shaders.py`
to regenerate them; that is the only thing that needs `glslangValidator` and
`spirv-cross` (`brew install glslang spirv-cross`).

### PlayStation Vita (in progress)

A homebrew port to the PS Vita and PSTV is under way; [`docs/VITA.md`](docs/VITA.md) has the toolchain, the
build, what was measured and the device checklist, and [`docs/VITA_PLAN.md`](docs/VITA_PLAN.md) the plan. With
VitaSDK installed, `cmake -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake` cross-builds what exists
so far: a hello VPK, a device render test, and the game itself, `MuggleBall3.vpk` (title ID `MUGB00003`, no
game data — the asset pack is copied to the console separately, `docs/VITA.md` "Installing the game"); the
desktop build is not affected.

## Game data

1. Install Magic Ball 3 and extract `MagicBall3.dat` with
   [`tools/molebox-dump`](tools/molebox-dump/README.md).
2. Convert everything into `assets/` (Python 3, nothing else to install; the
   formats are described in [`docs/ASSET_FORMATS.md`](docs/ASSET_FORMATS.md)):

   ```sh
   python3 tools/convert_assets.py --game "/path/to/Magic Ball 3" \
       --extracted /path/to/MB3OUT --out assets
   ```

   On macOS this also builds `assets/AppIcon.icns` from the icon in `MagicBall3.exe`; CMake
   puts it in the app bundle (run `cmake` again if you made it after configuring).

3. Run from the repo root, or point `MB3_ASSETS` at the folder:

   ```sh
   MuggleBall3=./build/app/MuggleBall3.app/Contents/MacOS/MuggleBall3
   $MuggleBall3                                  # the game: the main menu
   $MuggleBall3 --autoplay                       # the game, with the autopilot playing the levels you start
   $MuggleBall3 --level piratos-ostrovok --autoplay   # straight into a level, played by the autopilot, then on to the next
   $MuggleBall3 --level piratos-ostrovok         # the level viewer on a level, by name (any case) or by .json path
   $MuggleBall3 --screenshot out.png --level cowboys-napadenie [--size 1024x768] [--shapes]
   $MuggleBall3 --level piratos-ostrovok --hud [--hud-state 3,1529,1,Cannon:6,Magnit:30]   # with the in-game HUD (also in screenshots)
   $MuggleBall3 --screenshot out.png --level piratos-ostrovok --simulate 3 --ball -5,0   # a test ball, 3 s of physics
   $MuggleBall3 --screenshot out.png --level cowboys-napadenie --autoplay --simulate 20   # 20 s played by the autopilot, no window
   $MuggleBall3 --autoplay --level piratos-ostrovok --catch-prize magnit@2 --drop-prize bonus@3,-10   # a prize given at a time (GAMEPLAY.md 16.5)
   $MuggleBall3 --autoplay --level piratos-ostrovok --effect Fireworks/magenta@1,0,6,4 --screenshot out.png --simulate 1.25   # a particle effect at a time (GAMEPLAY.md 19.8)
   $MuggleBall3 --check assets/Data/Levels/piratos-po-krugy.json
   $MuggleBall3 --play-sound pirat_hit           # play a named sound (sounds.xml), wait, exit
   $MuggleBall3 --play-music Pirates --seconds 20  # one pass of Menu, Village, Pirates, Cowboys or Knights
   $MuggleBall3 --autoplay --physics-profile vita   # played with the Vita's PhysicsSettings (docs/PHYSICS.md), to check it on the host
   $MuggleBall3 --form MainMenu                  # a menu form on its own (any file in assets/Data/Menu, any case)
   $MuggleBall3 --form Options --screenshot out.png [--size 1024x768] [--language Russian]
   ```

   In the game: the main menu leads to Start Game, then a stage, then a level (or
   Quick Start); Escape in a menu goes one step back, Escape in a level pauses it
   (Escape again: main menu, any other key: resume). In a level the mouse is the bat (the
   cursor is hidden and kept in the window), a click or Space launches the ball, and you have
   two hearts to spare; a level is won when its last target is gone, then the next one of the
   campaign follows with your hearts and points, and the one you finished is open on the stage
   screens (the first level of each stage always is). Lose the last ball with no heart left and
   you get Game Over, or the name entry if your points make the high-score table. The menus have
   their sounds and music, the Options sliders set the volumes as you move them, and the game
   opens with the window size, fullscreen state, volumes and bat you left it with.
   In the level viewer (`--level`, `--shapes`, `--camera`, `--hud`, `--ball`): Left/Right (or PageUp/PageDown) switch
   level, Space shoots a test ball, P pauses the physics, L reloads the level (with
   fresh physics), F1 shows the physics shapes (where the bodies are now), left-drag
   orbits, right-drag pans, the wheel zooms, R returns to the game camera, Esc quits;
   the viewer plays the music of the level's world (a machine without an audio
   device plays silently), and F11 or Alt+Enter (and Ctrl+Cmd+F on macOS) toggle
   fullscreen; its window size and fullscreen are remembered between runs (`--size`
   is not).
   `--screenshot` renders one frame without a window; `--simulate <seconds>` (only
   with `--screenshot`) runs the physics that long first, and `--ball [x,degrees]`
   shoots a test ball at t = 0: from `(x, 1.2, -15)`, that many degrees off straight
   ahead, or without the argument at the middle of the movable bodies.
   `--camera ex,ey,ez,tx,ty,tz,fov` overrides the camera and `--size` sets the
   window or image size. `--autoplay` is the game, not the viewer: the autopilot (a player that needs
   no hands: it launches at once and keeps the bat where the ball will meet it) plays the levels,
   with `--level <name>` from that level on and without it from the ones you pick in the menus; it
   plays on nobody's behalf, so it reads and writes none of your saved options, progress and scores
   and leaves the mouse pointer alone;
   with `--screenshot` it plays `--simulate <seconds>` of the level without a window and the
   picture is the game's own, HUD included (a flash needs a lucky time to be caught). `--catch-prize <stem>[@seconds]`
   and `--drop-prize <stem>[@seconds[,x]]` (with `--autoplay`, repeatable) give the level a prize at that time of play,
   caught at once or falling from the far end of the board, to look at the effects without waiting for the drops
   (`<stem>` is a file of `Data/Objects/Prizes`: `bonus`, `magnit`, `tripple`...). `--effect <file>[/<name>][@seconds[,x,y,z[,x2,y2,z2]]]` (with `--autoplay`, repeatable)
   sets a particle effect of `Data/Particles` going at that time of play at a point (over the middle of the board without one; a beam's second end is the second point), to
   look at a file without waiting for the game to need it (`explosebig@5`, `Lightning@1,-8,1,0,8,1,10`; `docs/GAMEPLAY.md` 19.8). In the window
   the log reports the frame time every few seconds. `MB3_GPU_DEBUG=1` turns on the GPU back end's validation
   layer. `--physics-profile <name>` (any command above; default `desktop`, or `vita` on a Vita build) picks
   the `PhysicsSettings` every level is built with (`game/include/mb/game/PhysicsProfile.h`, `docs/PHYSICS.md`);
   an unknown name lists the known ones and exits. `--form` shows a menu form over the `background` form
   (`--no-background` leaves it out), with sample high scores; in the window Left/Right
   switch form and the menus work as in the game (the log names the sounds they would
   play and the settings they would save), and a screenshot can show one widget hovered
   or pressed with `--hover <name>` or `--press <name>`. A level JSON written by an
   older `tools/mb3level.py` still loads but logs a warning (old files drew wrong
   capsules with F1 / `--shapes`, and have no physics fields):
   run `convert_assets.py` again to refresh it.

   Settings, progress and high scores are saved as JSON in
   `~/Library/Application Support/MuggleBall3/` (see [`docs/SAVES.md`](docs/SAVES.md)); set
   `MB3_USER_DATA` to use another folder (the tests do, so that they never touch your saves).
   An app without bundled assets, started from the Finder, also looks for the converted
   assets in `assets/` inside that folder.

## Releases

Tagged builds are on the [releases page](https://github.com/GregoryIvo/Muggle-Ball-3/releases).
A release is a universal (arm64 + x86_64) macOS app that carries **no game data**:
install your own copy of Magic Ball 3 and convert its files (see "Game data"
above) next to the app or at `MB3_ASSETS`. The app is ad-hoc signed, so on first
launch macOS quarantines it; clear that once with:

```sh
xattr -dr com.apple.quarantine /path/to/MuggleBall3.app
```

(or right-click the app, choose Open, and confirm). Packaged builds need macOS
13.3 or later. A notarized build, which skips this step, needs an Apple Developer
ID — see [`docs/MACOS.md`](docs/MACOS.md).

## Contributing

Contributions are welcome — see [`CONTRIBUTING.md`](CONTRIBUTING.md) for how to
build, test and get the game data, the project layout, and the pull-request
process, and [`CHANGELOG.md`](CHANGELOG.md) for what has changed. The code is
MIT-licensed ([`LICENSE`](LICENSE)); it is original code and documentation only,
never game files.

## Legal

Magic Ball 3 and its assets are owned by Alawar Entertainment and Dream Dale.
This repository contains only original code and documentation. Do not commit
or distribute game files.
