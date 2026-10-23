# Changelog

Notable changes to Muggle Ball 3. The format roughly follows
[Keep a Changelog](https://keepachangelog.com/). This project is pre-1.0: the
runtime is new code and the game's assets are supplied by the player, never
included here.

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
