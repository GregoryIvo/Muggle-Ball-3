# Muggle Ball 3

A native, cross-platform re-implementation of **Magic Ball 3** (Dream Dale /
Alawar, 2006), built macOS-first. The runtime is new code; the game's art,
sound, music and levels are loaded from **your own installed copy** and are
never committed to this repository.

Status: early. The window opens through the SDL3 GPU API (Metal on macOS) and
shows any of the 83 converted levels from the original's fixed camera, standing
still: models, textures, sky and lighting, no physics or gameplay yet.

## Layout

| Path | What it is |
|---|---|
| `engine/` | Platform-neutral runtime: window, GPU renderer, logging, paths |
| `game/` | Magic Ball rules: object types, level model, level loader |
| `app/` | Executable entry point |
| `platform/macos/` | App bundle metadata (the only per-platform code so far) |
| `tools/` | Asset pipeline: `.mb3` level converter, archive unpacker, MoleBox dumper |
| `docs/` | Architecture, level format, how the data was recovered |
| `tests/` | C++ and Python tests that need no game data |

## Build (macOS)

```sh
brew install cmake ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

SDL3 and nlohmann/json are fetched automatically by CMake. Use
`-G Xcode` instead of Ninja to get an Xcode project.

## Game data

1. Install Magic Ball 3 and extract `MagicBall3.dat` with
   [`tools/molebox-dump`](tools/molebox-dump/README.md).
2. Convert everything into `assets/`:

   ```sh
   python3 tools/convert_assets.py --game "/path/to/Magic Ball 3" \
       --extracted /path/to/MB3OUT --out assets
   ```

3. Run from the repo root, or point `MB3_ASSETS` at the folder:

   ```sh
   MuggleBall3=./build/app/MuggleBall3.app/Contents/MacOS/MuggleBall3
   $MuggleBall3                                  # first level
   $MuggleBall3 --level piratos-ostrovok         # a level by name (any case) or by .json path
   $MuggleBall3 --screenshot out.png --level cowboys-napadenie [--size 1024x768] [--shapes]
   $MuggleBall3 --check assets/Data/Levels/piratos-po-krugy.json
   ```

   In the window: Left/Right (or PageUp/PageDown) switch level, F1 shows the
   physics shapes, left-drag orbits, right-drag pans, the wheel zooms, R returns
   to the game camera, Esc quits. `--screenshot` renders one frame without a
   window; `--camera ex,ey,ez,tx,ty,tz,fov` overrides the camera.

## Legal

Magic Ball 3 and its assets are owned by Alawar Entertainment and Dream Dale.
This repository contains only original code and documentation. Do not commit
or distribute game files.
