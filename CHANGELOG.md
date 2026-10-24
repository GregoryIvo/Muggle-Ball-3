# Changelog

Notable changes to Muggle Ball 3. The format roughly follows
[Keep a Changelog](https://keepachangelog.com/). This project is pre-1.0: the
runtime is new code and the game's assets are supplied by the player, never
included here.

## Unreleased

### Added
- PlayStation Vita port, milestone M1a: the VitaSDK cross build (`cmake/Vita.cmake`),
  Jolt through its Platform Blue hook, a hello VPK that probes the console, a
  placeholder LiveArea set and `docs/VITA.md`. The desktop build is unchanged.
- PlayStation Vita port, milestone M4a: a GL back end (`engine/src/render/gl/`,
  `MB3_RENDERER=GL|VITAGL`) implementing the full `Renderer` API over OpenGL 2.1
  (desktop, through SDL, for testing) and vitaGL (the console), from one source;
  the Vita cross build now defaults to it (`MB3_VITA_BUILD_ENGINE` is ON) and
  builds a device render-test VPK (`platform/vita/render_test/`, title ID
  `MUGB00005`). `tools/build_shaders.py` gained a GL half and `tools/image_diff.py`
  compares screenshots between back ends.

- PlayStation Vita port, milestone M3a: `mb_engine` and `mb_game` cross-compile for the
  Vita by default now (`MB3_VITA_BUILD_ENGINE` defaults ON); portability fixes found no
  behaviour to change on the desktop (`STBI_NO_THREAD_LOCALS`, `error_code` filesystem
  calls, a fallback in `writeFileAtomic` for a rename a regular file a file system
  refuses, a missing log directory created on first use, `setLogFile` and
  `sceClibPrintf` logging on `__vita__`). `PhysicsWorld` gained `droppedSteps()`,
  `bodyCount()`, `activeBodyCount()` and `tempAllocatorPeakBytes()` (the step
  allocator's own high-water mark, with a one-time log on its first heap spill);
  `GameSession` gained its own `droppedSteps()`, the session's own accumulator plus the
  world's. A named `PhysicsProfile` ("desktop", "vita", see
  `game/include/mb/game/PhysicsProfile.h` and `docs/PHYSICS.md`) picks every level's
  `PhysicsSettings`, sized from `mb_physics_smoke --measure`; selected with
  `--physics-profile <name>` in `MuggleBall3` (the game and the level viewer alike),
  `mb_physics_smoke`, `mb_gameplay_smoke` and `mb_cloth_smoke`.
- PlayStation Vita port, milestone M3b: the physics benchmark (`app/bench/PhysicsBench.{h,cpp}`), shared by a
  desktop executable (`mb_physics_bench`) and a Vita VPK (`mb3_vita_bench.vpk`, title ID `MUGB00004`). It
  plays a configured set of levels with the Autopilot in fixed steps, timing each `GameSession::update()`
  call, and appends one CSV line per level, so a host run and a device run of the same `bench-physics.json`
  line up and their ratio becomes the measured speed of the console, replacing the plan's 45-110x (70x
  central) estimate once a device CSV exists. `tools/bench_ratio.py` joins the two CSVs and reports the
  ratios and the plan's per-step budget. See `docs/VITA.md`, "Physics benchmark", for the Mac's pass-1
  numbers and the 45-minute wall-time estimate.
- PlayStation Vita port, M3b review fixes: the bench now drains `GameSession::takeEvents()` every step, like
  `autoplay()` and the game, and derives its planned step count from the profile's own `fixedStep` instead of
  assuming 60 Hz. It times each step with the kernel's own process-time counter on the Vita
  (`steady_clock` there resolves to `gettimeofday`, not `CLOCK_MONOTONIC`, under this SDK's libstdc++) and
  logs that gap once at start-up. A level only counts as done, and only reaches a caller's `onLevel`, once
  its CSV line is actually written, with a fast open-and-close check of the output path before any level
  runs; `bench-physics.json` now rejects a non-positive, absurdly large, or overflowing `seconds`/solver
  value instead of producing a plausible-looking row from one. `LevelResult` gained `stopRequested`, tracked
  separately from `endedBy`, so a stop request that lands on a level's own natural end is neither
  mislabelled as an early "stopped" run nor silently lost. `mb3_vita_bench` pre-checks its levels against the
  catalogue so a missing one cannot desync the on-screen "level i/N". `tools/bench_ratio.py`'s headline ratio
  is now the mean-time median (section 4.4 derives its 70x from mean step times, not p95); it also flags and
  excludes a level whose host and device rows cover a different number of steps or a different `endedBy`,
  and keeps the most complete run, with a warning, when a level's join key repeats within one CSV (an
  interrupted run followed by a full re-run).

### Changed
- Renderer split into a backend-neutral part (`engine/src/render/RenderCommon.*`:
  draw collection and sorting, line/sprite/effect vertex building, the render-state
  key) and a back end chosen at build time by the new `MB3_RENDERER` option
  (`SDLGPU`, the SDL3 GPU API, default; `NONE`, a stub for builds with no GPU API).
  No behaviour change.
- PlayStation Vita port, milestone M5a: `runGame` moved out of `app/main.cpp`'s developer harness
  into `app/Game.{h,cpp}` as `runGame(const GameOptions&)`, taking `FrameStats` with it (`main.cpp`'s
  own copy is gone; its level viewer now uses `mb_app::FrameStats`), so the Vita's own entry point
  (M5b) can run the game without the harness. The save-data and window-size helpers still have a
  separate copy in each file, since `main.cpp`'s viewer and screenshot modes still take the harness's
  own `Options`, not `GameOptions`. No behaviour change on the desktop build.
- PlayStation Vita port, milestone M5b: the front end that makes the whole game playable with buttons
  and the stick alone, or the front touch screen alone. `ConsoleInput`
  (`engine/include/mb/engine/ConsoleInput.h`/`.cpp`, platform-neutral, unit-tested on the host in
  `tests/test_console_input.cpp`) turns the joystick, its stick and the front touch screen into the
  same `InputEvents` the mouse and keyboard already fill; `Window.cpp` reads the system's enter-button
  setting and feeds it from SDL on the Vita. `FrontEnd` draws a small crosshair cursor in the menus
  (there is no system one), and `Menu::setFullscreenAvailable(false)` hides the Fullscreen check box
  the way `setVideoModes({})` already hid the window-size list. `app/CMakeLists.txt` now builds
  `FrontEnd`, `FrontEndAudio`, `GameAudio`, `PlayerInput`, `PlaySession` and `Game.cpp` once as a
  static library, `mb_app`, shared by the desktop `MuggleBall3` and the Vita's own
  `platform/vita/game/`, which produces the game's data-free `MuggleBall3.vpk` (title ID
  `MUGB00003`); the hello moved to its own title ID, `MUGB00009`, so both install at once. No
  behaviour change on the desktop build (verified byte-identical screenshots).

  Review fixes on top of the above: a confirm press that reopens a cancelled, one-shot IME no longer also
  submits the name field's current text in the same press (the two were sent together; now exclusive);
  `findFrontTouchId`'s unreached fallback no longer reads `SDL_GetTouchDevices`' array after freeing it;
  a reconnecting PSTV pad (sleep, low battery) is now re-opened through `SDL_EVENT_JOYSTICK_ADDED` instead of
  leaving the game with no button input until restart, and `SDL_EVENT_JOYSTICK_REMOVED` releases anything the
  departing pad still held; `ConsoleInput::frame`/`press`/`release`/`moveCursorTo` fall back to the last good
  value on a non-finite dt or coordinate instead of latching NaN into the cursor permanently; `Window::keepAwake`
  is now a no-op off the Vita (it pumped SDL events on every platform during a level load, which is unneeded,
  and newly started processing OS events mid-load on desktop where the previous build did not).

## [0.1.0] - 2026-10-23

First tagged release. The game is playable on macOS from your own copy of
Magic Ball 3.

### Added
- Native macOS runtime on SDL3 with the GPU API (Metal); also builds and is
  tested on Linux in CI.
- Draws any of the 83 levels from the original's camera: models, animated
  textures, sky and the per-level light, with optional physics-shape wireframes.
- Physics world over Jolt (standing in for PhysX 2.x): every actor is a rigid
  body with its real shapes, mass, material and joints; the windmills turn; a
  test ball can be shot in to knock things over.
- Full front end: main menu, stage and level selection, options, high scores,
  name entry, info and credits; the in-game HUD; Escape pauses.
- Rules of play (`GameSession`): the bat, the ball, bricks that take damage,
  score and explode, hearts and lives, the end of a level and the campaign flow.
  `--autoplay` runs an autopilot.
- Special objects (dynamite chains, cannons, teleports, fireworks, springs,
  volcanoes, metal), the 28 prizes, the bat's four weapons plus the Thunderbolt
  and meteor strikes, the particle systems, the 35 path-following sharks and
  birds, and the 22 cloth sails.
- Sound and music per world; settings, progress, high scores and window state
  saved as JSON in the per-user folder.
- The in-game camera is set from the original's exact values, recovered from its
  binary (eye, target, vertical FOV and far plane).
- Asset pipeline (`tools/convert_assets.py`) converting a legally-owned copy's
  data to the engine's formats, and macOS packaging (`scripts/package_macos.sh`):
  universal binary, ad-hoc or Developer ID signing, and notarization.

### Known limitations
- Not yet implemented: reflections, the island that sinks, and most of the
  models' animation takes (only the bat's weapon barrels and the projectiles'
  turn are played).
- macOS 13.3 or later for packaged builds. Other desktop platforms build but are
  not yet a supported runtime target.

### Not included
- Any game assets. Convert them from your own installed copy of Magic Ball 3
  (see the README and `CONTRIBUTING.md`).

[0.1.0]: https://github.com/GregoryIvo/Muggle-Ball-3/releases/tag/v0.1.0
